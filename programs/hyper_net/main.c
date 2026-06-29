#include "api.h"

/*
 * Hyper Net - a glowing mesh that flashes on, drifts a little along the axis
 * and twists a little, then fades away; after a pause it reappears somewhere
 * new. The screen-fade speed sets how long the motion trail lingers. The net
 * is two crossing families of anti-aliased ridges drawn additively over a
 * fading framebuffer (m_fade). Seamless across the cylinder: the horizontal
 * coordinate advances by a whole number of cells over the width. Psy colour
 * presets: UV Neon / Rainbow / Fire / Ice.
 */

static const char META[] =
    "{\"name\":\"Hyper Net\","
    "\"desc\":\"A glowing mesh that flashes, drifts and twists, then fades and reappears\","
    "\"params\":["
        "{\"id\":0,\"name\":\"Fade\",\"type\":\"int\",\"min\":1,\"max\":100,\"default\":30,\"desc\":\"Screen fade speed (trail length)\"},"
        "{\"id\":1,\"name\":\"Brightness\",\"type\":\"int\",\"min\":1,\"max\":255,\"default\":220,\"desc\":\"Overall brightness\"},"
        "{\"id\":2,\"name\":\"Palette\",\"type\":\"select\",\"options\":[\"Random\",\"UV Neon\",\"Rainbow\",\"Fire\",\"Ice\"],\"default\":0,\"desc\":\"Random picks a new preset each appearance, or pick one\"},"
        "{\"id\":3,\"name\":\"Rotate\",\"type\":\"int\",\"min\":0,\"max\":100,\"default\":40,\"desc\":\"How much the grid turns while it travels\"},"
        "{\"id\":4,\"name\":\"Move\",\"type\":\"int\",\"min\":0,\"max\":100,\"default\":40,\"desc\":\"How far the grid travels (random direction)\"},"
        "{\"id\":5,\"name\":\"Appear\",\"type\":\"int\",\"min\":1,\"max\":100,\"default\":50,\"desc\":\"How long the net shows each time\"},"
        "{\"id\":6,\"name\":\"Pause\",\"type\":\"int\",\"min\":0,\"max\":100,\"default\":30,\"desc\":\"Dark pause between appearances\"},"
        "{\"id\":7,\"name\":\"Step\",\"type\":\"int\",\"min\":2,\"max\":16,\"default\":5,\"desc\":\"Grid cell size in pixels\"},"
        "{\"id\":8,\"name\":\"Enter\",\"type\":\"select\",\"options\":[\"Instant\",\"Dissolve\"],\"default\":0,\"desc\":\"Instant flash, or dissolve in over the current image\"},"
        "{\"id\":9,\"name\":\"Exit\",\"type\":\"select\",\"options\":[\"Full fade\",\"Cut\"],\"default\":0,\"desc\":\"Fully fade out before the next appearance, or cut instantly\"},"
        "{\"id\":10,\"name\":\"Fade len\",\"type\":\"int\",\"min\":1,\"max\":100,\"default\":40,\"desc\":\"Length of the dissolve-in / fade-out ramps\"},"
        "{\"id\":11,\"name\":\"Flow\",\"type\":\"select\",\"options\":[\"Jump\",\"Continue\"],\"default\":0,\"desc\":\"Jump = each net appears somewhere new; Continue = keep position, only change motion\"}"
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
static inline float fcos(float a){ int i=(int)(a*40.7436654f+16448.5f)&255; return SINT[i]; }

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
        /* fold to a triangle so banded presets stay cyclic (no hue tear) */
        int t = (pal==1) ? i : (i<128 ? i*2 : (255-i)*2);
        int s; int h=pal_h(pal,t,&s); int c=m_hsv(h,s,255);
        LR[i]=(c>>16)&255; LG[i]=(c>>8)&255; LB[i]=c&255; }
}
static inline void add_px(int x,int y,int idx,int cover){
    if(cover<=0)return; if(cover>255)cover=255; idx&=255;
    int o=(y*W+x)*3;
    int r=FB[o]  +(LR[idx]*cover>>8); FB[o]  =r>255?255:r;
    int g=FB[o+1]+(LG[idx]*cover>>8); FB[o+1]=g>255?255:g;
    int b=FB[o+2]+(LB[idx]*cover>>8); FB[o+2]=b>255?255:b;
}

static uint32_t fhash(uint32_t s){ s^=s<<13; s*=2654435761u; s^=s>>15; s*=2246822519u; s^=s>>13; return s; }

/* triangular ridge: bright near integer values of f, period 1 */
static inline float ridge(float f){
    f=f-(float)((int)f); if(f<0)f+=1.0f;
    float d=f<0.5f?f:1.0f-f;
    float c=1.0f-d/0.14f;
    return c<0.0f?0.0f:c;
}

static void dims(void){ W=get_width(); H=get_height();
    if(W>MAX_W)W=MAX_W; if(H>MAX_H)H=MAX_H; if(W<1)W=1; if(H<1)H=1; }

/* Carried grid placement, so a "Continue" appearance can pick up exactly where
 * the previous one left off instead of recentring. The slide is stored as a
 * phase in grid-cell units (wrapped to [0,1)) — NOT a screen offset — so the
 * rotation always pivots on the screen centre and never appears to speed up as
 * the grid travels. s_baseAng carries the accumulated rotation. */
static int   s_cidx=-1;
static float s_basePhU=0.0f, s_basePhV=0.0f, s_baseAng=0.0f;

static inline float wrap01(float x){ x-=(float)(int)x; if(x<0.0f)x+=1.0f; return x; }

EXPORT(init) void init(void){ init_sin(); dims();
    s_cidx=-1; s_basePhU=0.0f; s_basePhV=0.0f; s_baseAng=0.0f;
    int total=MAX_W*MAX_H*3; for(int i=0;i<total;i++) FB[i]=0; }

EXPORT(update) void update(int tick_ms){
    int fade=get_param_i32(0), bright=get_param_i32(1), palParam=get_param_i32(2);
    int rotate=get_param_i32(3), move=get_param_i32(4), appear=get_param_i32(5), pause=get_param_i32(6);
    int step=get_param_i32(7);
    int appearMode=get_param_i32(8), exitMode=get_param_i32(9), fadeLen=get_param_i32(10);
    int flow=get_param_i32(11);
    dims();
    if(fade<1)fade=1; if(fade>100)fade=100;
    if(bright<1)bright=1; if(bright>255)bright=255;
    if(rotate<0)rotate=0; if(rotate>100)rotate=100;
    if(move<0)move=0; if(move>100)move=100;
    if(appear<1)appear=1; if(appear>100)appear=100;
    if(pause<0)pause=0; if(pause>100)pause=100;
    if(step<2)step=2; if(step>16)step=16;
    if(palParam<0)palParam=0; if(palParam>4)palParam=4;
    if(appearMode<0)appearMode=0; if(appearMode>1)appearMode=1;
    if(exitMode<0)exitMode=0; if(exitMode>1)exitMode=1;
    if(fadeLen<1)fadeLen=1; if(fadeLen>100)fadeLen=100;
    if(flow<0)flow=0; if(flow>1)flow=1;

    /* Fade the whole screen — this is the trail. Higher Fade = shorter trail. */
    int keep=256-fade*2; if(keep<40)keep=40; if(keep>254)keep=254;
    m_fade(FB,W*H*3,keep);

    float t=(float)tick_ms/1000.0f;

    /* Appearance lifecycle: active window then a dark pause, repeating. */
    float onDur =0.3f+(float)appear/100.0f*9.7f;     /* seconds the net is injected: 0.3..10 */
    float pauseDur=(float)pause/100.0f*4.0f;          /* seconds of dark gap */
    float cycle=onDur+pauseDur; if(cycle<0.05f)cycle=0.05f;
    int   cidx=(int)(t/cycle);
    float lt=t-(float)cidx*cycle;                     /* time within this cycle */

    if(lt<onDur){
        float la=lt/onDur;                            /* 0..1 progress of the appearance */

        /* Each appearance picks a random axis (the grid is drawn parallel and
         * perpendicular to it), a random travel direction and a turn direction.
         * Move = how far it travels, Rotate = how much it turns while
         * travelling. (A freely rotated grid can't wrap the cylinder seam
         * perfectly, but the net only flashes briefly.) */
        uint32_t h=fhash((uint32_t)cidx);
        float axis=(float)(h&1023)/1023.0f*6.2831853f;        /* random grid axis */
        float trav=(float)((h>>10)&1023)/1023.0f*6.2831853f;  /* random travel direction */
        float rdir=((h>>20)&1)?1.0f:-1.0f;                    /* random turn direction */
        int   baseHue=(int)((h>>21)&127);

        /* palette: Random picks a new preset each appearance, else the chosen one */
        int effPal=(palParam==0)?(int)((h>>30)&3):(palParam-1);
        build_pal(effPal);

        /* Where does this appearance start? Decided once, when its cycle begins.
         *   Jump (рваная)        — recentre and take the fresh random orientation.
         *   Continue (продолжение)— inherit the offset & angle the previous
         *     appearance ended on; only the travel/turn directions (from this
         *     appearance's hash) change, so the grid flows on from where it was. */
        float inv=1.0f/(float)step;                            /* grid step (cell px) */
        if(cidx!=s_cidx){
            if(flow==1 && s_cidx>=0){
                uint32_t ph=fhash((uint32_t)s_cidx);              /* previous appearance */
                float pTrav=(float)((ph>>10)&1023)/1023.0f*6.2831853f;
                float pRdir=((ph>>20)&1)?1.0f:-1.0f;
                float pDist=(float)move/100.0f*34.0f;            /* its full travel (la=1) */
                /* carry the slide forward as a grid-cell phase (stays bounded) */
                s_basePhU = wrap01(s_basePhU + pDist*fcos(pTrav)*inv);
                s_basePhV = wrap01(s_basePhV + pDist*fsin(pTrav)*inv);
                s_baseAng+= pRdir*(float)rotate/100.0f*3.0f;
            }else{
                s_basePhU=0.0f; s_basePhV=0.0f; s_baseAng=axis;
            }
            s_cidx=cidx;
        }

        float dist=(float)move/100.0f*34.0f*la;                   /* distance travelled so far */
        float ang =s_baseAng + rdir*(float)rotate/100.0f*3.0f*la; /* turned so far */
        /* Slide expressed as a grid-cell phase, applied AFTER the centre
         * rotation — so the grid rotates about the screen centre at a constant
         * apparent speed no matter how far it has travelled. */
        float driftU=s_basePhU + dist*fcos(trav)*inv;
        float driftV=s_basePhV + dist*fsin(trav)*inv;
        float ct=fcos(ang), st=fsin(ang);
        float cx=(float)W*0.5f, cy=(float)H*0.5f;

        /* Appearance envelope — a 0..1 opacity multiplier on the injected net.
         * It is *added over* the still-fading framebuffer (never cleared), so a
         * dissolve-in is a true cross-fade onto the current image, not a draw on
         * a blank screen. Both ramps last `fl` of the on-window (Fade len).
         *   Appear: Instant = pop straight to full; Dissolve = ramp 0 -> 1.
         *   Exit:   Full fade = ramp 1 -> 0 over the tail; Cut = stay full (the
         *           old image then just decays via the trail).
         * dist/ang keep advancing throughout, so the net is still travelling as
         * it dissolves in or fades out. */
        float fl=0.04f+(float)fadeLen/100.0f*0.50f;   /* ramp length: 4%..54% */
        float env=1.0f;
        if(appearMode==1 && la<fl) env=la/fl;                       /* dissolve in 0->1 */
        if(exitMode==0 && la>1.0f-fl){ float e=(1.0f-la)/fl;        /* full fade out 1->0 */
            if(e<env)env=e; }
        if(env<0.0f)env=0.0f; if(env>1.0f)env=1.0f;
        int inten=(int)((float)bright*env);

        for(int y=0;y<H;y++){
            for(int x=0;x<W;x++){
                float px=(float)x-cx, py=(float)y-cy;         /* relative to screen centre */
                float u=( px*ct + py*st)*inv - driftU;        /* rotate about centre, then slide */
                float v=(-px*st + py*ct)*inv - driftV;
                float cv=ridge(u); float dv=ridge(v);
                if(cv<dv)cv=dv;                               /* mesh = union of both families */
                if(cv>0.0f){
                    int idx=baseHue + (int)(t*8.0f) + (int)(v*6.0f);
                    add_px(x,y,idx,(int)(cv*(float)inten));
                }
            }
        }
    }
    draw();
}
