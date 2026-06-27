#include "api.h"

/*
 * Sacred Mandala - a rotating woven lattice wrapping the column. Several
 * rings stacked up the height each carry N nodes spaced around the
 * circumference; consecutive rings are rotated (twist) and woven together
 * with anti-aliased chords (m_line) plus glowing nodes (m_blend). The whole
 * net rotates and breathes in a slow pulse. Seamless: every segment is drawn
 * with ±W copies and the shorter path is chosen across the seam.
 */

static const char META[] =
    "{\"name\":\"Sacred Mandala\","
    "\"desc\":\"Rotating woven lattice of light with radial symmetry, breathing in pulse\","
    "\"params\":["
        "{\"id\":0,\"name\":\"Speed\",\"type\":\"int\",\"min\":1,\"max\":40,\"default\":40,\"desc\":\"Rotation speed\"},"
        "{\"id\":1,\"name\":\"Brightness\",\"type\":\"int\",\"min\":1,\"max\":255,\"default\":220,\"desc\":\"Overall brightness\"},"
        "{\"id\":2,\"name\":\"Palette\",\"type\":\"select\",\"options\":[\"UV Neon\",\"Rainbow\",\"Fire\",\"Ice\"],\"default\":0,\"desc\":\"Color preset\"},"
        "{\"id\":3,\"name\":\"Symmetry\",\"type\":\"int\",\"min\":3,\"max\":8,\"default\":6,\"desc\":\"Nodes per ring\"},"
        "{\"id\":4,\"name\":\"Twist\",\"type\":\"int\",\"min\":0,\"max\":10,\"default\":4,\"desc\":\"Helical twist between rings\"},"
        "{\"id\":5,\"name\":\"Direction\",\"type\":\"select\",\"options\":[\"CW\",\"CCW\"],\"default\":0,\"desc\":\"Rotation direction\"},"
        "{\"id\":6,\"name\":\"Palette direction\",\"type\":\"select\",\"options\":[\"Forward\",\"Reverse\"],\"default\":0,\"desc\":\"Direction the palette colors flow\"},"
        "{\"id\":7,\"name\":\"Rotation reverse\",\"type\":\"int\",\"min\":0,\"max\":600,\"default\":0,\"desc\":\"Auto-flip rotation every N tenths of a second (0 = off)\"},"
        "{\"id\":8,\"name\":\"Palette reverse\",\"type\":\"int\",\"min\":0,\"max\":600,\"default\":0,\"desc\":\"Auto-flip palette every N tenths of a second (0 = off)\"},"
        "{\"id\":9,\"name\":\"Glitch\",\"type\":\"select\",\"options\":[\"Off\",\"On\"],\"default\":0,\"desc\":\"Random vertical glitch stripes\"},"
        "{\"id\":10,\"name\":\"Glitch power\",\"type\":\"int\",\"min\":0,\"max\":100,\"default\":60,\"desc\":\"Glitch stripe intensity\"},"
        "{\"id\":11,\"name\":\"Glitch rate\",\"type\":\"int\",\"min\":1,\"max\":100,\"default\":40,\"desc\":\"How often glitch stripes fire\"}"
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
static inline int col(int idx,int shade){
    idx&=255; if(shade<0)shade=0; if(shade>255)shade=255;
    int r=LR[idx]*shade>>8, g=LG[idx]*shade>>8, b=LB[idx]*shade>>8;
    return (r<<16)|(g<<8)|b;
}

/* AA line / point with cylinder-seam wrap (m_line/m_blend don't wrap) */
static void wline(float x0,float y0,float x1,float y1,int rgb){
    float w=(float)W;
    m_line(FB,W,H,x0,   y0,x1,   y1,rgb);
    m_line(FB,W,H,x0-w,y0,x1-w,y1,rgb);
    m_line(FB,W,H,x0+w,y0,x1+w,y1,rgb);
}
static void wpt(float x,float y,int rgb){
    float w=(float)W;
    m_blend(FB,W,H,x,  y,rgb);
    m_blend(FB,W,H,x-w,y,rgb);
    m_blend(FB,W,H,x+w,y,rgb);
}
/* draw a chord taking the shorter way around the cylinder */
static void seg(float x0,float y0,float x1,float y1,int rgb){
    float dx=x1-x0;
    if(dx>(float)W*0.5f)x1-=(float)W; else if(dx<-(float)W*0.5f)x1+=(float)W;
    wline(x0,y0,x1,y1,rgb);
}

static float wrapf(float v){ int n=(int)(v/(float)W); v-=(float)n*(float)W; if(v<0)v+=(float)W; return v; }

/* hash an integer slot -> 32-bit pseudo-random (for glitch stripe timing) */
static uint32_t fhash(uint32_t s){ s^=s<<13; s*=2654435761u; s^=s>>15; s*=2246822519u; s^=s>>13; return s; }

static void dims(void){ W=get_width(); H=get_height();
    if(W>MAX_W)W=MAX_W; if(H>MAX_H)H=MAX_H; if(W<1)W=1; if(H<1)H=1; }

/* Phase accumulators so direction reversals are smooth: the velocity flips but
 * the accumulated phase stays continuous (no jump in angle / palette index).
 * g_t is a direction-independent clock for the breath pulse. */
static float g_t=0.0f, g_rot=0.0f, g_pal=0.0f;
static int   g_last_ms=0, g_have_last=0;

EXPORT(init) void init(void){ init_sin(); dims();
    g_t=g_rot=g_pal=0.0f; g_last_ms=0; g_have_last=0; }

EXPORT(update) void update(int tick_ms){
    int speed=get_param_i32(0), bright=get_param_i32(1), pal=get_param_i32(2);
    int sym=get_param_i32(3), twist=get_param_i32(4), dir=get_param_i32(5);
    int pdir=get_param_i32(6);                        /* palette flow direction */
    int rotRev=get_param_i32(7), palRev=get_param_i32(8); /* auto-reverse, tenths of a second */
    int glitch=get_param_i32(9), gpow=get_param_i32(10), grate=get_param_i32(11);
    dims();
    if(speed<1)speed=1; if(speed>40)speed=40;
    if(bright<1)bright=1; if(bright>255)bright=255;
    if(sym<3)sym=3; if(sym>8)sym=8;
    if(twist<0)twist=0; if(twist>10)twist=10;
    if(rotRev<0)rotRev=0; if(rotRev>600)rotRev=600;
    if(palRev<0)palRev=0; if(palRev>600)palRev=600;
    if(gpow<0)gpow=0; if(gpow>100)gpow=100;
    if(grate<1)grate=1; if(grate>100)grate=100;
    build_pal(pal);

    /* clear (lines are additive) */
    int total=W*H*3; for(int i=0;i<total;i++) FB[i]=0;

    /* Advance phases by the speed-scaled time delta since the last frame. */
    int dms = g_have_last ? (tick_ms - g_last_ms) : 0;
    if(dms<0)dms=0; if(dms>1000)dms=1000;            /* guard resets / long pauses */
    g_last_ms=tick_ms; g_have_last=1;
    float dt=(float)dms*(float)speed*0.0009f;

    float spin=dir? -1.0f:1.0f;                      /* base rotation direction */
    float pflow=pdir? -1.0f:1.0f;                    /* base palette direction */
    /* Periodic auto-reverse: every N tenths of a second flip the velocity sign.
     * Phase keeps accumulating, so the reversal eases through rather than jumps. */
    if(rotRev>0 && ((tick_ms/(rotRev*100)) & 1)) spin=-spin;
    if(palRev>0 && ((tick_ms/(palRev*100)) & 1)) pflow=-pflow;

    g_t   += dt;                                     /* breath clock (direction-independent) */
    g_rot += spin*dt;                                /* rotation phase */
    g_pal += pflow*dt;                               /* palette phase */

    float breath=0.65f+0.35f*fsin(g_t*0.5f);        /* slow pulse */
    int bb=(int)((float)bright*breath);

    int RINGS=H/3; if(RINGS<4)RINGS=4; if(RINGS>9)RINGS=9;
    int N=sym;

    float prev[16]; float prevY=0.0f; int have_prev=0;
    for(int k=0;k<RINGS;k++){
        float yk=((float)k+0.5f)/(float)RINGS*(float)H-0.5f;
        float rot=g_rot*0.15f + (float)k*((float)twist*0.13f);
        float cur[16];
        for(int j=0;j<N;j++) cur[j]=wrapf(((float)j/(float)N + rot)*(float)W);

        int idx=k*256/RINGS + (int)(g_pal*30.0f);

        /* weave to previous ring: struts + diagonals */
        if(have_prev){
            for(int j=0;j<N;j++){
                seg(prev[j],prevY,cur[j],yk,col(idx,bb));
                seg(prev[j],prevY,cur[(j+1)%N],yk,col(idx,(int)(bb*0.7f)));
            }
        }
        /* glowing nodes */
        for(int j=0;j<N;j++) wpt(cur[j],yk,col(idx+24,bb));

        for(int j=0;j<N;j++) prev[j]=cur[j];
        prevY=yk; have_prev=1;
    }

    /* ---- Glitch: random vertical stripes that flicker over the mandala ----
     * A fixed set of stripe "channels" each step through fast time slots; each
     * channel has its own random phase so they fire at staggered (random)
     * delays. A channel fires per slot with a probability set by Glitch rate,
     * and is lit only for the first slice of its slot -> sharp flicker. The
     * column, width, colour and brightness (× Glitch power) come from the slot
     * hash, written straight into the framebuffer as full-height bars. */
    if(glitch && gpow>0){
        float gt=(float)tick_ms*0.001f;                 /* real seconds (speed-independent) */
        float slotHz=9.0f;
        int chans = 2 + grate*8/100;                    /* up to ~10 candidate stripes */
        float prob = 0.12f + (float)grate/100.0f*0.8f;  /* per-slot fire chance */
        for(int s=0;s<chans;s++){
            uint32_t ph=fhash((uint32_t)s*2654435761u + 12345u);
            float off=(float)(ph&0xFFFFu)/65536.0f;     /* per-channel phase => random delay */
            float ts=gt*slotHz + off*8.0f;
            int slot=(int)ts; float fp=ts-(float)slot;
            uint32_t h=fhash(((uint32_t)slot*2246822519u) ^ ((uint32_t)s*40503u));
            if(((float)(h&0xFFFFu)/65536.0f) >= prob) continue;   /* didn't fire */
            float dur=0.20f + 0.45f*((float)((h>>16)&255)/255.0f);/* on-time fraction */
            if(fp>dur) continue;                          /* gone for the rest of the slot */
            int x=(int)(h%(uint32_t)W);
            int wpx=1+(int)((h>>7)%3u);                   /* 1..3 px wide */
            int n=wpx; if(x+n>W)n=W-x; if(n<=0) continue;
            int val=255*gpow/100;
            int rgb;
            if(((h>>5)&3u)==0) rgb=(val<<16)|(val<<8)|val;        /* white spike */
            else               rgb=m_hsv((int)((h>>9)&255),255,val); /* neon hue */
            for(int y=0;y<H;y++) m_fill((uint8_t*)FB+(y*W+x)*3,n,rgb);
        }
    }

    draw();
}
