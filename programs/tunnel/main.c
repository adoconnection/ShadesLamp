#include "api.h"

/*
 * Hyperspace Tunnel - infinite psy tunnel for a 360° cylinder.
 * x maps to the angle around the tunnel (seamless by construction),
 * y is depth: rings rush upward toward the viewer, twisting into a
 * spiral. Field is written straight into the RGB framebuffer through a
 * per-frame palette LUT (UV / Rainbow / Fire / Ice presets) and a fixed
 * sine table, so no per-pixel host math calls.
 */

static const char META[] =
    "{\"name\":\"Hyperspace Tunnel\","
    "\"desc\":\"Infinite twisting tunnel of light rings rushing upward\","
    "\"params\":["
        "{\"id\":0,\"name\":\"Speed\",\"type\":\"int\",\"min\":1,\"max\":20,\"default\":12,\"desc\":\"Rush speed\"},"
        "{\"id\":1,\"name\":\"Brightness\",\"type\":\"int\",\"min\":1,\"max\":255,\"default\":220,\"desc\":\"Overall brightness\"},"
        "{\"id\":2,\"name\":\"Palette\",\"type\":\"select\",\"options\":[\"UV Neon\",\"Rainbow\",\"Fire\",\"Ice\"],\"default\":0,\"desc\":\"Color preset\"},"
        "{\"id\":3,\"name\":\"Twist\",\"type\":\"int\",\"min\":0,\"max\":8,\"default\":3,\"desc\":\"Spiral arms / twist\"}"
    "]}";

EXPORT(get_meta_ptr) int get_meta_ptr(void){ return (int)META; }
EXPORT(get_meta_len) int get_meta_len(void){ return sizeof(META)-1; }

#define MAX_W 64
#define MAX_H 64
static uint8_t FB[MAX_W*MAX_H*3];
EXPORT(get_framebuffer) int get_framebuffer(void){ return (int)FB; }

static int W,H;

/* ---- fixed sine table (no per-pixel host calls) ---- */
static float SINT[256];
static void init_sin(void){ for(int i=0;i<256;i++) SINT[i]=m_sin((float)i*6.2831853f/256.0f); }
static inline float fsin(float a){ int i=(int)(a*40.7436654f+16384.5f)&255; return SINT[i]; }

/* ---- palette LUT (rebuilt only when the preset changes) ---- */
static uint8_t LR[256],LG[256],LB[256];
static int pal_cached=-1;
static int pal_h(int pal,int t,int* s){
    switch(pal){
        case 0: *s=255; return (96 + t*150/255)&255;   /* UV: green-cyan-blue-magenta */
        case 1: *s=255; return t&255;                  /* rainbow */
        case 2: *s=255; return (216 + t*96/255)&255;   /* fire: magenta-red-orange-yellow */
        default:*s=210; return (120 + t*56/255)&255;   /* ice: cyan-blue */
    }
}
static void build_pal(int pal){
    if(pal==pal_cached) return; pal_cached=pal;
    for(int i=0;i<256;i++){
        /* Rainbow is a full cyclic hue sweep; the banded presets only cover a
         * slice of the spectrum, so fold the index into a triangle (0..254..0)
         * to keep the LUT cyclic — no hard jump where the index wraps. */
        int t = (pal==1) ? i : (i<128 ? i*2 : (255-i)*2);
        int s; int h=pal_h(pal,t,&s); int c=m_hsv(h,s,255);
        LR[i]=(c>>16)&255; LG[i]=(c>>8)&255; LB[i]=c&255; }
}
static inline void putpx(int x,int y,int idx,int shade){
    if(shade<0)shade=0; if(shade>255)shade=255; idx&=255;
    int o=(y*W+x)*3;
    FB[o]  =(uint8_t)(LR[idx]*shade>>8);
    FB[o+1]=(uint8_t)(LG[idx]*shade>>8);
    FB[o+2]=(uint8_t)(LB[idx]*shade>>8);
}

static void dims(void){ W=get_width(); H=get_height();
    if(W>MAX_W)W=MAX_W; if(H>MAX_H)H=MAX_H; if(W<1)W=1; if(H<1)H=1; }

EXPORT(init) void init(void){ init_sin(); dims(); }

EXPORT(update) void update(int tick_ms){
    int speed=get_param_i32(0), bright=get_param_i32(1), pal=get_param_i32(2), twist=get_param_i32(3);
    dims();
    if(speed<1)speed=1; if(speed>20)speed=20;
    if(bright<1)bright=1; if(bright>255)bright=255;
    if(twist<0)twist=0; if(twist>8)twist=8;
    build_pal(pal);

    float t=(float)tick_ms*(float)speed*0.0008f;

    /* Per-column angle/color phases. Seamless: both advance by an integer
     * number of cycles across the full width, so x=W matches x=0. */
    float axph[MAX_W]; int axcol[MAX_W];
    for(int x=0;x<W;x++){
        float fx=(float)x/(float)W;
        axph[x]  = (float)twist*fx*6.2831853f;             /* radians */
        axcol[x] = (int)(fx*256.0f*(float)(twist+1));      /* hue swirl */
    }

    for(int y=0;y<H;y++){
        float ny=(float)y/(float)(H>1?H-1:1);
        float far=1.0f-ny;                       /* top = far, bottom = near */
        float z   = t + far*far*6.0f;            /* rings compress with distance */
        float persp = 0.20f + 0.80f*(1.0f-far);  /* near is brighter */
        float zf  = z*3.0f;
        int   zcol=(int)(z*40.0f);
        for(int x=0;x<W;x++){
            float ring=fsin(zf+axph[x]);         /* -1..1 */
            float shade=0.5f+0.5f*ring; shade*=shade;   /* sharpen rings */
            shade*=persp;
            int v=(int)(shade*(float)bright);
            putpx(x,y,zcol+axcol[x],v);
        }
    }
    draw();
}
