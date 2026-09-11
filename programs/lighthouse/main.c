#include "api.h"

/*
 * Lighthouse — a beam of light sweeping around the lamp. The light source
 * sits on the lamp's axis and turns, so on the cylinder wall it shows as a
 * soft vertical band of light travelling around (x wraps). Several beams
 * can share the rotation (real lighthouses often have two opposite ones),
 * the edge of the beam can be sharp or soft, a faint afterglow trails behind
 * it, a "lantern" vignette keeps the light strongest around the middle and
 * an ambient haze fills the dark side. Y=0 is the bottom.
 */

static const char META[] =
    "{\"name\":\"Lighthouse\","
    "\"desc\":\"A warm beam of light sweeping around the lamp like a lighthouse lantern\","
    "\"params\":["
        "{\"id\":0,\"name\":\"Speed\",\"type\":\"int\",\"min\":1,\"max\":100,\"default\":20,\"desc\":\"How fast the beam turns\"},"
        "{\"id\":1,\"name\":\"Beams\",\"type\":\"int\",\"min\":1,\"max\":4,\"default\":2,\"desc\":\"Number of beams around the lamp\"},"
        "{\"id\":2,\"name\":\"Width\",\"type\":\"int\",\"min\":5,\"max\":180,\"default\":40,\"desc\":\"Beam width in degrees\"},"
        "{\"id\":3,\"name\":\"Softness\",\"type\":\"int\",\"min\":0,\"max\":100,\"default\":50,\"desc\":\"Sharp beam edge (0) or a soft glow (100)\"},"
        "{\"id\":4,\"name\":\"Hue\",\"type\":\"int\",\"min\":0,\"max\":255,\"default\":30,\"desc\":\"Light colour hue (30 = warm)\"},"
        "{\"id\":5,\"name\":\"Saturation\",\"type\":\"int\",\"min\":0,\"max\":255,\"default\":90,\"desc\":\"0 = pure white, 255 = full colour\"},"
        "{\"id\":6,\"name\":\"Brightness\",\"type\":\"int\",\"min\":1,\"max\":255,\"default\":220,\"desc\":\"Overall brightness\"},"
        "{\"id\":7,\"name\":\"Afterglow\",\"type\":\"int\",\"min\":0,\"max\":100,\"default\":40,\"desc\":\"How long the light lingers behind the beam\"},"
        "{\"id\":8,\"name\":\"Lantern\",\"type\":\"int\",\"min\":0,\"max\":100,\"default\":30,\"desc\":\"Dim the top and bottom so the light sits in the middle (0 = full height)\"},"
        "{\"id\":9,\"name\":\"Haze\",\"type\":\"int\",\"min\":0,\"max\":100,\"default\":10,\"desc\":\"Faint ambient light on the dark side\"},"
        "{\"id\":10,\"name\":\"Direction\",\"type\":\"select\",\"options\":[\"Clockwise\",\"Counter-clockwise\"],\"default\":0,\"desc\":\"Which way the beam turns\"}"
    "]}";

EXPORT(get_meta_ptr) int get_meta_ptr(void){ return (int)META; }
EXPORT(get_meta_len) int get_meta_len(void){ return sizeof(META)-1; }

#define MAX_W 64
#define MAX_H 64

/* wasm3 on the ESP32 chains every opcode on the native stack until a wasm
 * function returns, so frame stages stay real functions (no inlining). */
#define NOINLINE __attribute__((noinline))

static uint8_t FB[MAX_W*MAX_H*3];
EXPORT(get_framebuffer) int get_framebuffer(void){ return (int)FB; }

static int W=32,H=48;
static int prev_tick=0;
static float angle=0.0f;               /* beam angle, degrees 0..360 */
static float glow[MAX_W];              /* per-column afterglow 0..1 */
static float colv[MAX_W];              /* per-column beam intensity 0..1 (beam + afterglow) */
static float rowv[MAX_H];              /* per-row lantern vignette 0..1 */

/* ---- 1. beam intensity per column ---- */
static NOINLINE void build_columns(int beams,int width,int soft,int after,float dt){
    float period=360.0f/(float)beams;          /* angular spacing of the beams */
    float halfw=(float)width*0.5f;
    float p=0.35f+(float)soft*0.03f;           /* falloff exponent: 0.35 sharp .. 3.35 soft */
    /* afterglow decay per second: 0 -> instant, 100 -> long persistence */
    float keep = after>0 ? m_pow(0.02f+(float)after*0.0088f, dt*2.0f) : 0.0f;
    for(int x=0;x<W;x++){
        float a=(float)x*(360.0f/(float)W);    /* column angle */
        float d=a-angle;
        /* distance to the nearest beam, folded into [0, period/2] */
        while(d<0.0f) d+=period;
        while(d>=period) d-=period;
        if(d>period*0.5f) d=period-d;
        float t=d/halfw;
        float v = t<1.0f ? m_pow(1.0f-t,p) : 0.0f;
        /* soft beams get a dim wide skirt so they never look clipped */
        if(t<2.0f && soft>50) v += (1.0f-t*0.5f)*(1.0f-t*0.5f)*((float)(soft-50)*0.003f)*(1.0f-v);
        if(v>1.0f)v=1.0f;
        float g=glow[x]*keep;
        if(v>g) g=v;
        glow[x]=g;
        colv[x]=g;
    }
}

/* ---- 2. lantern vignette per row ---- */
static NOINLINE void build_rows(int lantern){
    float mid=(float)(H-1)*0.5f;
    float k=(float)lantern*0.01f;
    for(int y=0;y<H;y++){
        float t=((float)y-mid)/(mid+0.5f);     /* -1..1 */
        float f=1.0f-t*t;                      /* 1 in the middle, 0 at the ends */
        rowv[y]=1.0f-k*(1.0f-f*f);             /* lantern 0 -> flat, 100 -> strong */
    }
}

/* ---- 3. compose ---- */
static NOINLINE void render(int rgb,int haze,int bright){
    int br=(rgb>>16)&255, bg=(rgb>>8)&255, bb=rgb&255;
    float amb=(float)haze*0.0025f;             /* up to 25 % of full at haze 100 */
    for(int x=0;x<W;x++){
        float c=colv[x];
        for(int y=0;y<H;y++){
            float v=c*rowv[y];
            v=amb+(1.0f-amb)*v;
            int vi=(int)(v*(float)bright+0.5f); if(vi>255)vi=255;
            uint8_t*p=FB+(y*W+x)*3;
            p[0]=(uint8_t)(br*vi/255); p[1]=(uint8_t)(bg*vi/255); p[2]=(uint8_t)(bb*vi/255);
        }
    }
}

static void dims(void){ W=get_width(); H=get_height();
    if(W>MAX_W)W=MAX_W; if(H>MAX_H)H=MAX_H; if(W<1)W=1; if(H<1)H=1; }

EXPORT(init) void init(void){
    dims();
    prev_tick=0; angle=0.0f;
    for(int i=0;i<MAX_W;i++){ glow[i]=0.0f; colv[i]=0.0f; }
}

EXPORT(update) void update(int tick_ms){
    dims();
    int speed=get_param_i32(0), beams=get_param_i32(1), width=get_param_i32(2), soft=get_param_i32(3);
    int hue=get_param_i32(4), sat=get_param_i32(5), bright=get_param_i32(6), after=get_param_i32(7);
    int lantern=get_param_i32(8), haze=get_param_i32(9), dir=get_param_i32(10);
    if(speed<1)speed=1; if(beams<1)beams=1; if(beams>4)beams=4;
    if(width<5)width=5; if(width>180)width=180; if(soft<0)soft=0; if(soft>100)soft=100;
    if(sat<0)sat=0; if(sat>255)sat=255; if(bright<1)bright=1; if(bright>255)bright=255;
    if(after<0)after=0; if(after>100)after=100; if(lantern<0)lantern=0; if(lantern>100)lantern=100;
    if(haze<0)haze=0; if(haze>100)haze=100; if(dir<0||dir>1)dir=0;

    int delta=tick_ms-prev_tick; if(delta<=0||delta>200)delta=33; prev_tick=tick_ms;
    float dt=(float)delta/1000.0f;

    /* speed 20 -> 72 deg/s (5 s per turn) */
    float degps=(float)speed*3.6f;
    angle+= (dir==0 ? -degps : degps)*dt;
    while(angle<0.0f) angle+=360.0f;
    while(angle>=360.0f) angle-=360.0f;

    int rgb=m_hsv(hue&255,sat,255);
    build_columns(beams,width,soft,after,dt);
    build_rows(lantern);
    render(rgb,haze,bright);
    draw();
}
