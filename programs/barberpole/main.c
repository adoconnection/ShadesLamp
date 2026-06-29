#include "api.h"

/*
 * Barberpole Spiral - helical ribbons of light wrapping the column and
 * climbing upward. Because x is periodic (full 360° wrap) the helix is
 * inherently seamless: across the width the diagonal phase advances by an
 * integer number of turns. Reads from far away; very "totem". Smooth
 * sub-pixel ribbons come from a continuous sine profile (no hard stamps).
 */

static const char META[] =
    "{\"name\":\"Barberpole Spiral\","
    "\"desc\":\"Helical ribbons of light spiralling up around the column\","
    "\"params\":["
        "{\"id\":0,\"name\":\"Speed\",\"type\":\"int\",\"min\":1,\"max\":50,\"default\":10,\"desc\":\"Climb speed\"},"
        "{\"id\":1,\"name\":\"Brightness\",\"type\":\"int\",\"min\":1,\"max\":255,\"default\":220,\"desc\":\"Overall brightness\"},"
        "{\"id\":2,\"name\":\"Palette\",\"type\":\"select\",\"options\":[\"UV Neon\",\"Rainbow\",\"Fire\",\"Ice\"],\"default\":1,\"desc\":\"Color preset\"},"
        "{\"id\":3,\"name\":\"Arms\",\"type\":\"int\",\"min\":1,\"max\":6,\"default\":2,\"desc\":\"Number of helix starts\"},"
        "{\"id\":4,\"name\":\"Pitch\",\"type\":\"int\",\"min\":1,\"max\":10,\"default\":3,\"desc\":\"Vertical tightness\"},"
        "{\"id\":5,\"name\":\"Direction\",\"type\":\"select\",\"options\":[\"Up\",\"Down\"],\"default\":0,\"desc\":\"Climb direction\"}"
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
    int arms=get_param_i32(3), pitch=get_param_i32(4), dir=get_param_i32(5);
    dims();
    if(speed<1)speed=1; if(speed>50)speed=50;
    if(bright<1)bright=1; if(bright>255)bright=255;
    if(arms<1)arms=1; if(arms>6)arms=6;
    if(pitch<1)pitch=1; if(pitch>10)pitch=10;
    build_pal(pal);

    /* Map the UI scale [1..50] onto the effective climb rate [0.2..22] with a
     * quadratic curve: linear mapping gave a harsh ~10x lurch from 1->2, so
     * square the normalized position — fine control when slow, steeper at top.
     * The top (22) is deliberately below the 30 FPS wagon-wheel limit: the
     * ribbon scrolls one full period per band unit, so once the per-frame
     * advance (33ms * espeed * 0.00055) passes 0.5 the spiral appears to spin
     * backwards. 22 keeps it at ~0.40 — fast but never reversing. */
    float n = (float)(speed-1) / 49.0f;            /* 0..1 */
    float espeed = 0.2f + (22.0f - 0.2f) * n * n;

    /* Direction flips the scroll: ribbons climb up (0) or sink down (1) */
    float t=(float)tick_ms*espeed*0.00055f;
    if(!dir) t=-t;
    float pf=(float)pitch;
    float af=(float)arms;
    float invW=1.0f/(float)W;
    float invH=1.0f/(float)(H>1?H-1:1);

    for(int y=0;y<H;y++){
        float ny=(float)y*invH;
        float vbase=pf*ny + t;          /* vertical + time component */
        for(int x=0;x<W;x++){
            /* diagonal helix phase; seamless: af*x*invW gains an integer
             * (= arms) over the full width, hue gains arms*256 ≡ 0 mod 256 */
            float band=af*(float)x*invW + vbase;
            float s=0.5f+0.5f*fsin(band*6.2831853f);
            s*=s;                       /* tighten ribbons */
            int idx=(int)(band*256.0f); /* hue rotates along the ribbon */
            putpx(x,y,idx,(int)(s*(float)bright));
        }
    }
    draw();
}
