#include "api.h"

/*
 * Fireworks 2 — rockets climb on a slightly parabolic arc, shedding orange
 * exhaust sparks, then burst into anti-aliased shells that leave glowing
 * trails (frame fade). Shell types: Peony (white core then colour),
 * Chrysanthemum (long tails), Palm (thick rising branches), Ring, Willow
 * (golden sparks that drift down slowly), Crackle (sparks pop into white
 * mini-bursts), Double (two-colour inner + outer shell). Palettes pick the
 * shell colours. Rendered into a framebuffer with m_blend for sub-pixel
 * motion; X wraps around the cylinder. Y=0 is the bottom.
 *
 * Structure note: wasm3 on the ESP32 has no tail calls, so every opcode of a
 * wasm function nests on the native stack until that function returns (or a
 * loop iterates). One huge inlined update() overflowed the 64 KB render
 * stack, so the frame is split into NOINLINE stages — each call unwinds.
 */

static const char META[] =
    "{\"name\":\"Fireworks\","
    "\"desc\":\"Rockets launch upward and explode into colorful particle bursts\","
    "\"params\":["
        "{\"id\":0,\"name\":\"Count\",\"type\":\"int\",\"min\":1,\"max\":10,\"default\":5,\"desc\":\"Number of simultaneous fireworks\"},"
        "{\"id\":1,\"name\":\"Brightness\",\"type\":\"int\",\"min\":1,\"max\":255,\"default\":200,\"desc\":\"Overall brightness\"},"
        "{\"id\":2,\"name\":\"Type\",\"type\":\"select\",\"options\":[\"Auto\",\"Peony\",\"Chrysanthemum\",\"Palm\",\"Ring\",\"Willow\",\"Crackle\",\"Double\"],\"default\":0,\"desc\":\"Shell type, or a random mix\"},"
        "{\"id\":3,\"name\":\"Palette\",\"type\":\"select\",\"options\":[\"Auto\",\"Gold\",\"Red-White-Blue\",\"Rainbow\",\"Pastel\",\"Neon\"],\"default\":0,\"desc\":\"Shell colours\"},"
        "{\"id\":4,\"name\":\"Trail\",\"type\":\"int\",\"min\":0,\"max\":100,\"default\":40,\"desc\":\"Length of the glowing trails\"},"
        "{\"id\":5,\"name\":\"Gravity\",\"type\":\"int\",\"min\":0,\"max\":100,\"default\":40,\"desc\":\"How fast sparks fall\"},"
        "{\"id\":6,\"name\":\"Launch Rate\",\"type\":\"int\",\"min\":1,\"max\":100,\"default\":50,\"desc\":\"How often rockets launch\"},"
        "{\"id\":7,\"name\":\"Sparks\",\"type\":\"int\",\"min\":1,\"max\":100,\"default\":50,\"desc\":\"Sparks per shell\"}"
    "]}";

EXPORT(get_meta_ptr) int get_meta_ptr(void){ return (int)META; }
EXPORT(get_meta_len) int get_meta_len(void){ return sizeof(META)-1; }

#define NOINLINE __attribute__((noinline))

#define MAX_W 64
#define MAX_H 64
static uint8_t FB[MAX_W*MAX_H*3];
EXPORT(get_framebuffer) int get_framebuffer(void){ return (int)FB; }
static int W=32,H=48;

/* ---- PRNG ---- */
static uint32_t rng=92731;
static uint32_t rnd(void){ uint32_t x=rng; x^=x<<13; x^=x>>17; x^=x<<5; rng=x; return x; }
static int rrange(int lo,int hi){ if(lo>=hi)return lo; return lo+(int)(rnd()%(uint32_t)(hi-lo)); }
static float frand(void){ return (float)(rnd()&0xFFFF)/65536.0f; }

#define TWO_PI 6.28318530f

/* ---- shells ---- */
#define MAX_FW 10
#define PER_FW 64
#define MAX_P (MAX_FW*PER_FW)
#define MAX_M 192            /* micro sparks: rocket exhaust + crackle children */

#define PH_IDLE 0
#define PH_ROCKET 1
#define PH_BURST 2

#define T_PEONY 0
#define T_CHRYS 1
#define T_PALM 2
#define T_RING 3
#define T_WILLOW 4
#define T_CRACKLE 5
#define T_DOUBLE 6
#define T_COUNT 7

static int   fw_phase[MAX_FW], fw_type[MAX_FW], fw_timer[MAX_FW], fw_age[MAX_FW];
static float fw_x[MAX_FW], fw_y[MAX_FW], fw_vx[MAX_FW], fw_vy[MAX_FW], fw_ty[MAX_FW];
static int   fw_hue[MAX_FW], fw_sat[MAX_FW], fw_hue2[MAX_FW], fw_sat2[MAX_FW];
static int   fw_flash[MAX_FW];          /* ms of burst flash remaining */

/* shell sparks (slice per shell) */
static float p_x[MAX_P], p_y[MAX_P], p_vx[MAX_P], p_vy[MAX_P];
static int   p_ttl[MAX_P], p_max[MAX_P];
static int   p_rgb[MAX_P];              /* full-brightness colour */
static uint8_t p_kind[MAX_P];           /* 0 normal, 1 chrys, 2 willow, 3 crackle */
static uint8_t p_drag[MAX_P];           /* drag per second * 32 */

/* micro sparks */
static float m_x[MAX_M], m_y[MAX_M], m_vx[MAX_M], m_vy[MAX_M];
static int   m_ttl[MAX_M], m_max[MAX_M], m_rgb[MAX_M];
static int   m_cursor=0;

static int prev_tick=0;

/* per-frame globals (set in update, read by the stages) */
static int   g_delta, g_bsh, g_nsp, g_pal, g_type_sel;
static float g_dt, g_gravity;

/* ---- colour helpers ---- */
static inline int scale_rgb(int rgb,int sh){
    if(sh<=0)return 0; if(sh>256)sh=256;
    return ((((rgb>>16)&255)*sh>>8)<<16)|((((rgb>>8)&255)*sh>>8)<<8)|((rgb&255)*sh>>8);
}
/* lerp toward white by t (0..256) */
static inline int whiten(int rgb,int t){
    if(t<=0)return rgb; if(t>256)t=256;
    int r=(rgb>>16)&255,g=(rgb>>8)&255,b=rgb&255;
    r+=((255-r)*t)>>8; g+=((255-g)*t)>>8; b+=((255-b)*t)>>8;
    return (r<<16)|(g<<8)|b;
}

/* pick shell colours for a palette */
static NOINLINE void pick_colours(int pal,int i){
    int h,s,h2,s2;
    switch(pal){
        case 1: h=rrange(22,40); s=rrange(190,256); h2=rrange(30,46); s2=150; break;         /* gold */
        case 2: { int k=rrange(0,3); h=(k==0)?0:(k==1)?0:165; s=(k==1)?0:255;
                  int k2=(k+1+rrange(0,2))%3; h2=(k2==0)?0:(k2==1)?0:165; s2=(k2==1)?0:255; } break; /* red-white-blue */
        case 3: h=rrange(0,256); s=255; h2=(h+128)&255; s2=255; break;                      /* rainbow (per-spark hue spread) */
        case 4: h=rrange(0,256); s=rrange(90,140); h2=(h+85)&255; s2=110; break;             /* pastel */
        case 5: { static const int NEON[4]={128,213,85,42}; int k=rrange(0,4); h=NEON[k]; s=255; h2=NEON[(k+1+rrange(0,3))%4]; s2=255; } break;
        default: h=rrange(0,256); s=rrange(200,256); h2=(h+rrange(60,196))&255; s2=255; break;
    }
    fw_hue[i]=h; fw_sat[i]=s; fw_hue2[i]=h2; fw_sat2[i]=s2;
}

static inline float wrapx(float x){ while(x<0)x+=(float)W; while(x>=(float)W)x-=(float)W; return x; }

/* blend with cylinder seam */
static NOINLINE void splat(float x,float y,int rgb){
    m_blend(FB,W,H,x,y,rgb);
    if(x<1.0f) m_blend(FB,W,H,x+(float)W,y,rgb);
    else if(x>(float)W-1.0f) m_blend(FB,W,H,x-(float)W,y,rgb);
}

static NOINLINE void micro_spawn(float x,float y,float vx,float vy,int ttl,int rgb){
    int k=m_cursor; m_cursor=(m_cursor+1)%MAX_M;
    m_x[k]=x; m_y[k]=y; m_vx[k]=vx; m_vy[k]=vy; m_ttl[k]=ttl; m_max[k]=ttl; m_rgb[k]=rgb;
}

static NOINLINE void launch(int i){
    fw_phase[i]=PH_ROCKET;
    fw_x[i]=frand()*(float)W; fw_y[i]=0.0f;
    fw_vx[i]=(frand()-0.5f)*(float)H*0.12f;
    fw_vy[i]=(float)H*(0.85f+frand()*0.5f);
    fw_ty[i]=(float)H*(0.5f+frand()*0.42f);
    fw_type[i]=(g_type_sel==0)?rrange(0,T_COUNT):g_type_sel-1;
    fw_age[i]=0;
    pick_colours(g_pal,i);
}

static NOINLINE void spawn_spark(int pi,float x,float y,float vx,float vy,int ttl,int rgb,int kind,float drag){
    p_x[pi]=x; p_y[pi]=y; p_vx[pi]=vx; p_vy[pi]=vy;
    p_ttl[pi]=ttl; p_max[pi]=ttl; p_rgb[pi]=rgb; p_kind[pi]=(uint8_t)kind;
    int d=(int)(drag*32.0f); if(d>255)d=255; p_drag[pi]=(uint8_t)d;
}

/* one spark of a shell: velocity/life/colour by shell type */
static NOINLINE void burst_spark(int i,int j,int nsp,int branches,float S){
    int type=fw_type[i], pi=i*PER_FW+j;
    float a=((float)j+frand()*0.8f)*TWO_PI/(float)nsp;
    float ca=m_cos(a), sa=m_sin(a);
    float vx,vy; int ttl,kind=0; float drag=1.0f;
    int h=fw_hue[i], s=fw_sat[i];
    int rainbow=(g_pal==3);
    if(rainbow) h=(int)(a*40.74f)&255;
    switch(type){
        case T_CHRYS: { float sp=S*(0.9f+frand()*0.35f); vx=ca*sp; vy=sa*sp; ttl=rrange(1100,1700); kind=1; drag=0.7f; } break;
        case T_PALM:  { int br=j%branches;
                        float ba=((float)br+0.5f)*3.14159265f/(float)branches;      /* upper half only */
                        float sp=S*(0.7f+frand()*0.7f);
                        vx=m_cos(ba)*sp*0.8f; vy=m_sin(ba)*sp+S*0.25f; ttl=rrange(900,1400); drag=0.9f; } break;
        case T_RING:  { float sp=S*1.05f; float sq=0.25f+frand()*0.3f; vx=ca*sp; vy=sa*sp*sq; ttl=rrange(800,1100); drag=1.1f; } break;
        case T_WILLOW:{ float sp=S*(0.6f+frand()*0.5f); vx=ca*sp; vy=sa*sp+S*0.15f; ttl=rrange(1600,2400); kind=2; drag=2.6f;
                        h=rrange(24,40); s=rrange(180,256); } break;
        case T_CRACKLE:{ float sp=S*(0.7f+frand()*0.5f); vx=ca*sp; vy=sa*sp; ttl=rrange(500,900); kind=3; drag=1.2f; } break;
        case T_DOUBLE:{ int inner=(j&1); float sp=S*(inner?0.45f:1.0f)*(0.9f+frand()*0.2f);
                        vx=ca*sp; vy=sa*sp; ttl=rrange(800,1200);
                        if(inner){ h=fw_hue2[i]; s=fw_sat2[i]; if(rainbow) h=(h+128)&255; } drag=1.0f; } break;
        default:      { float sp=S*(0.85f+frand()*0.3f); vx=ca*sp; vy=sa*sp; ttl=rrange(700,1100); drag=1.0f; } break;
    }
    int rgb=m_hsv((h+rrange(-8,9))&255,s,255);
    spawn_spark(pi,fw_x[i],fw_y[i],vx,vy,ttl,rgb,kind,drag);
}

static NOINLINE void burst(int i){
    int nsp=g_nsp;
    fw_phase[i]=PH_BURST; fw_age[i]=0; fw_flash[i]=140;
    float S=12.0f+(float)H*0.36f;             /* base spark speed px/s */
    int branches=7+rrange(0,4);               /* palm: rays per shell */
    for(int j=0;j<nsp;j++) burst_spark(i,j,nsp,branches,S);
    for(int j=nsp;j<PER_FW;j++) p_ttl[i*PER_FW+j]=0;
}

/* crackle pop: white flash + a few short micro sparks */
static NOINLINE void crackle_pop(int pi){
    int n=3+rrange(0,3);
    for(int k=0;k<n;k++){
        float a=frand()*TWO_PI, sp=4.0f+frand()*10.0f;
        micro_spawn(p_x[pi],p_y[pi],p_vx[pi]*0.3f+m_cos(a)*sp,p_vy[pi]*0.3f+m_sin(a)*sp,rrange(180,380),0xFFFFFF);
    }
}

/* ---- stage: one shell (rocket flight / burst bookkeeping / flash) ---- */
static NOINLINE void step_shell(int i){
    int delta=g_delta, bsh=g_bsh; float dt=g_dt;
    switch(fw_phase[i]){
    case PH_IDLE:
        fw_timer[i]-=delta;
        if(fw_timer[i]<=0) launch(i);
        break;
    case PH_ROCKET: {
        fw_age[i]+=delta;
        fw_vy[i]-=g_gravity*0.25f*dt;
        fw_x[i]=wrapx(fw_x[i]+fw_vx[i]*dt); fw_y[i]+=fw_vy[i]*dt;
        /* exhaust */
        int ex=0xFF6A10;
        for(int k=0;k<2;k++)
            micro_spawn(fw_x[i]+(frand()-0.5f)*0.6f,fw_y[i]-0.5f,
                        fw_vx[i]*0.2f+(frand()-0.5f)*10.0f,-(4.0f+frand()*10.0f),rrange(120,300),scale_rgb(ex,140+rrange(0,100)));
        /* head: flickering white-hot point */
        int hb=(bsh*(200+rrange(0,57)))>>8;
        splat(fw_x[i],fw_y[i],scale_rgb(whiten(m_hsv(fw_hue[i],fw_sat[i],255),160),hb));
        if(fw_y[i]>=fw_ty[i] || fw_vy[i]<=(float)H*0.1f) burst(i);
    } break;
    case PH_BURST: {
        fw_age[i]+=delta;
        int base=i*PER_FW, alive=0;
        for(int j=0;j<g_nsp;j++) if(p_ttl[base+j]>0){ alive=1; break; }
        if(!alive && fw_flash[i]<=0){
            fw_phase[i]=PH_IDLE;
            fw_timer[i]=rrange(150,1200)*60/get_param_i32(6);
        }
    } break;
    }
    /* burst flash: soft white blob decaying over ~140 ms */
    if(fw_flash[i]>0){
        int f=fw_flash[i]; fw_flash[i]-=delta;
        int rgb=scale_rgb(0xFFFFFF,(bsh*f*256/140)>>8);
        splat(fw_x[i],fw_y[i],rgb);
        int half=scale_rgb(rgb,110);
        splat(fw_x[i]-0.9f,fw_y[i],half); splat(fw_x[i]+0.9f,fw_y[i],half);
        splat(fw_x[i],fw_y[i]-0.9f,half); splat(fw_x[i],fw_y[i]+0.9f,half);
    }
}

/* ---- stage: one shell spark (physics + draw) ---- */
static NOINLINE void step_spark(int pi,int age){
    int delta=g_delta; float dt=g_dt;
    p_ttl[pi]-=delta;
    if(p_ttl[pi]<=0){ p_ttl[pi]=0; return; }
    /* crackle: pop at 55-75% of life */
    if(p_kind[pi]==3 && p_ttl[pi]<p_max[pi]*(55+(pi&15))/100){ crackle_pop(pi); p_ttl[pi]=0; return; }
    float d=1.0f-(float)p_drag[pi]*dt/32.0f; if(d<0)d=0;
    p_vy[pi]-=g_gravity*dt;
    p_vx[pi]*=d; p_vy[pi]*=d;
    p_x[pi]=wrapx(p_x[pi]+p_vx[pi]*dt); p_y[pi]+=p_vy[pi]*dt;
    if(p_y[pi]<-1.0f){ p_ttl[pi]=0; return; }
    if(p_y[pi]>=(float)H+1.0f) return;          /* above the top, may fall back */
    /* brightness: life curve + flicker in the second half */
    int life=p_ttl[pi]*256/p_max[pi];
    int v=life;
    if(p_kind[pi]==1) v=256-(((256-life)*(256-life))>>8);            /* chrys: stays bright longer */
    if(life<128) v=(v*(160+(int)(rnd()&95)))>>8;                     /* flicker */
    if(p_kind[pi]==2 && life<100) v=(v*(90+(int)(rnd()&165)))>>8;    /* willow: sputter */
    int rgb=p_rgb[pi];
    if(age<160) rgb=whiten(rgb,(160-age)*256/160);                  /* white core at the start */
    splat(p_x[pi],p_y[pi],scale_rgb(rgb,(v*g_bsh)>>8));
}

static NOINLINE void step_sparks(int count){
    for(int i=0;i<count;i++){
        if(fw_phase[i]!=PH_BURST) continue;
        int base=i*PER_FW, age=fw_age[i];
        for(int j=0;j<g_nsp;j++){
            int pi=base+j;
            if(p_ttl[pi]>0) step_spark(pi,age);
        }
    }
}

/* ---- stage: micro sparks ---- */
static NOINLINE void step_micro(void){
    int delta=g_delta; float dt=g_dt;
    for(int k=0;k<MAX_M;k++){
        if(m_ttl[k]<=0) continue;
        m_ttl[k]-=delta;
        if(m_ttl[k]<=0){ m_ttl[k]=0; continue; }
        m_vy[k]-=g_gravity*0.6f*dt;
        m_x[k]=wrapx(m_x[k]+m_vx[k]*dt); m_y[k]+=m_vy[k]*dt;
        if(m_y[k]<-1.0f){ m_ttl[k]=0; continue; }
        int life=m_ttl[k]*256/m_max[k];
        splat(m_x[k],m_y[k],scale_rgb(m_rgb[k],(life*g_bsh)>>8));
    }
}

static void dims(void){ W=get_width(); H=get_height();
    if(W>MAX_W)W=MAX_W; if(H>MAX_H)H=MAX_H; if(W<1)W=1; if(H<1)H=1; }

EXPORT(init) void init(void){
    dims(); rng=92731; prev_tick=0; m_cursor=0;
    for(int i=0;i<MAX_FW;i++){ fw_phase[i]=PH_IDLE; fw_timer[i]=rrange(100,900); fw_flash[i]=0; }
    for(int i=0;i<MAX_P;i++) p_ttl[i]=0;
    for(int i=0;i<MAX_M;i++) m_ttl[i]=0;
    m_fill(FB,MAX_W*MAX_H,0);
}

EXPORT(update) void update(int tick_ms){
    int oW=W,oH=H; dims();
    if(oW!=W||oH!=H) init();

    int count=get_param_i32(0), bright=get_param_i32(1), type_sel=get_param_i32(2), pal=get_param_i32(3);
    int trail=get_param_i32(4), grav=get_param_i32(5), sparks=get_param_i32(7);
    if(count<1)count=1; if(count>MAX_FW)count=MAX_FW;
    if(bright<1)bright=1; if(bright>255)bright=255;
    if(type_sel<0||type_sel>T_COUNT)type_sel=0; if(pal<0||pal>5)pal=0;
    if(trail<0)trail=0; if(trail>100)trail=100;
    if(grav<0)grav=0; if(grav>100)grav=100;
    if(sparks<1)sparks=1; if(sparks>100)sparks=100;
    int nsp=16+sparks*48/100; if(nsp>PER_FW)nsp=PER_FW;

    int delta=tick_ms-prev_tick; if(delta<=0||delta>200)delta=33; prev_tick=tick_ms;
    rng^=(uint32_t)tick_ms;

    g_delta=delta; g_dt=(float)delta/1000.0f;
    g_gravity=(float)H*(0.15f+(float)grav*0.017f);
    g_bsh=bright+1;                                       /* 1..256 */
    g_nsp=nsp; g_pal=pal; g_type_sel=type_sel;

    /* trails: fade the previous frame */
    if(trail==0) m_fill(FB,W*H,0);
    else m_fade(FB,W*H*3,120+trail*125/100);

    for(int i=0;i<count;i++) step_shell(i);
    step_sparks(count);
    step_micro();
    draw();
}
