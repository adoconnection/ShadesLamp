#include "api.h"

/*
 * Glitch - a corrupted video-signal look. A selectable background layer is
 * "corrupted" by randomised digital artefacts that fire with random delays:
 *
 *   Vertical / Horizontal each pick an artefact style:
 *     - Flare  ("Засвет")  : bright stripes light up (columns / rows)
 *     - Broken ("Сломана") : the matrix is displaced — columns shoved up/down,
 *                            rows torn sideways
 *   RGB split adds chromatic channel separation on glitch beats; Scanlines
 *   darken alternate rows. Intensity scales strength, Rate scales frequency.
 *
 * Background: Off (black) / Signal (scrolling coloured noise) / Bars (rolling
 * colour bands) / Plasma.
 */

static const char META[] =
    "{\"name\":\"Glitch\","
    "\"desc\":\"Corrupted video signal: flare stripes, broken matrix, RGB split and scanlines\","
    "\"params\":["
        "{\"id\":0,\"name\":\"Speed\",\"type\":\"int\",\"min\":1,\"max\":100,\"default\":50,\"desc\":\"Background scroll + glitch tempo\"},"
        "{\"id\":1,\"name\":\"Brightness\",\"type\":\"int\",\"min\":1,\"max\":255,\"default\":210,\"desc\":\"Overall brightness\"},"
        "{\"id\":2,\"name\":\"Palette\",\"type\":\"select\",\"options\":[\"UV Neon\",\"Rainbow\",\"Fire\",\"Ice\",\"Mono\"],\"default\":0,\"desc\":\"Colour preset\"},"
        "{\"id\":3,\"name\":\"Background\",\"type\":\"select\",\"options\":[\"Off\",\"Signal\",\"Bars\",\"Plasma\"],\"default\":1,\"desc\":\"Background layer\"},"
        "{\"id\":4,\"name\":\"Intensity\",\"type\":\"int\",\"min\":0,\"max\":100,\"default\":70,\"desc\":\"Glitch strength\"},"
        "{\"id\":5,\"name\":\"Rate\",\"type\":\"int\",\"min\":1,\"max\":100,\"default\":50,\"desc\":\"How often glitches fire\"},"
        "{\"id\":6,\"name\":\"Vertical\",\"type\":\"select\",\"options\":[\"Off\",\"Flare\",\"Broken\",\"Both\"],\"default\":1,\"desc\":\"Vertical glitch: bright stripes, shifted columns, or both\"},"
        "{\"id\":7,\"name\":\"Horizontal\",\"type\":\"select\",\"options\":[\"Off\",\"Flare\",\"Broken\",\"Both\"],\"default\":2,\"desc\":\"Horizontal glitch: bright stripes, torn rows, or both\"},"
        "{\"id\":8,\"name\":\"RGB split\",\"type\":\"select\",\"options\":[\"Off\",\"On\"],\"default\":1,\"desc\":\"Colour channel separation\"},"
        "{\"id\":9,\"name\":\"Scanlines\",\"type\":\"int\",\"min\":0,\"max\":100,\"default\":30,\"desc\":\"CRT scanline darkening\"}"
    "]}";

EXPORT(get_meta_ptr) int get_meta_ptr(void){ return (int)META; }
EXPORT(get_meta_len) int get_meta_len(void){ return sizeof(META)-1; }

#define MAX_W 64
#define MAX_H 64
static uint8_t FB[MAX_W*MAX_H*3];
static uint8_t NB[MAX_W*MAX_H];     /* noise signal field */
static uint8_t TMP[MAX_W*3];        /* one-row scratch for horizontal shifts */
static uint8_t TMPC[MAX_H*3];       /* one-column scratch for vertical shifts */
EXPORT(get_framebuffer) int get_framebuffer(void){ return (int)FB; }

static int W,H;

/* ---- palette LUT (256 entries) ---- */
static uint8_t LR[256],LG[256],LB[256];
static int pal_cached=-1;
static int pal_h(int pal,int t,int* s){
    switch(pal){
        case 0: *s=255; return (150 + t*120/255)&255;   /* UV neon: cyan-blue-magenta */
        case 1: *s=255; return t&255;                    /* rainbow */
        case 2: *s=255; return (0   + t*44/255)&255;     /* fire: red-orange-yellow */
        case 3: *s=210; return (130 + t*40/255)&255;     /* ice: cyan-blue */
        default:*s=0;   return t&255;                    /* mono: grayscale */
    }
}
static void build_pal(int pal){
    if(pal==pal_cached) return; pal_cached=pal;
    for(int i=0;i<256;i++){
        int t=(pal==1)?i:(i<128?i*2:(255-i)*2);          /* triangle fold -> cyclic LUT */
        int s; int h=pal_h(pal,t,&s); int c=m_hsv(h,s,255);
        LR[i]=(c>>16)&255; LG[i]=(c>>8)&255; LB[i]=c&255; }
}
static inline int col(int idx,int shade){
    idx&=255; if(shade<0)shade=0; if(shade>255)shade=255;
    int r=LR[idx]*shade>>8, g=LG[idx]*shade>>8, b=LB[idx]*shade>>8;
    return (r<<16)|(g<<8)|b;
}
static inline void put(int x,int y,int c){ int o=(y*W+x)*3; FB[o]=(c>>16)&255; FB[o+1]=(c>>8)&255; FB[o+2]=c&255; }

/* ---- hash for staggered glitch timing ---- */
static uint32_t fhash(uint32_t s){ s^=s<<13; s*=2654435761u; s^=s>>15; s*=2246822519u; s^=s>>13; return s; }

/* sine table for the plasma background */
static float SINT[256];
static void init_sin(void){ for(int i=0;i<256;i++) SINT[i]=m_sin((float)i*6.2831853f/256.0f); }
static inline float fsin(float a){ int i=(int)(a*40.7436654f+16384.5f)&255; return SINT[i]; }

/* rotate one row's pixels horizontally by `off` (wraps around the cylinder) */
static void rotate_row(int y,int off){
    off%=W; if(off<0)off+=W; if(off==0) return;
    uint8_t* row=FB+(y*W)*3;
    for(int x=0;x<W;x++){ int sx=(x-off+W)%W; TMP[x*3]=row[sx*3]; TMP[x*3+1]=row[sx*3+1]; TMP[x*3+2]=row[sx*3+2]; }
    for(int i=0;i<W*3;i++) row[i]=TMP[i];
}
/* shift one column's pixels vertically by `off` (wraps top/bottom) */
static void rotate_col(int x,int off){
    off%=H; if(off<0)off+=H; if(off==0) return;
    for(int y=0;y<H;y++){ int sy=(y-off+H)%H; int s=(sy*W+x)*3; TMPC[y*3]=FB[s]; TMPC[y*3+1]=FB[s+1]; TMPC[y*3+2]=FB[s+2]; }
    for(int y=0;y<H;y++){ int d=(y*W+x)*3; FB[d]=TMPC[y*3]; FB[d+1]=TMPC[y*3+1]; FB[d+2]=TMPC[y*3+2]; }
}
/* chromatic split: pull the R channel from the left, B from the right (G stays) */
static void split_row(int y,int dx){
    if(dx<=0) return;
    uint8_t* row=FB+(y*W)*3;
    for(int i=0;i<W*3;i++) TMP[i]=row[i];
    for(int x=0;x<W;x++){ int rx=(x-dx+W)%W, bx=(x+dx)%W; row[x*3]=TMP[rx*3]; row[x*3+2]=TMP[bx*3+2]; }
}

static void dims(void){ W=get_width(); H=get_height();
    if(W>MAX_W)W=MAX_W; if(H>MAX_H)H=MAX_H; if(W<1)W=1; if(H<1)H=1; }

EXPORT(init) void init(void){ dims(); init_sin(); pal_cached=-1; }

EXPORT(update) void update(int tick_ms){
    int speed=get_param_i32(0), bright=get_param_i32(1), pal=get_param_i32(2), bg=get_param_i32(3);
    int inten=get_param_i32(4), rate=get_param_i32(5);
    int vmode=get_param_i32(6), hmode=get_param_i32(7), rgbsplit=get_param_i32(8), scan=get_param_i32(9);
    dims();
    if(speed<1)speed=1; if(speed>100)speed=100;
    if(bright<1)bright=1; if(bright>255)bright=255;
    if(inten<0)inten=0; if(inten>100)inten=100;
    if(rate<1)rate=1; if(rate>100)rate=100;
    if(scan<0)scan=0; if(scan>100)scan=100;
    build_pal(pal);

    float t=(float)tick_ms*0.001f;                 /* real seconds */
    int hueScroll=(int)(t*(float)speed*0.5f);

    /* ── Background layer ─────────────────────────────────────────────────── */
    if(bg==0){                                       /* Off: black canvas */
        m_fill(FB,W*H,0);
    } else if(bg==1){                                /* Signal: scrolling coloured noise */
        int toff=(tick_ms*speed)/10; int ox=toff>>1, oy=toff>>2;
        m_noise_fill(NB,W,H,70,ox,oy,2);
        for(int y=0;y<H;y++) for(int x=0;x<W;x++){
            int i=y*W+x; int n=NB[i];
            int idx=(n + hueScroll + y*4)&255;
            int sh=(int)((float)bright*(0.28f+0.72f*(float)n/255.0f));
            put(x,y,col(idx,sh));
        }
    } else if(bg==2){                                /* Bars: rolling colour bands */
        for(int y=0;y<H;y++){
            int idx=(y*256/H + hueScroll)&255;
            int sh=(int)((float)bright*(0.55f+0.45f*fsin((float)y*0.5f+t*2.0f)));
            int c=col(idx,sh);
            m_fill(FB+(y*W)*3,W,c);
        }
    } else {                                         /* Plasma */
        float ts=t*(float)speed*0.03f;
        for(int y=0;y<H;y++) for(int x=0;x<W;x++){
            float v=fsin((float)x*0.13f+ts)+fsin((float)y*0.10f-ts*0.8f)+fsin((float)(x+y)*0.08f+ts*1.3f);
            int idx=(int)((v+3.0f)*42.0f)+hueScroll;
            int sh=(int)((float)bright*(0.45f+0.55f*(0.5f+0.5f*fsin(v*1.7f+ts))));
            put(x,y,col(idx,sh));
        }
    }

    float gI=(float)inten/100.0f;
    float prob=0.10f + (float)rate/100.0f*0.80f;     /* per-slot fire chance */
    float slotHz=5.0f + (float)speed*0.06f;          /* glitch tempo */

    /* ── Vertical component: columns ──────────────────────────────────────── */
    if(vmode && inten>0){
        int chans=2 + rate*8/100;
        int val=(int)((float)bright*(0.55f+0.45f*gI));
        int maxoff=1+(int)(gI*(float)H*0.5f);
        for(int s=0;s<chans;s++){
            uint32_t ph=fhash((uint32_t)s*2654435761u+12345u);
            float off=(float)(ph&0xFFFFu)/65536.0f;
            float ts=t*(slotHz*1.6f)+off*8.0f;
            int slot=(int)ts; float fp=ts-(float)slot;
            uint32_t h=fhash(((uint32_t)slot*2246822519u) ^ ((uint32_t)s*40503u));
            if(((float)(h&0xFFFFu)/65536.0f) >= prob) continue;
            float dur=0.18f+0.42f*((float)((h>>16)&255)/255.0f);
            if(fp>dur) continue;
            int x=(int)(h%(uint32_t)W);
            int wpx=1+(int)((h>>7)%3u); if(x+wpx>W)wpx=W-x; if(wpx<=0) continue;
            /* Off=0 Flare=1 Broken=2 Both=3; Both picks one per fired channel */
            int doFlare = (vmode==1) || (vmode==3 && ((h>>3)&1u));
            if(doFlare){                             /* Flare: bright vertical stripe */
                int rgb=(((h>>5)&3u)==0)?(val<<16)|(val<<8)|val:m_hsv((int)((h>>9)&255),255,val);
                for(int y=0;y<H;y++) m_fill(FB+(y*W+x)*3,wpx,rgb);
            } else {                                 /* Broken: shove columns up/down */
                int sh=(int)((h>>16)%(uint32_t)(2*maxoff+1))-maxoff;
                for(int c=0;c<wpx;c++) rotate_col(x+c,sh);
            }
        }
    }

    /* ── Horizontal component: rows ───────────────────────────────────────── */
    if(hmode && inten>0){
        int bands=1 + rate*5/100;
        int val=(int)((float)bright*(0.55f+0.45f*gI));
        int maxoff=1+(int)(gI*(float)W*0.6f);
        for(int s=0;s<bands;s++){
            uint32_t ph=fhash((uint32_t)s*2654435761u+777u);
            float off=(float)(ph&0xFFFFu)/65536.0f;
            float ts=t*slotHz+off*8.0f;
            int slot=(int)ts; float fp=ts-(float)slot;
            uint32_t h=fhash(((uint32_t)slot*2246822519u) ^ ((uint32_t)s*2654435761u));
            if(((float)(h&0xFFFFu)/65536.0f) >= prob) continue;
            float dur=0.20f+0.50f*((float)((h>>20)&255)/255.0f);
            if(fp>dur) continue;
            int y0=(int)(h%(uint32_t)H);
            int hgt=1+(int)((h>>8)%(uint32_t)(H/3+1));
            int y1=y0+hgt; if(y1>H)y1=H;
            /* Off=0 Flare=1 Broken=2 Both=3; Both picks one per fired band */
            int doFlare = (hmode==1) || (hmode==3 && ((h>>3)&1u));
            if(doFlare){                             /* Flare: bright horizontal stripe */
                int rgb=(((h>>5)&3u)==0)?(val<<16)|(val<<8)|val:m_hsv((int)((h>>9)&255),255,val);
                for(int y=y0;y<y1;y++) m_fill(FB+(y*W)*3,W,rgb);
            } else {                                 /* Broken: tear rows sideways */
                int shift=(int)((h>>16)%(uint32_t)(2*maxoff+1))-maxoff;
                for(int y=y0;y<y1;y++) rotate_row(y,shift);
            }
        }
    }

    /* ── RGB split: chromatic aberration on glitch beats ─────────────────── */
    if(rgbsplit && inten>0){
        float ts=t*slotHz; int slot=(int)ts; float fp=ts-(float)slot;
        uint32_t h=fhash(((uint32_t)slot*40503u) ^ 0xBEEFu);
        if((((float)(h&0xFFFFu)/65536.0f) < (prob*0.8f)) && fp<0.5f){
            int dx=1+(int)(gI*(float)W*0.18f);
            for(int y=0;y<H;y++) split_row(y,dx);
        }
    }

    /* ── Scanlines: darken alternate rows ─────────────────────────────────── */
    if(scan>0){
        int keep=256 - scan*200/100;                 /* 100% → keep 56 */
        for(int y=1;y<H;y+=2) m_fade(FB+(y*W)*3, W*3, keep);
    }

    draw();
}
