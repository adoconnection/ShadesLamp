#include "api.h"

/*
 * Ascension - neon plumes of energy streaming up the full 3 m column.
 * Several soft vertical streams wander around the circumference while
 * pulses of light travel upward through them and dissipate near the top.
 * Horizontal wrap distance is folded across the seam so plumes flow around
 * the cylinder with no break. Analytic (seamless) turbulence — no noise
 * texture seam. UV / Rainbow / Fire / Ice presets.
 */

static const char META[] =
    "{\"name\":\"Ascension\","
    "\"desc\":\"Neon plumes of energy streaming up the column with rising pulses\","
    "\"params\":["
        "{\"id\":0,\"name\":\"Speed\",\"type\":\"int\",\"min\":1,\"max\":50,\"default\":8,\"desc\":\"Rise speed\"},"
        "{\"id\":1,\"name\":\"Brightness\",\"type\":\"int\",\"min\":1,\"max\":255,\"default\":220,\"desc\":\"Overall brightness\"},"
        "{\"id\":2,\"name\":\"Palette\",\"type\":\"select\",\"options\":[\"UV Neon\",\"Rainbow\",\"Fire\",\"Ice\"],\"default\":0,\"desc\":\"Color preset\"},"
        "{\"id\":3,\"name\":\"Streams\",\"type\":\"int\",\"min\":1,\"max\":8,\"default\":6,\"desc\":\"Number of energy plumes\"},"
        "{\"id\":4,\"name\":\"Turbulence\",\"type\":\"int\",\"min\":1,\"max\":10,\"default\":10,\"desc\":\"Rising pulse density\"}"
    "]}";

EXPORT(get_meta_ptr) int get_meta_ptr(void){ return (int)META; }
EXPORT(get_meta_len) int get_meta_len(void){ return sizeof(META)-1; }

#define MAX_W 64
#define MAX_H 64
static uint8_t FB[MAX_W*MAX_H*3];
EXPORT(get_framebuffer) int get_framebuffer(void){ return (int)FB; }

static int W,H;

static float SINT[256];
static void init_sin(void){ for(int i=0;i<256;i++) SINT[i]=m_sin((float)i*6.2831853f/256.0f); }
static inline float fsin(float a){ int i=(int)(a*40.7436654f+16384.5f)&255; return SINT[i]; }

static uint8_t LR[256],LG[256],LB[256];
static int pal_cached=-1;
static int pal_h(int pal,int t,int* s){
    switch(pal){
        case 0: *s=255; return (96 + t*150/255)&255;
        case 1: *s=255; return t&255;
        case 2: *s=255; return (216 + t*96/255)&255;
        default:*s=210; return (120 + t*56/255)&255;
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
    int speed=get_param_i32(0), bright=get_param_i32(1), pal=get_param_i32(2);
    int streams=get_param_i32(3), turb=get_param_i32(4);
    dims();
    if(speed<1)speed=1; if(speed>50)speed=50;
    if(bright<1)bright=1; if(bright>255)bright=255;
    if(streams<1)streams=1; if(streams>8)streams=8;
    if(turb<1)turb=1; if(turb>10)turb=10;
    build_pal(pal);

    /* Map the UI scale [1..50] onto the effective rise rate [0.2..50] with a
     * quadratic curve: linear mapping made 1->2 jump ~10x (a harsh lurch), so
     * square the normalized position — gentle, fine control at the slow end,
     * steeper ramp toward the top. Endpoints stay 0.2 (slow default) .. 50. */
    float n = (float)(speed-1) / 49.0f;            /* 0..1 */
    float espeed = 0.2f + (50.0f - 0.2f) * n * n;

    float t=(float)tick_ms*espeed*0.0012f;

    /* Plume centres drift slowly around the circumference */
    float cx[8];
    for(int p=0;p<streams;p++){
        float base=((float)p+0.5f)/(float)streams;
        float drift=0.06f*fsin(t*0.7f+(float)p*1.3f);
        cx[p]=(base+drift)*(float)W;
    }
    float sig=(float)W/(float)streams*0.5f;
    float sig2=sig*sig;
    float tf=(float)turb;
    float halfW=(float)W*0.5f;
    float invH=1.0f/(float)(H>1?H-1:1);

    for(int y=0;y<H;y++){
        float ny=(float)y*invH;
        float env=0.25f+0.75f*(1.0f-ny);          /* source bright at base, fades up */
        int   hidx=(int)(ny*150.0f)+(int)(t*25.0f); /* hue shifts with height & time */
        for(int x=0;x<W;x++){
            float shade=0.0f;
            for(int p=0;p<streams;p++){
                float dx=(float)x-cx[p];
                if(dx>halfW)dx-=(float)W; else if(dx<-halfW)dx+=(float)W;
                float fall=sig2/(sig2+dx*dx);                 /* soft plume profile */
                float pulse=0.5f+0.5f*fsin(ny*tf - t*2.0f + (float)p*1.7f); /* travels up */
                shade+=fall*pulse;
            }
            shade*=env;
            putpx(x,y,hidx,(int)(shade*(float)bright));
        }
    }
    draw();
}
