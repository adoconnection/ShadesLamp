#include "api.h"

/* ---- Metadata JSON ---- */
static const char META[] =
    "{\"name\":\"Popcorn\","
    "\"desc\":\"Kernels pop, fly up and pile while jostling the heap\","
    "\"params\":["
        "{\"id\":0,\"name\":\"Rate\",\"type\":\"int\","
         "\"min\":1,\"max\":100,\"default\":55,"
         "\"desc\":\"How often kernels pop\"},"
        "{\"id\":1,\"name\":\"Force\",\"type\":\"int\","
         "\"min\":1,\"max\":100,\"default\":55,"
         "\"desc\":\"How high they shoot\"},"
        "{\"id\":2,\"name\":\"Jiggle\",\"type\":\"int\","
         "\"min\":0,\"max\":100,\"default\":60,"
         "\"desc\":\"How much a pop tosses the pile\"},"
        "{\"id\":3,\"name\":\"Brightness\",\"type\":\"int\","
         "\"min\":1,\"max\":255,\"default\":235,"
         "\"desc\":\"Overall brightness\"},"
        "{\"id\":4,\"name\":\"Hue\",\"type\":\"int\","
         "\"min\":0,\"max\":255,\"default\":35,"
         "\"desc\":\"Popcorn tint (35=warm gold)\"},"
        "{\"id\":5,\"name\":\"Kernels\",\"type\":\"int\","
         "\"min\":1,\"max\":12,\"default\":6,"
         "\"desc\":\"How many kernels pop at once\"}"
    "]}";

EXPORT(get_meta_ptr)
int get_meta_ptr(void) { return (int)META; }

EXPORT(get_meta_len)
int get_meta_len(void) { return sizeof(META) - 1; }

/* ---- PRNG ---- */
static uint32_t rng = 0x9e3779b9u;
static uint32_t rng_next(void) { uint32_t x=rng; x^=x<<13; x^=x>>17; x^=x<<5; rng=x; return x; }
static float rnd(void) { return (float)(rng_next() & 0xFFFF) / 65536.0f; }
static float rnds(void) { return rnd()*2.0f - 1.0f; }
static int rrange(int lo, int hi) { if (lo>=hi) return lo; return lo + (int)(rng_next() % (uint32_t)(hi-lo)); }
static uint32_t hashu(uint32_t a){ a^=a>>16; a*=0x7feb352du; a^=a>>15; a*=0x846ca68bu; a^=a>>16; return a; }

/* ---- HSV to RGB ---- */
static void hsv2rgb(int h, int s, int v, int *r, int *g, int *b) {
    if (v<0)v=0; if(v>255)v=255; if(s<0)s=0; if(s>255)s=255;
    if (s==0){ *r=*g=*b=v; return; }
    h&=0xFF; int region=h/43, rem=(h-region*43)*6;
    int p=(v*(255-s))>>8, q=(v*(255-((s*rem)>>8)))>>8, t=(v*(255-((s*(255-rem))>>8)))>>8;
    switch(region){
        case 0:*r=v;*g=t;*b=p;break; case 1:*r=q;*g=v;*b=p;break;
        case 2:*r=p;*g=v;*b=t;break; case 3:*r=p;*g=q;*b=v;break;
        case 4:*r=t;*g=p;*b=v;break; default:*r=v;*g=p;*b=q;break;
    }
}

/* ---- Geometry ---- */
#define MAX_W 64
#define MAX_H 64

/* Pile: rest height + a transient upward jostle that settles back. */
static float pileBase[MAX_W];   /* rest height (grows as popcorn lands, self-levels) */
static float bump[MAX_W];       /* transient upward jostle from pops (>=0), decays */
static float tmpBuf[MAX_W];     /* scratch for diffusion passes */

/* Flying popcorn particles */
#define MAX_FLY 56
static int   fl_on[MAX_FLY];
static float fl_x[MAX_FLY], fl_y[MAX_FLY], fl_vx[MAX_FLY], fl_vy[MAX_FLY];

/* Kernels sitting on the surface, waiting to pop */
#define MAX_KERN 12
static float k_x[MAX_KERN];
static int   k_timer[MAX_KERN];

static int curW=16, curH=32;
static int32_t prev_tick;
static int emptying = 0;

static int wrapc(int x){ x%=curW; if(x<0)x+=curW; return x; }

static void reset_kernel(int i, int rate) {
    k_x[i] = rnd() * (float)curW;
    int base = 95 - rate * 8 / 10;        /* higher rate -> shorter wait */
    if (base < 8) base = 8;
    k_timer[i] = rrange(base / 2, base);
}

EXPORT(init)
void init(void) {
    curW = get_width();  if (curW<1)curW=1; if(curW>MAX_W)curW=MAX_W;
    curH = get_height(); if (curH<1)curH=1; if(curH>MAX_H)curH=MAX_H;
    for (int x=0;x<MAX_W;x++){ pileBase[x]=0; bump[x]=0; }
    for (int i=0;i<MAX_FLY;i++) fl_on[i]=0;
    for (int i=0;i<MAX_KERN;i++){ k_x[i]=rnd()*(float)curW; k_timer[i]=rrange(10,60); }
    prev_tick = 0;
    emptying = 0;
}

/* spawn a flying popcorn at (x,y) with an upward random-angle kick */
static void launch(float x, float y, float force, float upbias) {
    for (int i=0;i<MAX_FLY;i++){
        if (!fl_on[i]){
            fl_on[i]=1;
            fl_x[i]=x; fl_y[i]=y;
            fl_vx[i]=rnds()*force*0.6f;
            fl_vy[i]=force*(upbias + (1.0f-upbias)*rnd());
            return;
        }
    }
}

EXPORT(update)
void update(int tick_ms) {
    int rate   = get_param_i32(0);
    int force_p= get_param_i32(1);
    int jig    = get_param_i32(2);
    int bright = get_param_i32(3);
    int hue    = get_param_i32(4);
    int kerns  = get_param_i32(5);

    curW = get_width();  if (curW<1)curW=1; if(curW>MAX_W)curW=MAX_W;
    curH = get_height(); if (curH<1)curH=1; if(curH>MAX_H)curH=MAX_H;
    if (kerns<1)kerns=1; if(kerns>MAX_KERN)kerns=MAX_KERN;

    rng ^= (uint32_t)tick_ms;
    int32_t d = tick_ms - prev_tick;
    if (d<=0 || d>200) d=33;
    prev_tick = tick_ms;
    float dt = (float)d / 1000.0f;

    float gravity = (float)curH * 1.3f;                 /* px/s^2, scales with height */
    float launchV = (4.0f + (float)force_p * 0.5f);     /* base launch speed */
    launchV *= (float)curH / 32.0f;                     /* scale to lamp height */
    float jigBump = (float)jig * 0.035f;                /* px of upward jostle per pop (0..~3.5) */

    /* ---- settle the heap so it rises evenly (angle of repose) ---- */
    for (int x=0;x<curW;x++){
        float left  = pileBase[wrapc(x-1)];
        float right = pileBase[wrapc(x+1)];
        tmpBuf[x] = pileBase[x] + 0.22f * ((left + right) * 0.5f - pileBase[x]);
    }
    for (int x=0;x<curW;x++) pileBase[x] = tmpBuf[x];

    /* ---- jostle: bump spreads to neighbours and decays (no overshoot) ---- */
    for (int x=0;x<curW;x++){
        float left  = bump[wrapc(x-1)];
        float right = bump[wrapc(x+1)];
        tmpBuf[x] = (bump[x] * 0.5f + (left + right) * 0.25f) * 0.86f;
    }
    for (int x=0;x<curW;x++) bump[x] = tmpBuf[x];

    /* ---- kernels: tick & pop ---- */
    for (int i=0;i<kerns;i++){
        k_timer[i]--;
        if (k_timer[i] <= 0){
            int kx = wrapc((int)k_x[i]);
            float surf = pileBase[kx] + bump[kx];
            /* fly the popped kernel up at a random angle */
            launch((float)kx + 0.5f, surf + 1.0f, launchV, 0.72f);
            /* jostle the settled heap: a clean upward bump near the pop */
            bump[kx]          += jigBump;
            bump[wrapc(kx-1)] += jigBump * 0.5f;
            bump[wrapc(kx+1)] += jigBump * 0.5f;
            /* sometimes knock a ready piece loose off the heap top */
            if (pileBase[kx] > 2.0f && rnd() < 0.4f){
                launch((float)kx + rnds()*0.5f, surf, launchV*0.55f, 0.6f);
                pileBase[kx] -= 0.8f; if (pileBase[kx]<0) pileBase[kx]=0;
            }
            reset_kernel(i, rate);
        }
    }

    /* ---- flying popcorn: integrate + land ---- */
    for (int i=0;i<MAX_FLY;i++){
        if (!fl_on[i]) continue;
        fl_vy[i] -= gravity * dt;
        fl_y[i]  += fl_vy[i] * dt;
        fl_x[i]  += fl_vx[i] * dt;
        if (fl_x[i] < 0) fl_x[i] += curW;
        if (fl_x[i] >= curW) fl_x[i] -= curW;

        int ix = wrapc((int)fl_x[i]);
        float surf = pileBase[ix] + bump[ix];
        if (fl_vy[i] < 0.0f && fl_y[i] <= surf){
            if (!emptying){
                /* deposit into the lowest of the nearby columns so valleys
                   fill first -> the level rises evenly instead of in spikes */
                int lo = ix;
                if (pileBase[wrapc(ix-1)] < pileBase[lo]) lo = wrapc(ix-1);
                if (pileBase[wrapc(ix+1)] < pileBase[lo]) lo = wrapc(ix+1);
                pileBase[lo] += 1.0f;
            }
            fl_on[i] = 0;
        } else if (fl_y[i] < -2.0f){
            fl_on[i] = 0;                   /* safety: fell off bottom */
        }
    }

    /* ---- fill cycle: empty the pot once nearly full, then refill ---- */
    float avg = 0.0f;
    for (int x=0;x<curW;x++) avg += pileBase[x];
    avg /= (float)curW;
    if (!emptying && avg > (float)curH * 0.82f) emptying = 1;
    if (emptying){
        for (int x=0;x<curW;x++) pileBase[x] *= 0.94f;
        if (avg < (float)curH * 0.30f) emptying = 0;
    }

    /* ════ render ════ */
    /* background: near-black warm pot */
    for (int x=0;x<curW;x++)
        for (int y=0;y<curH;y++)
            set_pixel(x,y, 2,1,0);

    /* pile: lumpy warm popcorn from floor up to surface */
    for (int x=0;x<curW;x++){
        int top = (int)(pileBase[x] + bump[x] + 0.5f);
        if (top > curH) top = curH;
        for (int y=0;y<top;y++){
            uint32_t hsh = hashu((uint32_t)x*131u + (uint32_t)y*977u);
            int lump = (int)(hsh & 31) - 12;             /* texture */
            int depth = top - 1 - y;                      /* 0 at surface */
            int v = bright - depth*6 + lump;              /* brighter/fresher on top */
            if (v < 30) v = 30;
            int sat = 40 + depth*4; if (sat>120) sat=120; /* deeper = warmer */
            int r,g,b; hsv2rgb(hue, sat, v, &r,&g,&b);
            set_pixel(x,y,r,g,b);
        }
    }

    /* kernels: small dim orange dots on the surface */
    for (int i=0;i<kerns;i++){
        int kx = wrapc((int)k_x[i]);
        int ky = (int)(pileBase[kx] + bump[kx] + 0.5f);
        if (ky >= curH) ky = curH-1;
        int r,g,b; hsv2rgb((hue+8)&0xFF, 220, bright/3, &r,&g,&b);
        set_pixel(kx, ky, r, g, b);
    }

    /* flying popcorn: bright near-white puffs with a soft glow */
    for (int i=0;i<MAX_FLY;i++){
        if (!fl_on[i]) continue;
        int px = wrapc((int)fl_x[i]);
        int py = (int)(fl_y[i] + 0.5f);
        if (py < 0 || py >= curH) continue;
        int r,g,b; hsv2rgb(hue, 25, bright, &r,&g,&b);     /* bright cream core */
        set_pixel(px, py, r, g, b);
        int gr,gg,gb; hsv2rgb(hue, 70, bright/2, &gr,&gg,&gb);
        set_pixel(wrapc(px-1), py, gr,gg,gb);
        set_pixel(wrapc(px+1), py, gr,gg,gb);
        if (py+1 < curH) set_pixel(px, py+1, gr,gg,gb);
        if (py-1 >= 0)   set_pixel(px, py-1, gr,gg,gb);
    }

    draw();
}
