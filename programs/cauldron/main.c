#include "api.h"

/*
 * Cauldron — a magic cauldron. The lower part of the lamp is a liquid whose
 * surface undulates like Waves, but instead of a fixed hue the colour drifts
 * along a palette (the same palettes as Painter). Bubbles rise through the
 * liquid and burst at the surface, throwing glowing sparks upward that wobble
 * and fade as they float away. Y=0 is the bottom, X wraps around the cylinder.
 */

static const char META[] =
    "{\"name\":\"Magic Cauldron\","
    "\"desc\":\"A bubbling potion whose colour drifts along a palette, throwing sparks up from the surface\","
    "\"params\":["
        "{\"id\":0,\"name\":\"Speed\",\"type\":\"int\",\"min\":1,\"max\":10,\"default\":4,\"desc\":\"How fast the surface waves travel\"},"
        "{\"id\":1,\"name\":\"Palette\",\"type\":\"select\",\"options\":[\"Rainbow\",\"UV Neon\",\"Fire\",\"Ice\",\"Magenta-Cyan\",\"Yellow-Red-Green\",\"Pastel\"],\"default\":1,\"desc\":\"Colours the potion cycles through\"},"
        "{\"id\":2,\"name\":\"Colour Cycle\",\"type\":\"int\",\"min\":1,\"max\":100,\"default\":30,\"desc\":\"How fast the potion colour walks along the palette\"},"
        "{\"id\":3,\"name\":\"Brightness\",\"type\":\"int\",\"min\":1,\"max\":255,\"default\":210,\"desc\":\"Overall brightness\"},"
        "{\"id\":4,\"name\":\"Level\",\"type\":\"int\",\"min\":5,\"max\":95,\"default\":25,\"desc\":\"Potion fill level (%)\"},"
        "{\"id\":5,\"name\":\"Choppiness\",\"type\":\"int\",\"min\":0,\"max\":100,\"default\":40,\"desc\":\"Wave height on the surface\"},"
        "{\"id\":6,\"name\":\"Bubbling\",\"type\":\"int\",\"min\":0,\"max\":100,\"default\":50,\"desc\":\"How many bubbles and sparks\"},"
        "{\"id\":7,\"name\":\"Spark Height\",\"type\":\"int\",\"min\":1,\"max\":100,\"default\":40,\"desc\":\"How high the sparks fly (100 = up to the top of the lamp)\"}"
    "]}";

EXPORT(get_meta_ptr) int get_meta_ptr(void){ return (int)META; }
EXPORT(get_meta_len) int get_meta_len(void){ return sizeof(META)-1; }

#define MAX_W 64
#define MAX_H 64
#define MAX_SPARK 96
#define MAX_BUB 24
#define SPARK_DRAG 0.9f       /* air drag, 1/s: sparks slow down as they rise */

/* wasm3 on the ESP32 chains every opcode on the native stack until a wasm
 * function returns, so frame stages stay real functions (no inlining). */
#define NOINLINE __attribute__((noinline))

static uint8_t FB[MAX_W*MAX_H*3];
EXPORT(get_framebuffer) int get_framebuffer(void){ return (int)FB; }

static int W=32,H=48;
static int prev_tick=0;
static uint32_t phase=0;          /* wave phase, advances with time*speed */
static float palpos=0.0f;         /* potion colour position along the palette 0..256 */
static float SINT[256];           /* sine table for the per-pixel shimmer */
static float surf[MAX_W];         /* surface height per column (px) */
static float crest[MAX_W];        /* 0..1 how high this column's crest is */
static uint8_t DR[256],DG[256],DB[256];   /* per-frame depth colour LUT (full value) */

/* per-frame values shared between stages */
static float g_dt=0.033f, g_bubbling=0.5f, g_height=0.4f;
static int   g_bright=210;
static uint32_t g_shph=0;

/* ---- PRNG ---- */
static uint32_t rng=0x2545F491u;
static uint32_t rnd(void){ uint32_t x=rng; x^=x<<13; x^=x>>17; x^=x<<5; rng=x; return x; }
static float frand(void){ return (float)(rnd()&0xFFFF)/65536.0f; }

/* ---- palette LUT: 256 packed 0xRRGGBB (same palettes as Painter) ---- */
static int PAL[256];
static int cur_pal=-1;
static inline int tri(int i){ return i<128 ? i*2 : (255-i)*2; }   /* 0..254..0, seamless */
static NOINLINE void build_palette(int p){
    for(int i=0;i<256;i++){
        int t=tri(i), h, s=255, v=255;
        switch(p){
            case 1: h=96 + t*150/254; break;                   /* UV neon: green-cyan-blue-magenta */
            case 2: h=t*42/254; break;                         /* fire: red-orange-yellow */
            case 3: h=128 + t*42/254; s=90 + t*165/254; break; /* ice: pale cyan - deep blue */
            case 4: h=128 + t*85/254; break;                   /* magenta-cyan through blue */
            case 5: h=t*85/254; break;                         /* green-yellow-red */
            case 6: h=i; s=110; break;                         /* pastel rainbow */
            default: h=i; break;                               /* rainbow */
        }
        PAL[i]=m_hsv(h&255,s,v);
    }
    cur_pal=p;
}

/* ---- sparks (rise above the surface, wobble, fade) ---- */
static float sx[MAX_SPARK], sy[MAX_SPARK], svx[MAX_SPARK], svy[MAX_SPARK];
static float slife[MAX_SPARK], smax[MAX_SPARK], sph[MAX_SPARK], swob[MAX_SPARK];
static int   scol[MAX_SPARK];
static uint8_t salive[MAX_SPARK];

/* ---- bubbles (rise inside the liquid, burst at the surface) ---- */
static float bx[MAX_BUB], by[MAX_BUB], brad[MAX_BUB], bvel[MAX_BUB], bph[MAX_BUB];
static uint8_t balive[MAX_BUB];

static inline float wrapx(float x){ while(x<0.0f)x+=(float)W; while(x>=(float)W)x-=(float)W; return x; }

/* sine of an integer angle (0..255 per period) — native, for the surface */
static float fsin(int angle){ return m_sin((float)angle*(6.28318530f/256.0f)); }

static NOINLINE void spawn_spark(float x,float y,float power){
    for(int i=0;i<MAX_SPARK;i++){
        if(salive[i]) continue;
        salive[i]=1;
        sx[i]=wrapx(x); sy[i]=y;
        /* aim for a rise of (Spark Height %) of the free space above the surface;
         * with drag k the travel over life T is v0/k*(1-e^-kT), solve for v0 */
        float T=0.8f+frand()*1.0f;
        float rise=((float)H-y)*g_height*power*(0.7f+0.6f*frand());
        if(rise<1.0f) rise=1.0f;
        float v0=rise*SPARK_DRAG/(1.0f-m_exp(-SPARK_DRAG*T))*1.2f;
        svx[i]=(frand()-0.5f)*5.0f;
        svy[i]=v0;
        smax[i]=T;
        slife[i]=T;
        sph[i]=frand()*6.2831853f;
        swob[i]=1.0f+frand()*2.5f;
        scol[i]=((int)palpos + 24 + (int)(frand()*48.0f)) & 255;   /* a bit ahead of the potion colour */
        return;
    }
}

static NOINLINE void spawn_bubble(void){
    for(int i=0;i<MAX_BUB;i++){
        if(balive[i]) continue;
        int col=(int)(frand()*(float)W); if(col>=W)col=W-1;
        float top=surf[col];
        if(top<2.0f) return;                     /* too shallow to bubble */
        balive[i]=1;
        bx[i]=(float)col+frand();
        by[i]=top*(0.05f+frand()*0.45f);
        brad[i]=0.7f+frand()*1.1f;
        bvel[i]=2.5f+frand()*4.0f+brad[i]*1.5f;
        bph[i]=frand()*6.2831853f;
        return;
    }
}

/* ---- 1. surface height per column (integer wave numbers: seamless) ---- */
static NOINLINE void build_surface(int level,int chop){
    int ph1=(int)(phase/45), ph2=(int)(phase/28), ph3=(int)(phase/17);
    float level_px=(float)level*0.01f*(float)H;
    float amp_px=(float)chop*0.01f*(float)H*0.20f;
    for(int x=0;x<W;x++){
        int base=x*256/W;
        float s=0.55f*fsin(1*base+ph1) + 0.30f*fsin(2*base-ph2) + 0.20f*fsin(3*base+ph3);
        surf[x]=level_px+amp_px*s;
        crest[x]=(s+1.05f)/2.10f;
    }
}

/* ---- 2. depth colour LUT: surface = potion colour, deeper = a little further
 * along the palette and darker; built once per frame from the palette LUT ---- */
static NOINLINE void build_depth_lut(void){
    int p0=(int)palpos&255, p1=(p0+28)&255;
    int c0=PAL[p0], c1=PAL[p1];
    int r0=(c0>>16)&255, g0=(c0>>8)&255, b0=c0&255;
    int r1=(c1>>16)&255, g1=(c1>>8)&255, b1=c1&255;
    for(int i=0;i<256;i++){
        int m=i*160/255;                          /* 0..160 blend toward deep colour */
        DR[i]=(uint8_t)(r0+((r1-r0)*m>>8));
        DG[i]=(uint8_t)(g0+((g1-g0)*m>>8));
        DB[i]=(uint8_t)(b0+((b1-b0)*m>>8));
    }
}

/* ---- 3. liquid fill with anti-aliased surface line and crest glow ---- */
static NOINLINE void render_liquid(void){
    int bright=g_bright;
    uint32_t shph=g_shph;
    float invH=1.0f/(float)H;
    for(int x=0;x<W;x++){
        float sy0=surf[x];
        float glow=crest[x]*crest[x]*0.55f;
        int xs=x*34;
        for(int y=0;y<H;y++){
            uint8_t*p=FB+(y*W+x)*3;
            float ds=sy0-(float)y;
            float cov=ds+0.5f;
            if(cov<=0.0f){ p[0]=0; p[1]=0; p[2]=0; continue; }
            if(cov>1.0f) cov=1.0f;
            float td=ds*invH; if(td<0.0f)td=0.0f; if(td>1.0f)td=1.0f;
            float vf=0.42f+0.58f*(1.0f-td);
            vf+=SINT[(xs+y*19+(int)shph)&255]*0.10f*(1.0f-td);
            if(vf<0.0f)vf=0.0f;
            int val=(int)(vf*(float)bright*cov+0.5f);
            if(val>255)val=255;
            int di=(int)(td*255.0f);
            int r=DR[di]*val/255, g=DG[di]*val/255, b=DB[di]*val/255;
            /* glowing rim just under the surface line */
            if(ds<1.8f){
                float fb=1.0f-ds/1.8f; if(fb>1.0f)fb=1.0f; if(fb<0.0f)fb=0.0f;
                float gs=glow*fb; if(ds<0.0f) gs*=(1.0f+ds);
                if(gs<0.0f)gs=0.0f;
                int gw=(int)(gs*(float)bright);
                r+=(255-r)*gw/255; g+=(255-g)*gw/255; b+=(255-b)*gw/255;
            }
            p[0]=(uint8_t)(r>255?255:r); p[1]=(uint8_t)(g>255?255:g); p[2]=(uint8_t)(b>255?255:b);
        }
    }
}

/* ---- 4. bubbles: rise, wobble, burst at the surface into sparks ---- */
static NOINLINE void step_bubbles(void){
    float dt=g_dt;
    for(int i=0;i<MAX_BUB;i++){
        if(!balive[i]) continue;
        bph[i]+=dt*4.0f;
        by[i]+=bvel[i]*dt;
        bx[i]=wrapx(bx[i]+m_sin(bph[i])*1.5f*dt);
        int col=(int)bx[i]; if(col<0)col=0; if(col>=W)col=W-1;
        if(by[i]>=surf[col]-brad[i]*0.5f){
            balive[i]=0;
            int n=2+(int)(brad[i]*2.5f);
            float power=0.8f+brad[i]*0.5f;
            for(int k=0;k<n;k++) spawn_spark(bx[i]+(frand()-0.5f)*brad[i]*2.0f, surf[col]+0.3f, power);
        }
    }
}

/* one soft bubble: brighten the liquid inside its disc, strongest at the rim */
static NOINLINE void draw_bubble(int i){
    float cx=bx[i], cy=by[i], r=brad[i];
    int x0=(int)(cx-r-1.0f)-1, x1=(int)(cx+r+1.0f)+1;
    int y0=(int)(cy-r-1.0f)-1, y1=(int)(cy+r+1.0f)+1;
    if(y0<0)y0=0; if(y1>H-1)y1=H-1;
    for(int y=y0;y<=y1;y++){
        float dy=(float)y-cy;
        for(int x=x0;x<=x1;x++){
            int xx=x%W; if(xx<0)xx+=W;
            if((float)y>surf[xx]-0.5f) continue;            /* only inside the liquid */
            float dx=(float)x-cx;
            float d=__builtin_sqrtf(dx*dx+dy*dy);
            float cov=r+0.5f-d; if(cov<=0.0f) continue; if(cov>1.0f)cov=1.0f;
            float rim=1.0f-(r-d)*0.6f/(r+0.01f); if(rim<0.35f)rim=0.35f; if(rim>1.0f)rim=1.0f;
            int ci=(int)(cov*rim*150.0f);
            uint8_t*p=FB+(y*W+xx)*3;
            p[0]+=((255-p[0])*ci)>>8; p[1]+=((255-p[1])*ci)>>8; p[2]+=((255-p[2])*ci)>>8;
        }
    }
}

static NOINLINE void draw_bubbles(void){
    for(int i=0;i<MAX_BUB;i++) if(balive[i]) draw_bubble(i);
}

/* ---- 5. sparks: float up with a sideways wobble, slow down, fade ---- */
static NOINLINE void step_sparks(void){
    float dt=g_dt;
    for(int i=0;i<MAX_SPARK;i++){
        if(!salive[i]) continue;
        slife[i]-=dt;
        if(slife[i]<=0.0f){ salive[i]=0; continue; }
        sph[i]+=dt*swob[i]*3.0f;
        svy[i]-=svy[i]*SPARK_DRAG*dt;
        if(svy[i]<1.5f) svy[i]=1.5f;
        svx[i]-=svx[i]*1.5f*dt;
        sx[i]=wrapx(sx[i]+(svx[i]+m_sin(sph[i])*swob[i])*dt);
        sy[i]+=svy[i]*dt;
        if(sy[i]>(float)H+1.0f) salive[i]=0;
    }
}

static NOINLINE void draw_sparks(void){
    int bright=g_bright;
    for(int i=0;i<MAX_SPARK;i++){
        if(!salive[i]) continue;
        float t=slife[i]/smax[i];                   /* 1 fresh .. 0 dying */
        float a=t*t*(3.0f-2.0f*t);                  /* smooth fade */
        int c=PAL[scol[i]];
        int r=(c>>16)&255, g=(c>>8)&255, b=c&255;
        int wt=(int)((t*t)*170.0f);                 /* fresh sparks are white-hot */
        r+=(255-r)*wt>>8; g+=(255-g)*wt>>8; b+=(255-b)*wt>>8;
        int v=(int)(a*(float)bright);
        r=r*v/255; g=g*v/255; b=b*v/255;
        int rgb=(r<<16)|(g<<8)|b;
        float fx=sx[i], fy=sy[i];
        m_blend(FB,W,H,fx,fy,rgb);
        if(fx>(float)(W-1)) m_blend(FB,W,H,fx-(float)W,fy,rgb);   /* cylinder seam */
    }
}

/* ---- 6. spawning: bubbles from the deep + small fizz straight off the surface ---- */
static float bub_acc=0.0f, fizz_acc=0.0f;
static NOINLINE void spawn_stage(void){
    float dt=g_dt, k=g_bubbling;
    float wf=(float)W/32.0f;
    bub_acc+=k*k*9.0f*wf*dt;                        /* bubbles per second at 32 wide */
    while(bub_acc>=1.0f){ bub_acc-=1.0f; spawn_bubble(); }
    fizz_acc+=k*k*10.0f*wf*dt;
    while(fizz_acc>=1.0f){
        fizz_acc-=1.0f;
        int col=(int)(frand()*(float)W); if(col>=W)col=W-1;
        if(frand()<crest[col]) spawn_spark((float)col+frand(), surf[col]+0.2f, 0.6f);
    }
}

static void dims(void){ W=get_width(); H=get_height();
    if(W>MAX_W)W=MAX_W; if(H>MAX_H)H=MAX_H; if(W<1)W=1; if(H<1)H=1; }

static NOINLINE void reset_particles(void){
    for(int i=0;i<MAX_SPARK;i++) salive[i]=0;
    for(int i=0;i<MAX_BUB;i++) balive[i]=0;
    bub_acc=0.0f; fizz_acc=0.0f;
}

EXPORT(init) void init(void){
    dims();
    prev_tick=0; phase=0; palpos=0.0f;
    for(int i=0;i<256;i++) SINT[i]=m_sin((float)i*(6.28318530f/256.0f));
    build_palette(1);
    reset_particles();
}

EXPORT(update) void update(int tick_ms){
    int oW=W,oH=H; dims();
    if(oW!=W||oH!=H) reset_particles();

    int speed=get_param_i32(0), pal=get_param_i32(1), cyc=get_param_i32(2);
    int bright=get_param_i32(3), level=get_param_i32(4), chop=get_param_i32(5);
    int bubbling=get_param_i32(6), height=get_param_i32(7);
    if(speed<1)speed=1; if(pal<0||pal>6)pal=0;
    if(cyc<1)cyc=1; if(bright<1)bright=1; if(bright>255)bright=255;
    if(bubbling<0)bubbling=0; if(bubbling>100)bubbling=100;
    if(height<1)height=1; if(height>100)height=100;

    int delta=tick_ms-prev_tick; if(delta<=0||delta>200)delta=33; prev_tick=tick_ms;
    g_dt=(float)delta/1000.0f;
    rng^=(uint32_t)tick_ms;

    phase+=(uint32_t)(delta*speed);
    palpos+=(float)cyc*0.12f*g_dt;                  /* palette units per second */
    while(palpos>=256.0f)palpos-=256.0f;

    g_bright=bright;
    g_bubbling=(float)bubbling*0.01f;
    g_height=(float)height*0.01f;
    g_shph=phase/12;

    if(pal!=cur_pal) build_palette(pal);
    build_surface(level,chop);
    build_depth_lut();
    render_liquid();
    spawn_stage();
    step_bubbles();
    draw_bubbles();
    step_sparks();
    draw_sparks();
    draw();
}
