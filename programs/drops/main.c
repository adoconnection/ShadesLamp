#include "api.h"

/*
 * Drops — raindrops swell at random spots, then trickle down the lamp. While
 * falling each drop gets small random left/right nudges, so it meanders down a
 * natural wiggly path, leaving a fading wet trail. X wraps around the cylinder.
 * Y=0 is the BOTTOM, so falling = y decreasing.
 */

static const char META[] =
    "{\"name\":\"Drops\","
    "\"desc\":\"Raindrops swell and trickle down a meandering path\","
    "\"params\":["
        "{\"id\":0,\"name\":\"Colour\",\"type\":\"select\","
         "\"options\":[\"Water\",\"Storm\",\"Rainbow\"],\"default\":0,"
         "\"desc\":\"Colour palette\"},"
        "{\"id\":1,\"name\":\"Density\",\"type\":\"int\","
         "\"min\":1,\"max\":30,\"default\":8,"
         "\"desc\":\"How often new drops appear\"},"
        "{\"id\":2,\"name\":\"Brightness\",\"type\":\"int\","
         "\"min\":1,\"max\":255,\"default\":200,"
         "\"desc\":\"Overall brightness\"},"
        "{\"id\":3,\"name\":\"Speed\",\"type\":\"int\","
         "\"min\":1,\"max\":100,\"default\":40,"
         "\"desc\":\"Fall speed\"},"
        "{\"id\":4,\"name\":\"Wander\",\"type\":\"int\","
         "\"min\":0,\"max\":100,\"default\":40,"
         "\"desc\":\"Random left/right drift while falling\"},"
        "{\"id\":5,\"name\":\"Tail\",\"type\":\"int\","
         "\"min\":1,\"max\":20,\"default\":9,"
         "\"desc\":\"Wet trail length\"},"
        "{\"id\":6,\"name\":\"Size\",\"type\":\"int\","
         "\"min\":1,\"max\":3,\"default\":1,"
         "\"desc\":\"Drop width (pixels)\"},"
        "{\"id\":7,\"name\":\"Stalls\",\"type\":\"int\","
         "\"min\":0,\"max\":100,\"default\":25,"
         "\"desc\":\"How often a drop pauses while trickling down\"}"
    "]}";

EXPORT(get_meta_ptr)
int get_meta_ptr(void) { return (int)META; }
EXPORT(get_meta_len)
int get_meta_len(void) { return sizeof(META) - 1; }

/* ---- PRNG ---- */
static uint32_t rng = 13579;
static uint32_t rng_next(void){ uint32_t x=rng; x^=x<<13; x^=x>>17; x^=x<<5; rng=x; return x; }
static int random8(void){ return (int)(rng_next() & 0xFF); }
static int random_range(int lo,int hi){ if(lo>=hi)return lo; return lo+(int)(rng_next()%(uint32_t)(hi-lo)); }
static float frand(void){ return (float)(rng_next()&0xFFFF)/65536.0f; }

/* ---- HSV ---- */
static void hsv_to_rgb(int h,int s,int v,int*r,int*g,int*b){
    int c=m_hsv(h&0xFF,s,v); *r=(c>>16)&255; *g=(c>>8)&255; *b=c&255;
}

/* ---- framebuffer (intensity + hue per pixel) ---- */
#define MAX_W 64
#define MAX_H 64
static uint8_t fb_val[MAX_W][MAX_H];
static uint8_t fb_hue[MAX_W][MAX_H];

/* ---- drops ---- */
#define MAX_DROPS 48
#define PH_GROW 1
#define PH_HOLD 2
#define PH_FALL 3
static uint8_t d_phase[MAX_DROPS];
static float   d_x[MAX_DROPS], d_y[MAX_DROPS];
static float   d_vy[MAX_DROPS];                     /* fall speed */
static float   d_grow[MAX_DROPS];                   /* grow progress 0..1 */
static int32_t d_hold[MAX_DROPS];                   /* ms left holding in place */
static int32_t d_freeze[MAX_DROPS];                 /* ms left stalled while falling */
static uint8_t d_hue[MAX_DROPS];

static int32_t prev_tick;

EXPORT(init)
void init(void){
    rng = 13579;
    prev_tick = 0;
    for (int i=0;i<MAX_DROPS;i++) d_phase[i]=0;
    for (int x=0;x<MAX_W;x++) for (int y=0;y<MAX_H;y++){ fb_val[x][y]=0; fb_hue[x][y]=0; }
}

static uint8_t qsub(uint8_t a,uint8_t b){ return a>b?(uint8_t)(a-b):0; }

/* paint, keeping the brightest contribution */
static void paint(int x,int y,int v,int hue,int W,int H){
    x = ((x % W) + W) % W;
    if (y<0||y>=H) return;
    if (v > fb_val[x][y]) { fb_val[x][y]=(uint8_t)v; fb_hue[x][y]=(uint8_t)hue; }
}

/* paint a drop of the given width centred on xc (sides dimmer) */
static void paint_wide(int xc,int y,int v,int hue,int size,int W,int H){
    paint(xc, y, v, hue, W, H);
    if (size >= 2) { paint(xc-1, y, v*3/5, hue, W, H); paint(xc+1, y, v*3/5, hue, W, H); }
    if (size >= 3) { paint(xc-2, y, v*2/5, hue, W, H); paint(xc+2, y, v*2/5, hue, W, H); }
}

/* paint a round drop body of radius r (brighter centre, softer edge) */
static void paint_disc(int xc,int yc,int v,int hue,float r,int W,int H){
    if (r < 0.6f) { paint(xc, yc, v, hue, W, H); return; }
    int ri = (int)(r + 0.5f);
    float r2 = r*r + 0.25f;
    for (int dy=-ri; dy<=ri; dy++)
        for (int dx=-ri; dx<=ri; dx++) {
            float d2 = (float)(dx*dx + dy*dy);
            if (d2 > r2) continue;
            int vv = (d2 < 0.5f) ? v : (int)(v * (0.55f + 0.45f * (1.0f - d2 / r2)));
            paint(xc+dx, yc+dy, vv, hue, W, H);
        }
}

static int find_free(void){ for(int i=0;i<MAX_DROPS;i++) if(!d_phase[i]) return i; return -1; }

EXPORT(update)
void update(int tick_ms){
    int palette = get_param_i32(0);
    int density = get_param_i32(1);
    int bright  = get_param_i32(2);
    int speed   = get_param_i32(3);
    int wander  = get_param_i32(4);
    int tail    = get_param_i32(5);
    int size    = get_param_i32(6);
    int pause   = get_param_i32(7);
    if (size < 1) size = 1; if (size > 3) size = 3;
    if (pause < 0) pause = 0; if (pause > 100) pause = 100;

    int W = get_width();
    int H = get_height();
    if (W>MAX_W) W=MAX_W; if (W<1) W=1;
    if (H>MAX_H) H=MAX_H; if (H<1) H=1;
    if (tail<1) tail=9;

    rng ^= (uint32_t)tick_ms;

    int32_t delta = tick_ms - prev_tick;
    if (delta<0 || delta>200) delta=33;
    prev_tick = tick_ms;
    float dt = (float)delta/1000.0f;

    /* fade the wet trails — longer Tail = slower fade */
    int fade = (int)(72.0f/(float)tail); if(fade<3)fade=3; if(fade>90)fade=90;
    for (int x=0;x<W;x++) for (int y=0;y<H;y++) fb_val[x][y]=qsub(fb_val[x][y],(uint8_t)fade);

    /* palette saturation */
    int sat = (palette==1) ? 110 : (palette==2 ? 235 : 205);

    /* spawn new drops */
    if (random_range(0,100) < density) {
        int i = find_free();
        if (i >= 0) {
            d_phase[i] = PH_GROW;
            d_x[i] = (float)random_range(0,W) + 0.5f;
            d_y[i] = (float)random_range(H/3, H);     /* random spot, upper part */
            d_vy[i] = (float)H * (0.20f + (float)speed/100.0f*1.3f) * (0.8f+frand()*0.5f);
            d_grow[i] = 0.0f;
            d_freeze[i] = 0;
            if (palette==2) d_hue[i] = (uint8_t)random8();           /* Rainbow */
            else            d_hue[i] = (uint8_t)(140 + random_range(-8,9)); /* Water/Storm blue */
        }
    }

    float stepWander = (float)wander/100.0f * 1.8f;   /* L/R nudge per descended row */
    int   pauseProb  = pause * 2;                      /* per-1000 chance to stall */

    for (int i=0;i<MAX_DROPS;i++) {
        if (!d_phase[i]) continue;
        int xc = (int)(d_x[i] + 0.5f);

        if (d_phase[i]==PH_GROW) {                     /* swell: radius grows to size */
            d_grow[i] += dt / 0.30f;
            float gp = d_grow[i] > 1.0f ? 1.0f : d_grow[i];
            paint_disc(xc, (int)d_y[i], 255, d_hue[i], gp * (float)size, W, H);
            if (d_grow[i] >= 1.0f) { d_phase[i] = PH_HOLD; d_hold[i] = 220 + random_range(0,260); }
            continue;
        }

        if (d_phase[i]==PH_HOLD) {                      /* sit fully formed for a moment */
            paint_disc(xc, (int)d_y[i], 255, d_hue[i], (float)size, W, H);
            d_hold[i] -= delta;
            if (d_hold[i] <= 0) d_phase[i] = PH_FALL;
            continue;
        }

        /* PH_FALL */
        if (d_freeze[i] > 0) {                          /* stalled in place mid-trickle */
            d_freeze[i] -= delta;
            paint_disc(xc, (int)d_y[i], 255, d_hue[i], (float)size, W, H);
            continue;
        }
        if (pauseProb > 0 && (int)(rng_next() % 1000) < pauseProb) {
            d_freeze[i] = 140 + random_range(0, pause * 6);   /* begin a stall */
            paint_disc(xc, (int)d_y[i], 255, d_hue[i], (float)size, W, H);
            continue;
        }

        /* flow down; on each crossed row add a random L/R nudge -> meander */
        float old_y = d_y[i];
        d_vy[i] += (float)H * 0.25f * dt;
        d_y[i] -= d_vy[i] * dt;
        int y0 = (int)old_y, y1 = (int)d_y[i];
        for (int yy = y0; yy >= y1; yy--) {
            d_x[i] += (frand() - 0.5f) * 2.0f * stepWander;
            paint_wide((int)(d_x[i] + 0.5f), yy, 255, d_hue[i], size, W, H);
        }
        paint_disc((int)(d_x[i] + 0.5f), (int)d_y[i], 255, d_hue[i], (float)size, W, H); /* round head */

        if (d_y[i] < -1.0f) d_phase[i] = 0;
    }

    /* render */
    for (int x=0;x<W;x++) {
        for (int y=0;y<H;y++) {
            int v = fb_val[x][y];
            if (v==0){ set_pixel(x,y,0,0,0); continue; }
            int r,g,b;
            if (v >= 235) {                            /* bright head, whiter */
                hsv_to_rgb(fb_hue[x][y], sat/3, 255, &r,&g,&b);
            } else {
                hsv_to_rgb(fb_hue[x][y], sat, v, &r,&g,&b);
            }
            r=r*bright/255; g=g*bright/255; b=b*bright/255;
            set_pixel(x,y,r,g,b);
        }
    }

    draw();
}
