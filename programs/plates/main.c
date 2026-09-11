#include "api.h"

/*
 * Plates — two white parallel plates, one above and one below, with a stream
 * of rainbow pixels flying between them. Each pixel leaves a plate along its
 * normal (perpendicular to the tilted surface) and flies straight. The plates
 * breathe (the gap between them shrinks and grows) and slowly change their
 * orientation while staying parallel: a tilted plane cutting the lamp's
 * cylinder unrolls to a sine wave, so both plates share one sine (amplitude =
 * tilt, phase = azimuth) and only differ by their offset. A particle that
 * lands on a plate leaves a short coloured splash that fades. Y=0 is the
 * bottom, X wraps around the cylinder.
 */

static const char META[] =
    "{\"name\":\"Plates\","
    "\"desc\":\"Two white parallel plates with rainbow pixels flying between them; the plates breathe and tilt\","
    "\"params\":["
        "{\"id\":0,\"name\":\"Speed\",\"type\":\"int\",\"min\":1,\"max\":100,\"default\":40,\"desc\":\"Particle flight speed\"},"
        "{\"id\":1,\"name\":\"Particles\",\"type\":\"int\",\"min\":1,\"max\":64,\"default\":16,\"desc\":\"How many particles fly at once\"},"
        "{\"id\":2,\"name\":\"Direction\",\"type\":\"select\",\"options\":[\"Both ways\",\"Upward\",\"Downward\"],\"default\":0,\"desc\":\"Which way the particles fly\"},"
        "{\"id\":3,\"name\":\"Thickness\",\"type\":\"int\",\"min\":1,\"max\":8,\"default\":2,\"desc\":\"Plate thickness in pixels\"},"
        "{\"id\":4,\"name\":\"Motion\",\"type\":\"int\",\"min\":0,\"max\":100,\"default\":35,\"desc\":\"How fast the plates breathe and turn (0 = still)\"},"
        "{\"id\":5,\"name\":\"Tilt\",\"type\":\"int\",\"min\":0,\"max\":100,\"default\":50,\"desc\":\"How far the plates may tilt\"},"
        "{\"id\":6,\"name\":\"Trail\",\"type\":\"int\",\"min\":0,\"max\":100,\"default\":30,\"desc\":\"Length of the particle trails\"},"
        "{\"id\":7,\"name\":\"Brightness\",\"type\":\"int\",\"min\":1,\"max\":255,\"default\":200,\"desc\":\"Overall brightness\"},"
        "{\"id\":8,\"name\":\"Plate Glow\",\"type\":\"int\",\"min\":0,\"max\":100,\"default\":60,\"desc\":\"Brightness of the white plates relative to the particles\"},"
        "{\"id\":9,\"name\":\"Squeeze\",\"type\":\"int\",\"min\":0,\"max\":100,\"default\":60,\"desc\":\"Particles speed up as the plates close in (0 = constant speed)\"}"
    "]}";

EXPORT(get_meta_ptr) int get_meta_ptr(void){ return (int)META; }
EXPORT(get_meta_len) int get_meta_len(void){ return sizeof(META)-1; }

#define MAX_W 64
#define MAX_H 64
#define MAX_P 64
#define MAX_SPLASH 32
#define MAX_SLOPE 0.55f     /* max |dx/dy| of a launch: keeps pixels heading across the gap */

/* wasm3 on the ESP32 chains every opcode on the native stack until a wasm
 * function returns, so frame stages stay real functions (no inlining). */
#define NOINLINE __attribute__((noinline))

static uint8_t FB[MAX_W*MAX_H*3];      /* composed frame: plates + particle layer */
static uint8_t PART[MAX_W*MAX_H*3];    /* particle layer with fading trails */
EXPORT(get_framebuffer) int get_framebuffer(void){ return (int)FB; }

static int W=32,H=48;
static int prev_tick=0;
static float tsec=0.0f;                /* animation time for the plates (scaled by Motion) */
static float hue_base=0.0f;            /* slow global hue drift, 0..256 */

/* per-column plate positions (centre line of each plate) */
static float ytop[MAX_W], ybot[MAX_W];
static float g_half_th=1.0f;           /* half thickness in px */

/* per-frame shared values */
static float g_dt=0.033f;
static int   g_bright=200, g_glow=60;

/* ---- PRNG ---- */
static uint32_t rng=0xA5D3F00Du;
static uint32_t rnd(void){ uint32_t x=rng; x^=x<<13; x^=x>>17; x^=x<<5; rng=x; return x; }
static float frand(void){ return (float)(rnd()&0xFFFF)/65536.0f; }

static inline float wrapx(float x){ while(x<0.0f)x+=(float)W; while(x>=(float)W)x-=(float)W; return x; }
static inline int colof(float x){ int c=(int)x; if(c<0)c=0; if(c>=W)c=W-1; return c; }

/* ---- particles ---- */
static float px[MAX_P], py[MAX_P], pvx[MAX_P], pvy[MAX_P];   /* position, velocity (px/s) */
static float g_amp=0.0f, g_azim=0.0f;                       /* current plate tilt: sine amplitude & phase */
static float g_gap=1.0f, g_gapmax=1.0f;                     /* current / widest half gap (px) */
static float g_boost=1.0f;                                  /* speed factor from the squeeze */
static float g_sq=0.0f;                                     /* 0..1 how hard the plates are squeezing */
static float ppx[MAX_P], ppy[MAX_P];                        /* previous position (for motion streaks) */
static int   phue[MAX_P];
static uint8_t palive[MAX_P];
static int   nalive=0;

/* ---- splashes where a particle hit a plate ---- */
static float sx[MAX_SPLASH], sy[MAX_SPLASH], slife[MAX_SPLASH];
static int   shue[MAX_SPLASH];
static uint8_t salive[MAX_SPLASH];

static NOINLINE void add_splash(float x,float y,int hue){
    for(int i=0;i<MAX_SPLASH;i++){
        if(salive[i]) continue;
        salive[i]=1; sx[i]=x; sy[i]=y; shue[i]=hue; slife[i]=1.0f;
        return;
    }
}

/* ---- 1. plate geometry for this frame ---- */
static NOINLINE void build_plates(int tilt,int th){
    float hh=(float)H*0.5f;
    g_half_th=(float)th*0.5f;
    /* tilt amplitude (px) breathes between 0 and the allowed maximum */
    float amax=(hh-g_half_th-1.0f)*0.6f*(float)tilt*0.01f;
    float amp=amax*(0.5f+0.5f*m_sin(tsec*0.37f+1.3f));
    float azim=tsec*0.53f;                                   /* the plates turn around the axis */
    /* half gap: from a few px up to what still fits with the tilt */
    float gmin=g_half_th+1.5f;
    float gmax=hh-g_half_th-amp-0.5f;
    if(gmax<gmin) gmax=gmin;
    float gap=gmin+(gmax-gmin)*(0.5f+0.5f*m_sin(tsec*0.61f));
    /* the pair drifts up and down a little inside the remaining room */
    float room=hh-g_half_th-amp-gap; if(room<0.0f) room=0.0f;
    float cy=hh-0.5f+room*0.8f*m_sin(tsec*0.23f+2.1f);
    g_amp=amp; g_azim=azim;
    g_gap=gap; g_gapmax=hh-g_half_th-0.5f; if(g_gapmax<gmin) g_gapmax=gmin;
    for(int x=0;x<W;x++){
        float s=amp*m_sin((float)x*(6.2831853f/(float)W)+azim);
        ytop[x]=cy+gap+s;
        ybot[x]=cy-gap+s;
    }
}

/* ---- 2. compose: white plates (anti-aliased) + particle layer ---- */
static NOINLINE void draw_frame(void){
    int white=g_bright*g_glow/100;
    float ht=g_half_th+0.5f;
    for(int x=0;x<W;x++){
        float yt=ytop[x], yb=ybot[x];
        for(int y=0;y<H;y++){
            float d1=yt-(float)y; if(d1<0.0f)d1=-d1;
            float d2=yb-(float)y; if(d2<0.0f)d2=-d2;
            float d=d1<d2?d1:d2;
            float cov=ht-d; if(cov<0.0f)cov=0.0f; if(cov>1.0f)cov=1.0f;
            int pvw=(int)(cov*(float)white);
            int o=(y*W+x)*3;
            int r=pvw+PART[o], g=pvw+PART[o+1], b=pvw+PART[o+2];
            FB[o]=(uint8_t)(r>255?255:r); FB[o+1]=(uint8_t)(g>255?255:g); FB[o+2]=(uint8_t)(b>255?255:b);
        }
    }
}

/* ---- 3. spawn particles on a plate's inner face ---- */
static NOINLINE void spawn_particle(int dir,float speed){
    for(int i=0;i<MAX_P;i++){
        if(palive[i]) continue;
        int up;
        if(dir==1) up=1; else if(dir==2) up=0; else up=(rnd()&1);
        float x=frand()*(float)W;
        int c=colof(x);
        if(ytop[c]-ybot[c]<2.0f*g_half_th+2.0f) return;      /* plates too close */
        palive[i]=1; nalive++;
        px[i]=x;
        py[i]= up ? ybot[c]+g_half_th+0.5f : ytop[c]-g_half_th-0.5f;
        /* launch along the plate's normal: the plate is y = c + amp*sin(kx+azim),
         * so its slope is amp*k*cos(kx+azim) and the normal is (-slope, 1) */
        float k=6.2831853f/(float)W;
        float slope=g_amp*k*m_cos(x*k+g_azim);
        /* on a steep stretch the true normal is nearly horizontal and the pixel
         * would skim along the plate; cap the launch at ~29 deg off vertical so
         * it always heads for the other plate */
        if(slope>MAX_SLOPE) slope=MAX_SLOPE; if(slope<-MAX_SLOPE) slope=-MAX_SLOPE;
        float inv=1.0f/__builtin_sqrtf(1.0f+slope*slope);
        float v=speed*(0.75f+frand()*0.5f)*(up?1.0f:-1.0f);
        pvx[i]=-slope*inv*v;
        pvy[i]=inv*v;
        ppx[i]=px[i]; ppy[i]=py[i];
        phue[i]=((int)hue_base+(int)(frand()*96.0f))&255;
        return;
    }
}

static NOINLINE void spawn_stage(int want,int dir,float speed){
    if(nalive>=want) return;
    /* ramp in gradually instead of all at once; when the plates squeeze the
     * flights get short, so launch more often to keep the gap busy */
    float p=(float)(want-nalive)*g_dt*3.0f; if(p>1.0f)p=1.0f;
    int tries=1+(int)g_boost; if(tries>6)tries=6;
    for(int k=0;k<tries && nalive<want;k++) if(frand()<p) spawn_particle(dir,speed);
}

/* ---- 4. move particles; landing on a plate makes a splash ---- */
static NOINLINE void step_particles(void){
    float dt=g_dt;
    for(int i=0;i<MAX_P;i++){
        if(!palive[i]) continue;
        ppx[i]=px[i]; ppy[i]=py[i];
        px[i]=wrapx(px[i]+pvx[i]*dt*g_boost);
        py[i]+=pvy[i]*dt*g_boost;
        int c=colof(px[i]);
        float top=ytop[c]-g_half_th, bot=ybot[c]+g_half_th;
        if(pvy[i]>0.0f && py[i]>=top){ add_splash(px[i],top,phue[i]); palive[i]=0; nalive--; }
        else if(pvy[i]<0.0f && py[i]<=bot){ add_splash(px[i],bot,phue[i]); palive[i]=0; nalive--; }
        else if(py[i]>top+1.0f || py[i]<bot-1.0f){ palive[i]=0; nalive--; }   /* squeezed out by a moving plate */
    }
}

static inline int scale_rgb(int c,int v){
    int r=((c>>16)&255)*v/255, g=((c>>8)&255)*v/255, b=(c&255)*v/255;
    return (r<<16)|(g<<8)|b;
}

/* ---- 5. particle layer: fade the trails, splat particles and splashes.
 * The harder the plates squeeze, the hotter the pixels: they turn whiter,
 * glow onto their neighbours, and a fast step is drawn as a streak from the
 * previous position so a pixel crossing a narrow gap never vanishes. ---- */
static NOINLINE void draw_particles(void){
    float sq=g_sq;
    int sat=255-(int)(sq*170.0f);
    int sidev=(int)(sq*0.7f*(float)g_bright);
    for(int i=0;i<MAX_P;i++){
        if(!palive[i]) continue;
        int rgb=scale_rgb(m_hsv(phue[i],sat,255),g_bright);
        float fx=px[i], fy=py[i];
        float dx=fx-ppx[i], dy=fy-ppy[i];
        if(dx>(float)W*0.5f) dx-=(float)W; else if(dx<-(float)W*0.5f) dx+=(float)W;
        if(dx*dx+dy*dy>1.5f && dx*dx<(float)(W*W)*0.2f && ppx[i]+dx>=0.0f && ppx[i]+dx<(float)W)
            m_line(PART,W,H,ppx[i],ppy[i],ppx[i]+dx,ppy[i],rgb);     /* motion streak (no seam) */
        m_blend(PART,W,H,fx,fy,rgb);
        if(fx>(float)(W-1)) m_blend(PART,W,H,fx-(float)W,fy,rgb);   /* cylinder seam */
        if(sidev>0){
            int side=scale_rgb(rgb,sidev);
            m_blend(PART,W,H,wrapx(fx-1.0f),fy,side);
            m_blend(PART,W,H,wrapx(fx+1.0f),fy,side);
            m_blend(PART,W,H,fx,fy-1.0f,side);
            m_blend(PART,W,H,fx,fy+1.0f,side);
        }
    }
}

static NOINLINE void draw_splashes(void){
    float dt=g_dt;
    for(int i=0;i<MAX_SPLASH;i++){
        if(!salive[i]) continue;
        slife[i]-=dt*4.0f;
        if(slife[i]<=0.0f){ salive[i]=0; continue; }
        float a=slife[i];
        int v=(int)(a*a*(float)g_bright*(1.0f+g_sq)); if(v>255)v=255;
        int rgb=scale_rgb(m_hsv(shue[i],200-(int)(g_sq*120.0f),255),v);
        float spread=(1.0f-a)*2.5f;                     /* the splash widens as it fades */
        float fx=sx[i], fy=sy[i];
        m_blend(PART,W,H,fx,fy,rgb);
        int side=scale_rgb(rgb,140);
        m_blend(PART,W,H,wrapx(fx-spread),fy,side);
        m_blend(PART,W,H,wrapx(fx+spread),fy,side);
    }
}

static void dims(void){ W=get_width(); H=get_height();
    if(W>MAX_W)W=MAX_W; if(H>MAX_H)H=MAX_H; if(W<1)W=1; if(H<1)H=1; }

static NOINLINE void reset_all(void){
    m_fill(PART,MAX_W*MAX_H,0);
    for(int i=0;i<MAX_P;i++) palive[i]=0;
    for(int i=0;i<MAX_SPLASH;i++) salive[i]=0;
    nalive=0;
}

EXPORT(init) void init(void){
    dims();
    prev_tick=0; tsec=0.0f; hue_base=0.0f;
    reset_all();
}

EXPORT(update) void update(int tick_ms){
    int oW=W,oH=H; dims();
    if(oW!=W||oH!=H) reset_all();

    int speed=get_param_i32(0), want=get_param_i32(1), dir=get_param_i32(2);
    int th=get_param_i32(3), motion=get_param_i32(4), tilt=get_param_i32(5);
    int trail=get_param_i32(6), bright=get_param_i32(7), glow=get_param_i32(8);
    int squeeze=get_param_i32(9); if(squeeze<0)squeeze=0; if(squeeze>100)squeeze=100;
    if(speed<1)speed=1; if(want<1)want=1; if(want>MAX_P)want=MAX_P;
    if(dir<0||dir>2)dir=0; if(th<1)th=1; if(th>8)th=8;
    if(motion<0)motion=0; if(motion>100)motion=100; if(tilt<0)tilt=0; if(tilt>100)tilt=100;
    if(trail<0)trail=0; if(trail>100)trail=100;
    if(bright<1)bright=1; if(bright>255)bright=255; if(glow<0)glow=0; if(glow>100)glow=100;

    int delta=tick_ms-prev_tick; if(delta<=0||delta>200)delta=33; prev_tick=tick_ms;
    g_dt=(float)delta/1000.0f;
    rng^=(uint32_t)tick_ms;

    tsec+=g_dt*(float)motion*0.02f;                    /* Motion 50 -> 1x */
    hue_base+=g_dt*12.0f; while(hue_base>=256.0f)hue_base-=256.0f;
    g_bright=bright; g_glow=glow;

    /* particle speed in px/s, scaled to the lamp height so the flight time feels alike */
    float pspeed=(float)speed*0.5f*((float)H/48.0f+0.5f);

    build_plates(tilt,th);
    /* the closer the plates, the faster the particles: (widest gap / gap)^squeeze */
    float q=g_gapmax/g_gap;
    g_boost = squeeze>0 ? m_pow(q,(float)squeeze*0.01f) : 1.0f;
    if(g_boost>8.0f) g_boost=8.0f;
    g_sq=(q-1.5f)*0.4f; if(g_sq<0.0f)g_sq=0.0f; if(g_sq>1.0f)g_sq=1.0f;   /* glow kicks in below ~2/3 of the widest gap */

    /* trail fade: 0 = clear every frame, 100 = long trails */
    if(trail==0) m_fill(PART,W*H,0);
    else { int keep=150+trail; if(keep>246)keep=246; m_fade(PART,W*H*3,keep); }

    spawn_stage(want,dir,pspeed);
    step_particles();
    draw_particles();
    draw_splashes();
    draw_frame();
    draw();
}
