#include "api.h"

/*
 * Tentacles - glowing tentacles rooted at the base of the column, writhing
 * their way up toward the sky. Each is an anti-aliased polyline (m_line) that
 * sways by a sum of sines (organic writhe), thick and bright at the root,
 * tapering to a glowing tip (m_blend). Count is a parameter; colour follows
 * the same psy presets as the other column effects (UV / Rainbow / Fire /
 * Ice). Seamless across the cylinder seam via ±W copies.
 */

static const char META[] =
    "{\"name\":\"Tentacles\","
    "\"desc\":\"Glowing tentacles writhing up the column from base to sky\","
    "\"params\":["
        "{\"id\":0,\"name\":\"Speed\",\"type\":\"int\",\"min\":1,\"max\":100,\"default\":45,\"desc\":\"Writhe speed\"},"
        "{\"id\":1,\"name\":\"Brightness\",\"type\":\"int\",\"min\":1,\"max\":255,\"default\":220,\"desc\":\"Overall brightness\"},"
        "{\"id\":2,\"name\":\"Palette\",\"type\":\"select\",\"options\":[\"UV Neon\",\"Rainbow\",\"Fire\",\"Ice\"],\"default\":0,\"desc\":\"Color preset\"},"
        "{\"id\":3,\"name\":\"Count\",\"type\":\"int\",\"min\":2,\"max\":12,\"default\":6,\"desc\":\"Number of tentacles\"},"
        "{\"id\":4,\"name\":\"Writhe\",\"type\":\"int\",\"min\":1,\"max\":10,\"default\":5,\"desc\":\"Sway amplitude\"}"
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
        /* fold the index to a triangle so banded presets stay cyclic (no tear) */
        int t = (pal==1) ? i : (i<128 ? i*2 : (255-i)*2);
        int s; int h=pal_h(pal,t,&s); int c=m_hsv(h,s,255);
        LR[i]=(c>>16)&255; LG[i]=(c>>8)&255; LB[i]=c&255; }
}
static inline int col(int idx,int shade){
    idx&=255; if(shade<0)shade=0; if(shade>255)shade=255;
    int r=LR[idx]*shade>>8, g=LG[idx]*shade>>8, b=LB[idx]*shade>>8;
    return (r<<16)|(g<<8)|b;
}

/* one AA segment (core + optional side glow), no wrap */
static void draw1(float x0,float y0,float x1,float y1,int core,int glow,int wide){
    if(wide){
        m_line(FB,W,H,x0-0.7f,y0,x1-0.7f,y1,glow);
        m_line(FB,W,H,x0+0.7f,y0,x1+0.7f,y1,glow);
    }
    m_line(FB,W,H,x0,y0,x1,y1,core);
}
/* segment with seam copies only when it nears an edge */
static void seg(float x0,float y0,float x1,float y1,int core,int glow,int wide){
    draw1(x0,y0,x1,y1,core,glow,wide);
    float mn=x0<x1?x0:x1, mx=x0>x1?x0:x1;
    if(mn<2.0f)            draw1(x0+(float)W,y0,x1+(float)W,y1,core,glow,wide);
    if(mx>(float)W-2.0f)   draw1(x0-(float)W,y0,x1-(float)W,y1,core,glow,wide);
}
static void wpt(float x,float y,int rgb){
    m_blend(FB,W,H,x,y,rgb);
    if(x<2.0f) m_blend(FB,W,H,x+(float)W,y,rgb);
    if(x>(float)W-2.0f) m_blend(FB,W,H,x-(float)W,y,rgb);
}

static void dims(void){ W=get_width(); H=get_height();
    if(W>MAX_W)W=MAX_W; if(H>MAX_H)H=MAX_H; if(W<1)W=1; if(H<1)H=1; }

EXPORT(init) void init(void){ init_sin(); dims(); }

EXPORT(update) void update(int tick_ms){
    int speed=get_param_i32(0), bright=get_param_i32(1), pal=get_param_i32(2);
    int count=get_param_i32(3), writhe=get_param_i32(4);
    dims();
    if(speed<1)speed=1; if(speed>100)speed=100;
    if(bright<1)bright=1; if(bright>255)bright=255;
    if(count<2)count=2; if(count>12)count=12;
    if(writhe<1)writhe=1; if(writhe>10)writhe=10;
    build_pal(pal);

    /* clear (lines are additive) */
    int total=W*H*3; for(int i=0;i<total;i++) FB[i]=0;

    float t=(float)tick_ms/1000.0f;
    float sp=(float)speed*0.025f;
    float fw=(float)W;
    float fh=(float)(H>1?H-1:1);
    float maxAmp=fw*0.06f*(float)writhe;     /* tip sway */
    int N=count;

    for(int i=0;i<N;i++){
        float baseX=((float)i+0.5f)/(float)N*fw;
        float phase=(float)i*1.7f;
        int hueOff=i*256/N;

        float px=baseX, py=0.0f;
        for(int y=1;y<H;y++){
            float ny=(float)y/fh;                 /* 0 at base .. 1 at tip */
            float amp=maxAmp*ny;                  /* anchored at root, free at tip */
            float sway = fsin(ny*3.0f - t*sp + phase)
                       + 0.4f*fsin(ny*6.0f + t*sp*0.7f + phase*1.3f);
            float cx=baseX + amp*sway;

            int idx = (int)(ny*120.0f) + hueOff + (int)(t*20.0f);
            int sh  = (int)((float)bright*(0.55f+0.45f*(1.0f-ny)));  /* tip a touch dimmer */
            int wide = (ny<0.65f);                /* thick near root, thin near tip */
            seg(px,py,cx,(float)y, col(idx,sh), col(idx,sh*35/100), wide);

            px=cx; py=(float)y;
        }
        /* glowing tip */
        int tipIdx=(int)(120.0f)+hueOff+(int)(t*20.0f);
        wpt(px,py,col(tipIdx,bright));
    }
    draw();
}
