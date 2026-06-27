#include "api.h"

/*
 * Laser Sky - a festival laser show beaming up from the base of the column
 * into the night sky. Several "shows" you can switch (like TV channels):
 *   1 Fan        - a fan of beams sweeping side-to-side, rotating around
 *   2 Sweep      - searchlights spaced around the column, swinging together
 *   3 Crisscross - left/right leaning beams crossing in an X
 *   4 Cone       - beams from the rim converging to a rotating apex overhead
 *   5 Strobe     - beams flashing in a BPM pulse at shifting angles
 * 0 Auto cycles them. Beams are anti-aliased (m_line), fade upward into the
 * sky, glow at the emitter (m_blend) and wrap seamlessly across the seam.
 * Colour presets: Green / RGB / Rainbow / Magenta-Cyan. Haze adds night-sky
 * atmosphere.
 */

static const char META[] =
    "{\"name\":\"Laser Sky\","
    "\"desc\":\"Festival laser beams shooting up into the night sky, switchable shows\","
    "\"params\":["
        "{\"id\":0,\"name\":\"Speed\",\"type\":\"int\",\"min\":1,\"max\":100,\"default\":45,\"desc\":\"Sweep / pulse speed\"},"
        "{\"id\":1,\"name\":\"Brightness\",\"type\":\"int\",\"min\":1,\"max\":255,\"default\":230,\"desc\":\"Overall brightness\"},"
        "{\"id\":2,\"name\":\"Color\",\"type\":\"select\",\"options\":[\"Auto\",\"Green\",\"UV Neon\",\"Rainbow\",\"Yellow-Red-Green\",\"Magenta-Cyan\"],\"default\":0,\"desc\":\"Auto cycles colour each show, or pick one\"},"
        "{\"id\":3,\"name\":\"Beams\",\"type\":\"int\",\"min\":2,\"max\":12,\"default\":6,\"desc\":\"Number of beams\"},"
        "{\"id\":4,\"name\":\"Show\",\"type\":\"select\",\"options\":[\"Auto\",\"Fan\",\"Sweep\",\"Crisscross\",\"Cone\",\"Strobe\"],\"default\":0,\"desc\":\"Auto cycles, or pick one\"},"
        "{\"id\":5,\"name\":\"Haze\",\"type\":\"int\",\"min\":0,\"max\":100,\"default\":35,\"desc\":\"Night-sky atmosphere\"},"
        "{\"id\":6,\"name\":\"Flashes\",\"type\":\"select\",\"options\":[\"Off\",\"White\",\"Colour\",\"Strobe\"],\"default\":0,\"desc\":\"Full-background flash on the beat\"},"
        "{\"id\":7,\"name\":\"Flash Power\",\"type\":\"int\",\"min\":0,\"max\":100,\"default\":60,\"desc\":\"Flash intensity\"},"
        "{\"id\":8,\"name\":\"Flash Area\",\"type\":\"select\",\"options\":[\"Full\",\"Half\",\"Third\",\"Half V\",\"Third V\",\"Random\"],\"default\":0,\"desc\":\"Zone the flash lights: full, horizontal/vertical half or third, or random\"},"
        "{\"id\":9,\"name\":\"Flash Rate\",\"type\":\"int\",\"min\":1,\"max\":100,\"default\":40,\"desc\":\"How often flashes fire (low = rare)\"}"
    "]}";

EXPORT(get_meta_ptr) int get_meta_ptr(void){ return (int)META; }
EXPORT(get_meta_len) int get_meta_len(void){ return sizeof(META)-1; }

#define MAX_W 64
#define MAX_H 64
static uint8_t FB[MAX_W*MAX_H*3];
EXPORT(get_framebuffer) int get_framebuffer(void){ return (int)FB; }

static int W,H;

/* ---- sine table ---- */
static float SINT[256];
static void init_sin(void){ for(int i=0;i<256;i++) SINT[i]=m_sin((float)i*6.2831853f/256.0f); }
static inline float fsin(float a){ int i=(int)(a*40.7436654f+16384.5f)&255; return SINT[i]; }
static inline float fcos(float a){ int i=(int)(a*40.7436654f+16448.5f)&255; return SINT[i]; }

/* ---- laser colour: pure, saturated hues (mode = concrete colour 0..4) ---- */
static int laser_rgb(int mode,int idx,float t){
    int hue,sat=255;
    switch(mode){
        case 1: hue=96 + (idx*37+(int)(t*15.0f))%150; break;           /* UV neon (green-cyan-blue-magenta) */
        case 2: hue=(idx*40+(int)(t*30.0f))&255; break;                /* rainbow */
        case 3: { int k=idx%3; hue=(k==0)?42:(k==1)?0:85; } break;     /* yellow-red-green */
        case 4: hue=(idx&1)?128:213; break;                            /* magenta-cyan */
        default: hue=92; break;                                        /* green */
    }
    return m_hsv(hue&255,sat,255);
}
static inline int scale_rgb(int rgb,int sh){
    if(sh<0)sh=0; if(sh>255)sh=255;
    int r=((rgb>>16)&255)*sh>>8, g=((rgb>>8)&255)*sh>>8, b=(rgb&255)*sh>>8;
    return (r<<16)|(g<<8)|b;
}

/* ---- AA primitives with cylinder-seam wrap ---- */
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

/* Draw one volumetric beam from base (bx,by) to tip (tx,ty): split into
 * segments that fade toward the sky, with a soft side-glow and a bright core.
 * The horizontal delta takes the shorter way around the cylinder. */
static void draw_beam(float bx,float by,float tx,float ty,int rgb,int inten){
    float dx=tx-bx;
    if(dx>(float)W*0.5f)tx-=(float)W; else if(dx<-(float)W*0.5f)tx+=(float)W;
    const int NSEG=4;
    for(int s=0;s<NSEG;s++){
        float f0=(float)s/(float)NSEG, f1=(float)(s+1)/(float)NSEG;
        float x0=bx+(tx-bx)*f0, y0=by+(ty-by)*f0;
        float x1=bx+(tx-bx)*f1, y1=by+(ty-by)*f1;
        int sh=(int)((float)inten*(1.0f-f0*0.78f));   /* fade upward */
        int core=scale_rgb(rgb,sh);
        int glow=scale_rgb(rgb,sh*40/100);
        wline(x0-0.7f,y0,x1-0.7f,y1,glow);            /* soft width */
        wline(x0+0.7f,y0,x1+0.7f,y1,glow);
        wline(x0,y0,x1,y1,core);                      /* bright core */
    }
    wpt(bx,by,scale_rgb(rgb,inten));                  /* emitter glow */
}

static float wrapf(float v){ int n=(int)(v/(float)W); v-=(float)n*(float)W; if(v<0)v+=(float)W; return v; }

static uint32_t rng_st=2463534242u;
static uint32_t rnd(uint32_t seed){ uint32_t x=seed*2654435761u+rng_st; x^=x<<13;x^=x>>17;x^=x<<5; return x; }

/* hash a flash slot index -> 32-bit random */
static uint32_t fhash(uint32_t s){ s^=s<<13; s*=2654435761u; s^=s>>15; s*=2246822519u; s^=s>>13; return s; }

/* pick the lit rectangle for a flash: full / half / third, horizontal bands
 * (over height) or vertical bands (around the circumference), or random. The
 * specific band is chosen from the slot hash hh, so it varies per flash. */
static void pick_region(int area,uint32_t hh,int* x0,int* x1,int* y0,int* y1){
    *x0=0; *x1=W; *y0=0; *y1=H;
    int a=area;
    if(a==5) a=1+(int)((hh>>3)%4u);          /* Random -> Half/Third/HalfV/ThirdV */
    if(a==1){                                 /* half, horizontal band */
        if((hh>>5)&1u) *y0=H/2; else *y1=H/2;
    } else if(a==2){                          /* third, horizontal band */
        int s=(int)((hh>>5)%3u); *y0=s*H/3; *y1=(s==2)?H:(s+1)*H/3;
    } else if(a==3){                          /* half, vertical band */
        if((hh>>5)&1u) *x0=W/2; else *x1=W/2;
    } else if(a==4){                          /* third, vertical band */
        int s=(int)((hh>>5)%3u); *x0=s*W/3; *x1=(s==2)?W:(s+1)*W/3;
    }
}
/* fill a rectangular zone (per-row, since vertical bands aren't contiguous) */
static void fill_region(int x0,int x1,int y0,int y1,int rgb){
    int n=x1-x0; if(n<=0) return;
    for(int y=y0;y<y1;y++) m_fill((uint8_t*)FB+(y*W+x0)*3,n,rgb);
}

static void dims(void){ W=get_width(); H=get_height();
    if(W>MAX_W)W=MAX_W; if(H>MAX_H)H=MAX_H; if(W<1)W=1; if(H<1)H=1; }

EXPORT(init) void init(void){ init_sin(); dims(); }

EXPORT(update) void update(int tick_ms){
    int speed=get_param_i32(0), bright=get_param_i32(1), cmode=get_param_i32(2);
    int beams=get_param_i32(3), show=get_param_i32(4), haze=get_param_i32(5);
    int flmode=get_param_i32(6), flpow=get_param_i32(7), flarea=get_param_i32(8), flrate=get_param_i32(9);
    dims();
    if(speed<1)speed=1; if(speed>100)speed=100;
    if(bright<1)bright=1; if(bright>255)bright=255;
    if(beams<2)beams=2; if(beams>12)beams=12;
    if(haze<0)haze=0; if(haze>100)haze=100;
    if(flpow<0)flpow=0; if(flpow>100)flpow=100;
    if(flrate<1)flrate=1; if(flrate>100)flrate=100;
    if(flarea<0)flarea=0; if(flarea>5)flarea=5;

    float t=(float)tick_ms/1000.0f;
    float sp=(float)speed*0.06f;     /* speed 100 ≈ 3× the old top speed — very dynamic */

    /* Cycle counter: a new show / colour every ~8s */
    int cyc=tick_ms/8000;

    /* Auto cycles the 5 shows; otherwise the picked one */
    int NUM=5, chan;
    if(show==0) chan=(cyc%NUM); else chan=show-1;

    /* Auto cycles the colour preset each show; otherwise the picked one */
    if(cmode==0) cmode=cyc%5; else cmode=cmode-1;

    /* Night-sky haze fills the whole background first */
    int hzR=haze*6/100, hzG=haze*10/100, hzB=haze*22/100;
    m_fill(FB,W*H,(hzR<<16)|(hzG<<8)|hzB);

    /* Random beat flashes: a fixed slot grid where each slot fires with a
     * probability set by Flash Rate (so flashes are random, denser at higher
     * rate). The lit zone is chosen per flash from the slot hash. */
    if(flmode>0 && flpow>0){
        float slotHz=5.0f;
        float ts=t*slotHz; int slot=(int)ts; float fp=ts-(float)slot;
        uint32_t hh=fhash((uint32_t)slot);
        float prob=(float)flrate/100.0f;
        int fired=((float)(hh&0xFFFFu)/65536.0f)<prob;
        if(fired){
            float env;
            if(flmode==3){ float fl=t*22.0f; env=(((int)fl)&1)?1.0f:0.0f; }  /* strobe burst */
            else { env=1.0f-fp; env*=env; }                                  /* decaying flash */
            float amp=env*(float)flpow/100.0f;
            int flR,flG,flB;
            if(flmode==2){                   /* colour: random hue per flash */
                int c=m_hsv((int)((hh>>8)&255),255,255);
                flR=(int)(((c>>16)&255)*amp); flG=(int)(((c>>8)&255)*amp); flB=(int)((c&255)*amp);
            } else {                         /* white / strobe */
                int wv=(int)(255.0f*amp); flR=flG=flB=wv;
            }
            if(flR>0||flG>0||flB>0){
                int x0,x1,y0,y1; pick_region(flarea,hh,&x0,&x1,&y0,&y1);
                int fR=hzR+flR, fG=hzG+flG, fB=hzB+flB;
                if(fR>255)fR=255; if(fG>255)fG=255; if(fB>255)fB=255;
                fill_region(x0,x1,y0,y1,(fR<<16)|(fG<<8)|fB);
            }
        }
    }

    float by=0.5f;                  /* emitter just above the base row */
    float topY=(float)H*1.15f;      /* beams exit past the top into the sky */
    float fw=(float)W;
    int N=beams;

    if(chan==0){                    /* FAN: rotating fan sweeping side-to-side */
        /* base jumps around in ragged 7-px steps instead of gliding 1 px */
        float raw=t*sp*0.35f*fw;
        float cx=wrapf((float)(((int)(raw/7.0f))*7));
        float sweep=fw*0.16f*fsin(t*sp*1.3f);
        float step=fw*0.5f/(float)N;
        for(int j=0;j<N;j++){
            float off=((float)j-(float)(N-1)*0.5f)*step + sweep;
            int rgb=laser_rgb(cmode,j,t);
            draw_beam(cx,by,wrapf(cx+off),topY,rgb,bright);
        }
    } else if(chan==1){             /* SWEEP: searchlights around the rim swinging */
        float swing=fw*0.22f;
        for(int j=0;j<N;j++){
            float base=(float)j/(float)N*fw;
            float off=swing*fsin(t*sp+(float)j*0.9f);
            int rgb=laser_rgb(cmode,j,t);
            draw_beam(base,by,wrapf(base+off),topY,rgb,bright);
        }
    } else if(chan==2){             /* CRISSCROSS: alternating leans crossing */
        float lean=fw*0.30f, sway=fw*0.12f*fsin(t*sp*1.5f);
        for(int j=0;j<N;j++){
            float base=(float)j/(float)N*fw;
            float dir=(j&1)?1.0f:-1.0f;
            int rgb=laser_rgb(cmode,j,t);
            draw_beam(base,by,wrapf(base+dir*lean+sway),topY,rgb,bright);
        }
    } else if(chan==3){             /* CONE: rim beams converge to a rotating apex */
        float apex=wrapf(t*sp*0.4f*fw);
        for(int j=0;j<N;j++){
            float base=(float)j/(float)N*fw;
            int rgb=laser_rgb(cmode,j,t);
            draw_beam(base,by,apex,topY,rgb,bright);
        }
    } else {                        /* STROBE: BPM flashes at shifting angles */
        float beatHz=1.5f+sp*3.0f;
        float bp=t*beatHz;
        int beat=(int)bp;
        float decay=1.0f-(bp-(float)beat);          /* bright on the beat, decays */
        int inten=(int)((float)bright*(0.25f+0.75f*decay*decay));
        for(int j=0;j<N;j++){
            uint32_t h=rnd((uint32_t)(beat*131+j*977));
            float base=(float)(h%1000u)/1000.0f*fw;
            float off=((float)((h>>10)%1000u)/1000.0f-0.5f)*fw*0.5f;
            int rgb=laser_rgb(cmode,(int)(h>>20)&7,t);
            draw_beam(base,by,wrapf(base+off),topY,rgb,inten);
        }
    }
    draw();
}
