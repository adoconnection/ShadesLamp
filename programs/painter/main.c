#include "api.h"

/*
 * Painter — white brushes roam the display and leave a coloured trail behind.
 * The paint colour walks along a palette as the brush travels (rainbow by
 * default: starts red, then orange, yellow...). Old paint stays put and the
 * whole canvas slowly fades. When a brush crosses old paint the Blend mode
 * decides what happens: Replace (fresh paint covers), Add (light mixing,
 * saturates to white), Mix (average, like wet paint), CMYK (subtractive, like
 * real pigments: red over green -> dark). Motion styles: smooth Wander,
 * straight Bounce, Lissajous figures, Spiral around a drifting centre, and a
 * frantic Scribble. X wraps around the cylinder, Y bounces. Y=0 is the bottom.
 */

static const char META[] =
    "{\"name\":\"Painter\","
    "\"desc\":\"White brushes roam the screen leaving rainbow paint that slowly fades\","
    "\"params\":["
        "{\"id\":0,\"name\":\"Speed\",\"type\":\"int\",\"min\":1,\"max\":100,\"default\":40,\"desc\":\"Brush travel speed\"},"
        "{\"id\":1,\"name\":\"Brushes\",\"type\":\"int\",\"min\":1,\"max\":8,\"default\":2,\"desc\":\"Number of brushes\"},"
        "{\"id\":2,\"name\":\"Size\",\"type\":\"int\",\"min\":1,\"max\":10,\"default\":3,\"desc\":\"Brush radius\"},"
        "{\"id\":3,\"name\":\"Palette\",\"type\":\"select\",\"options\":[\"Rainbow\",\"UV Neon\",\"Fire\",\"Ice\",\"Magenta-Cyan\",\"Yellow-Red-Green\",\"Pastel\"],\"default\":0,\"desc\":\"Paint colours the brush walks through\"},"
        "{\"id\":4,\"name\":\"Blend\",\"type\":\"select\",\"options\":[\"Replace\",\"Add\",\"Mix\",\"CMYK\"],\"default\":0,\"desc\":\"What happens when a brush crosses old paint\"},"
        "{\"id\":5,\"name\":\"Fade\",\"type\":\"int\",\"min\":0,\"max\":100,\"default\":15,\"desc\":\"How fast the canvas fades to black (0 = never)\"},"
        "{\"id\":6,\"name\":\"Motion\",\"type\":\"select\",\"options\":[\"Wander\",\"Bounce\",\"Lissajous\",\"Spiral\",\"Scribble\"],\"default\":0,\"desc\":\"How the brushes move\"},"
        "{\"id\":7,\"name\":\"Colour Change\",\"type\":\"int\",\"min\":1,\"max\":100,\"default\":30,\"desc\":\"How fast the paint colour shifts along the stroke\"}"
    "]}";

EXPORT(get_meta_ptr) int get_meta_ptr(void){ return (int)META; }
EXPORT(get_meta_len) int get_meta_len(void){ return sizeof(META)-1; }

#define MAX_W 64
#define MAX_H 64
#define MAX_B 8

/* wasm3 on the ESP32 chains every opcode on the native stack until a wasm
 * function returns, so big helpers must stay real functions (no inlining). */
#define NOINLINE __attribute__((noinline))

static uint8_t FB[MAX_W*MAX_H*3];      /* what the host displays: paint + white heads */
static uint8_t PAINT[MAX_W*MAX_H*3];   /* persistent canvas (trail) */
EXPORT(get_framebuffer) int get_framebuffer(void){ return (int)FB; }

static int W=32,H=48;

/* ---- PRNG ---- */
static uint32_t rng=0x9E3779B9u;
static uint32_t rnd(void){ uint32_t x=rng; x^=x<<13; x^=x>>17; x^=x<<5; rng=x; return x; }
static float frand(void){ return (float)(rnd()&0xFFFF)/65536.0f; }

/* ---- palette LUT: 256 packed 0xRRGGBB ---- */
static int PAL[256];
static int cur_pal=-1;
static inline int tri(int i){ return i<128 ? i*2 : (255-i)*2; }   /* 0..254..0, seamless */
static NOINLINE void build_palette(int p){
    for(int i=0;i<256;i++){
        int t=tri(i), h, s=255, v=255;
        switch(p){
            case 1: h=96 + t*150/254; break;                 /* UV neon: green-cyan-blue-magenta */
            case 2: h=t*42/254; break;                       /* fire: red-orange-yellow */
            case 3: h=128 + t*42/254; s=90 + t*165/254; break; /* ice: pale cyan - deep blue */
            case 4: h=128 + t*85/254; break;                 /* magenta-cyan through blue */
            case 5: h=t*85/254; break;                       /* green-yellow-red */
            case 6: h=i; s=110; break;                       /* pastel rainbow */
            default: h=i; break;                             /* rainbow */
        }
        PAL[i]=m_hsv(h&255,s,v);
    }
    cur_pal=p;
}

/* ---- brushes ---- */
static float bx[MAX_B],by[MAX_B];        /* position */
static float bang[MAX_B],bcurv[MAX_B];   /* heading, curvature (Wander/Scribble/Bounce) */
static float bt[MAX_B],bph[MAX_B];       /* parametric time & phase (Lissajous/Spiral) */
static float bfa[MAX_B],bfb[MAX_B];      /* Lissajous frequency ratios */
static float bcx[MAX_B],bcy[MAX_B];      /* Spiral centre */
static float bpos[MAX_B];                /* palette position 0..256 */
static float bsf[MAX_B];                 /* per-brush speed factor */
static int   nb=0, cur_motion=-1;
static float fade_acc=0.0f;
static int   prev_tick=0;

static NOINLINE void init_brush(int i,int n){
    bx[i]=frand()*(float)W; by[i]=2.0f+frand()*(float)(H-4);
    float a=(frand()*0.8f+0.35f)*(frand()<0.5f?1.0f:-1.0f);      /* avoid near-horizontal */
    bang[i]=frand()<0.5f?a:3.14159265f-a;
    bcurv[i]=0.0f;
    bt[i]=frand()*6.2831853f; bph[i]=frand()*6.2831853f;
    bfa[i]=1.0f+(float)(rnd()%3); bfb[i]=1.0f+(float)(rnd()%3); if(bfa[i]==bfb[i]) bfb[i]+=1.0f;
    bcx[i]=bx[i]; bcy[i]=by[i];
    bpos[i]=(float)(i*256/n);
    bsf[i]=0.8f+frand()*0.4f;
}

/* ---- paint one pixel of the canvas with coverage ci (0..256) ---- */
static inline void paint_px(uint8_t*p,int cr,int cg,int cb,int ci,int mode){
    int pr=p[0],pg=p[1],pb=p[2];
    int tr,tg,tb;
    if(mode==1){                                          /* Add (light) */
        tr=pr+((cr*ci)>>8); tg=pg+((cg*ci)>>8); tb=pb+((cb*ci)>>8);
        p[0]=tr>255?255:tr; p[1]=tg>255?255:tg; p[2]=tb>255?255:tb;
        return;
    }
    int a=pr>pg?pr:pg; if(pb>a)a=pb;
    if(mode==0 || a==0){ tr=cr; tg=cg; tb=cb; }
    else {
        int nr=pr*255/a, ng=pg*255/a, nbb=pb*255/a;       /* existing pigment at full brightness */
        if(mode==2){ tr=(nr+cr)>>1; tg=(ng+cg)>>1; tb=(nbb+cb)>>1; }        /* Mix */
        else       { tr=nr*cr/255;  tg=ng*cg/255;  tb=nbb*cb/255; }        /* CMYK (multiply) */
    }
    p[0]=pr+(((tr-pr)*ci)>>8); p[1]=pg+(((tg-pg)*ci)>>8); p[2]=pb+(((tb-pb)*ci)>>8);
}

/* soft round stamp of radius r at (cx,cy); w = per-stamp weight for the
 * accumulating blend modes (Replace ignores it — it's idempotent) */
static NOINLINE void stamp(float cx,float cy,float r,int rgb,int mode,float w){
    int cr=(rgb>>16)&255, cg=(rgb>>8)&255, cb=rgb&255;
    int x0=(int)(cx-r-1.0f)-1, x1=(int)(cx+r+1.0f)+1;
    int y0=(int)(cy-r-1.0f)-1, y1=(int)(cy+r+1.0f)+1;
    if(y0<0)y0=0; if(y1>H-1)y1=H-1;
    int wi=(int)(w*256.0f); if(wi>256)wi=256;
    for(int y=y0;y<=y1;y++){
        float dy=(float)y-cy;
        for(int x=x0;x<=x1;x++){
            float dx=(float)x-cx;
            float cov=r+0.5f-__builtin_sqrtf(dx*dx+dy*dy);
            if(cov<=0.0f) continue;
            if(cov>1.0f) cov=1.0f;
            int ci=(int)(cov*256.0f);
            if(mode!=0) ci=(ci*wi)>>8;
            if(ci<=0) continue;
            int xx=x%W; if(xx<0)xx+=W;
            paint_px(PAINT+(y*W+xx)*3,cr,cg,cb,ci,mode);
        }
    }
}

/* white brush head drawn over the canvas copy in FB */
static NOINLINE void head(float cx,float cy,float r){
    int x0=(int)(cx-r-1.0f)-1, x1=(int)(cx+r+1.0f)+1;
    int y0=(int)(cy-r-1.0f)-1, y1=(int)(cy+r+1.0f)+1;
    if(y0<0)y0=0; if(y1>H-1)y1=H-1;
    for(int y=y0;y<=y1;y++){
        float dy=(float)y-cy;
        for(int x=x0;x<=x1;x++){
            float dx=(float)x-cx;
            float cov=r+0.5f-__builtin_sqrtf(dx*dx+dy*dy);
            if(cov<=0.0f) continue;
            if(cov>1.0f) cov=1.0f;
            int ci=(int)(cov*256.0f);
            int xx=x%W; if(xx<0)xx+=W;
            uint8_t*p=FB+(y*W+xx)*3;
            p[0]+=((255-p[0])*ci)>>8; p[1]+=((255-p[1])*ci)>>8; p[2]+=((255-p[2])*ci)>>8;
        }
    }
}

static inline float wrapx(float x){ while(x<0)x+=(float)W; while(x>=(float)W)x-=(float)W; return x; }

/* move brush i by dt; returns new position in *nx,*ny */
static NOINLINE void move_brush(int i,int motion,float v,float dt,float r){
    float x=bx[i],y=by[i];
    switch(motion){
        case 2: {                                            /* Lissajous */
            float A=(float)W*0.5f, B=(float)H*0.5f-r;
            bt[i]+=v*dt/(A*0.9f);
            x=A+A*m_sin(bfa[i]*bt[i]+bph[i]);
            y=B+r+B*m_sin(bfb[i]*bt[i]);
        } break;
        case 3: {                                            /* Spiral around a drifting centre */
            float R=(float)(W<H?W:H)*0.22f*(1.15f+m_sin(bt[i]*0.13f+bph[i]));
            if(R<1.0f)R=1.0f;
            bt[i]+=v*dt/R;
            bcurv[i]+=(frand()-0.5f)*4.0f*dt; if(bcurv[i]>1.5f)bcurv[i]=1.5f; if(bcurv[i]<-1.5f)bcurv[i]=-1.5f;
            bang[i]+=bcurv[i]*dt;
            float cv=v*0.25f;
            bcx[i]+=m_cos(bang[i])*cv*dt; bcy[i]+=m_sin(bang[i])*cv*dt;
            float lo=r, hi=(float)(H-1)-r;
            if(bcy[i]<lo){ bcy[i]=lo+(lo-bcy[i]); bang[i]=-bang[i]; }
            if(bcy[i]>hi){ bcy[i]=hi-(bcy[i]-hi); bang[i]=-bang[i]; }
            bcx[i]=wrapx(bcx[i]);
            x=bcx[i]+R*m_cos(bt[i]); y=bcy[i]+R*m_sin(bt[i]);
            if(y<0.0f)y=0.0f; if(y>(float)(H-1))y=(float)(H-1);
        } break;
        default: {                                           /* Wander / Bounce / Scribble */
            if(motion==0){
                bcurv[i]+=(frand()-0.5f)*8.0f*dt; if(bcurv[i]>2.5f)bcurv[i]=2.5f; if(bcurv[i]<-2.5f)bcurv[i]=-2.5f;
                bang[i]+=bcurv[i]*dt;
            } else if(motion==4){
                bcurv[i]+=(frand()-0.5f)*60.0f*dt; if(bcurv[i]>12.0f)bcurv[i]=12.0f; if(bcurv[i]<-12.0f)bcurv[i]=-12.0f;
                bang[i]+=bcurv[i]*dt;
                v*=1.4f;
            }
            x+=m_cos(bang[i])*v*dt; y+=m_sin(bang[i])*v*dt;
            float lo=0.0f, hi=(float)(H-1);
            if(y<lo){ y=lo+(lo-y); bang[i]=-bang[i]; bcurv[i]=-bcurv[i]; }
            if(y>hi){ y=hi-(y-hi); bang[i]=-bang[i]; bcurv[i]=-bcurv[i]; }
            if(y<lo)y=lo; if(y>hi)y=hi;
        } break;
    }
    bx[i]=wrapx(x); by[i]=y;
}

static void dims(void){ W=get_width(); H=get_height();
    if(W>MAX_W)W=MAX_W; if(H>MAX_H)H=MAX_H; if(W<1)W=1; if(H<1)H=1; }

EXPORT(init) void init(void){
    dims();
    m_fill(PAINT,MAX_W*MAX_H,0);
    build_palette(0);
    nb=0; cur_motion=-1; prev_tick=0; fade_acc=0.0f;
}

EXPORT(update) void update(int tick_ms){
    int oW=W,oH=H; dims();
    if(oW!=W||oH!=H){ m_fill(PAINT,MAX_W*MAX_H,0); nb=0; }

    int speed=get_param_i32(0), want=get_param_i32(1), size=get_param_i32(2);
    int pal=get_param_i32(3), mode=get_param_i32(4), fade=get_param_i32(5);
    int motion=get_param_i32(6), cchg=get_param_i32(7);
    if(want<1)want=1; if(want>MAX_B)want=MAX_B;
    if(size<1)size=1; if(size>10)size=10;
    if(pal<0||pal>6)pal=0; if(mode<0||mode>3)mode=0; if(motion<0||motion>4)motion=0;

    int delta=tick_ms-prev_tick; if(delta<=0||delta>200)delta=33; prev_tick=tick_ms;
    float dt=(float)delta/1000.0f;
    rng^=(uint32_t)tick_ms;

    if(pal!=cur_pal) build_palette(pal);
    if(motion!=cur_motion){ cur_motion=motion; for(int i=0;i<nb;i++){ bt[i]=frand()*6.2831853f; bcx[i]=bx[i]; bcy[i]=by[i]; bcurv[i]=0.0f; } }
    if(want!=nb){ for(int i=0;i<want;i++) if(i>=nb) init_brush(i,want); nb=want; }

    float r=0.6f+(float)size*0.45f;
    float v=(float)speed*0.6f;                              /* px/s */
    float hrate=(float)(cchg*cchg)*0.0012f;                 /* palette units per px */

    /* whole-canvas fade: strength in 1/256 steps per second, accumulated so
     * slow settings still fade (m_fade truncates, so keep=255 drops >=1/frame) */
    if(fade>0){
        fade_acc+=(float)(fade*fade)*0.035f*dt;
        int n=(int)fade_acc;
        if(n>0){ fade_acc-=(float)n; if(n>200)n=200; m_fade(PAINT,W*H*3,256-n); }
    } else fade_acc=0.0f;

    /* move brushes & lay paint along the path they travelled this frame */
    for(int i=0;i<nb;i++){
        float px=bx[i],py=by[i];
        move_brush(i,motion,v*bsf[i],dt,r);
        float dx=bx[i]-px, dy=by[i]-py;
        if(dx>(float)W*0.5f)dx-=(float)W; else if(dx<-(float)W*0.5f)dx+=(float)W;
        float dist=__builtin_sqrtf(dx*dx+dy*dy);
        float step=r*0.5f; if(step<0.5f)step=0.5f;
        int n=(int)(dist/step)+1; if(n>24)n=24;
        float seg=dist/(float)n, w=seg/(2.0f*r); if(w>1.0f)w=1.0f;
        for(int k=1;k<=n;k++){
            float f=(float)k/(float)n;
            float sx=wrapx(px+dx*f), sy=py+dy*f;
            float pp=bpos[i]+dist*f*hrate;
            int pi=(int)pp&255;
            stamp(sx,sy,r,PAL[pi],mode,w);
        }
        bpos[i]+=dist*hrate; while(bpos[i]>=256.0f)bpos[i]-=256.0f;
    }

    /* compose: canvas + white heads */
    {
        int n4=(W*H*3+3)>>2;
        uint32_t*s=(uint32_t*)PAINT,*d=(uint32_t*)FB;
        for(int k=0;k<n4;k++) d[k]=s[k];
    }
    for(int i=0;i<nb;i++) head(bx[i],by[i],r);
    draw();
}
