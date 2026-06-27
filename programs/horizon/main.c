#include "api.h"

/*
 * Horizon — a faraway view: a bright sky (gradient, like a real sky) over
 * darker water or a green field, split at an adjustable horizon line. Sparse
 * white clouds drift slowly along just under the very top. Presets recolour the
 * scene: Ocean / Bliss (a nod to the XP wallpaper) / Azure Meadow.
 * Y=0 is the BOTTOM, so the sky is the high-y rows, the ground the low-y rows.
 */

static const char META[] =
    "{\"name\":\"Horizon\","
    "\"desc\":\"Distant sky over water or field with drifting clouds\","
    "\"params\":["
        "{\"id\":0,\"name\":\"Preset\",\"type\":\"select\","
         "\"options\":[\"Ocean\",\"Bliss\",\"Azure Meadow\"],\"default\":0,"
         "\"desc\":\"Scene colours\"},"
        "{\"id\":1,\"name\":\"Horizon\",\"type\":\"int\","
         "\"min\":0,\"max\":100,\"default\":40,"
         "\"desc\":\"Height of the lower (water/field) part, %\"},"
        "{\"id\":2,\"name\":\"Clouds\",\"type\":\"select\","
         "\"options\":[\"Off\",\"Few\",\"Many\"],\"default\":1,"
         "\"desc\":\"How many clouds drift by\"},"
        "{\"id\":3,\"name\":\"Speed\",\"type\":\"int\","
         "\"min\":1,\"max\":100,\"default\":30,"
         "\"desc\":\"Cloud drift speed\"},"
        "{\"id\":4,\"name\":\"Brightness\",\"type\":\"int\","
         "\"min\":1,\"max\":255,\"default\":220,"
         "\"desc\":\"Overall brightness\"}"
    "]}";

EXPORT(get_meta_ptr)
int get_meta_ptr(void) { return (int)META; }
EXPORT(get_meta_len)
int get_meta_len(void) { return sizeof(META) - 1; }

/* ---- PRNG ---- */
static uint32_t rng = 20260627;
static uint32_t rng_next(void){ uint32_t x=rng; x^=x<<13; x^=x>>17; x^=x<<5; rng=x; return x; }
static float frand(void){ return (float)(rng_next() & 0xFFFF) / 65536.0f; }

/* ---- palette: 4 colours per preset (sky top, sky horizon, ground horizon, ground bottom) ---- */
typedef struct { int r,g,b; } Col;
static Col lerpC(Col a, Col b, float t){
    Col c; c.r=(int)(a.r+(b.r-a.r)*t); c.g=(int)(a.g+(b.g-a.g)*t); c.b=(int)(a.b+(b.b-a.b)*t); return c;
}

/* ---- clouds ---- */
/* Cloud shape & size taken from Greenland as drawn on the Earth map: a chunky
 * ~4x5 irregular teardrop, broad to the "north" (top), tapering downward. The
 * 5x5 mask holds soft edges (0.5) and a solid core (1.0). Row 0 = bottom. */
static const float GLAND[5][5] = {
    { 0.0f, 0.0f, 0.5f, 0.0f, 0.0f },   /* bottom point        */
    { 0.0f, 0.5f, 1.0f, 1.0f, 0.0f },
    { 0.0f, 1.0f, 1.0f, 1.0f, 0.5f },   /* middle (widest)     */
    { 0.5f, 1.0f, 1.0f, 1.0f, 0.0f },
    { 0.0f, 0.5f, 1.0f, 0.5f, 0.0f },   /* top                 */
};

/* mask value with implicit 0 outside the 5x5 grid (soft fade to nothing) */
static float gland_at(int r, int c){
    if (r < 0 || r > 4 || c < 0 || c > 4) return 0.0f;
    return GLAND[r][c];
}

#define MAX_CLOUDS 8
static float   cl_x[MAX_CLOUDS];   /* centre x (drifts) */
static float   cl_y[MAX_CLOUDS];   /* centre y (near the top) */
static float   cl_spd[MAX_CLOUDS]; /* drift speed factor (parallax) */
static uint8_t cl_flip[MAX_CLOUDS];/* mirror horizontally for variety */
static int   started = 0;
static int32_t prev_tick = 0;

EXPORT(init)
void init(void){ rng = 20260627; started = 0; prev_tick = 0; }

static void seed_clouds(int W, int H){
    for (int i = 0; i < MAX_CLOUDS; i++){
        cl_x[i]   = frand() * (float)W;
        /* centre ~2px below the top so the 5px-tall blob sits just under it */
        cl_y[i]   = (float)(H - 1) - 2.0f - frand() * (float)H * 0.10f;
        cl_spd[i] = 0.5f + frand() * 0.9f;                        /* parallax variation */
        cl_flip[i] = (rng_next() & 1);
    }
}

EXPORT(update)
void update(int tick_ms){
    int preset = get_param_i32(0);
    int horiz  = get_param_i32(1);
    int clouds = get_param_i32(2);
    int speed  = get_param_i32(3);
    int bright = get_param_i32(4);

    int W = get_width();
    int H = get_height();
    if (W < 1) W = 1;
    if (H < 1) H = 1;
    if (horiz < 0) horiz = 0;
    if (horiz > 100) horiz = 100;

    if (!started){ seed_clouds(W, H); started = 1; }

    int32_t delta = tick_ms - prev_tick;
    if (delta < 0 || delta > 200) delta = 33;
    prev_tick = tick_ms;
    float dt = (float)delta / 1000.0f;

    /* palette per preset: skyTop, skyHorizon, groundHorizon, groundBottom */
    Col skyTop, skyHor, grndHor, grndBot, cloudC = {255,255,255};
    if (preset == 1) {           /* Bliss — vivid blue sky, green rolling field */
        skyTop = (Col){ 28, 96,210}; skyHor = (Col){150,200,245};
        grndHor= (Col){ 96,176, 66}; grndBot= (Col){ 44,116, 44};
    } else if (preset == 2) {    /* Azure Meadow — light azure sky, fresh grass */
        skyTop = (Col){ 92,162,236}; skyHor = (Col){182,216,246};
        grndHor= (Col){122,192, 92}; grndBot= (Col){ 84,156, 70};
    } else {                     /* Ocean (default) — dark dusk sky over deep water.
                                  * Water = Earth program's "Map" sea (40,90,200)
                                  * at brightness 23 (x23/255), flat like Earth. */
        skyTop = (Col){  6, 14, 36}; skyHor = (Col){ 26, 52, 92};
        Col sea = (Col){ 40*23/255, 90*23/255, 200*23/255 };  /* ≈ (3,8,18) */
        grndHor = sea; grndBot = sea;
    }

    int horizonY = (int)((float)H * (float)horiz / 100.0f);   /* lower part height */
    if (horizonY < 0) horizonY = 0;          /* 0% = all sky */
    if (horizonY > H) horizonY = H;          /* 100% = all water/field */

    /* drift clouds */
    float spd = ((float)speed / 100.0f) * 6.0f + 0.4f;        /* px/sec */
    int nClouds = (clouds == 0) ? 0 : (clouds == 1) ? 3 : 6;
    if (nClouds > MAX_CLOUDS) nClouds = MAX_CLOUDS;
    for (int i = 0; i < nClouds; i++){
        cl_x[i] += cl_spd[i] * spd * dt;
        while (cl_x[i] >= (float)W) cl_x[i] -= (float)W;
        while (cl_x[i] < 0.0f)      cl_x[i] += (float)W;
    }

    float skyH = (float)(H - horizonY);
    if (skyH < 1.0f) skyH = 1.0f;
    float grH  = (float)horizonY;
    if (grH < 1.0f) grH = 1.0f;

    for (int y = 0; y < H; y++){
        Col base;
        if (y >= horizonY){
            /* sky: lighter/hazier near the horizon, deeper at the top */
            float t = (float)(y - horizonY) / skyH;     /* 0 horizon .. 1 top */
            base = lerpC(skyHor, skyTop, t);
        } else {
            /* water/field: lighter near the horizon, darker at the bottom */
            float t = (float)y / grH;                   /* 0 bottom .. 1 horizon */
            base = lerpC(grndBot, grndHor, t);
        }
        for (int x = 0; x < W; x++){
            int r = base.r, g = base.g, b = base.b;

            /* clouds — Greenland shape, sampled bilinearly at the cloud's
             * fractional position so the edges are soft and the drift smooth.
             * Drawn regardless of the horizon so they're never clipped. */
            if (nClouds > 0){
                float mask = 0.0f;
                for (int i = 0; i < nClouds; i++){
                    float ddx = (float)x - cl_x[i];
                    if (ddx >  (float)W * 0.5f) ddx -= (float)W;   /* wrap */
                    if (ddx < -(float)W * 0.5f) ddx += (float)W;
                    float gx = ddx + 2.0f;                 /* mask column coord */
                    float gy = ((float)y - cl_y[i]) + 2.0f;/* mask row coord */
                    if (gx <= -1.0f || gx >= 5.0f || gy <= -1.0f || gy >= 5.0f) continue;
                    if (cl_flip[i]) gx = 4.0f - gx;        /* mirror for variety */

                    int c0 = (int)gx; if (gx < 0.0f && (float)c0 != gx) c0--;
                    int r0 = (int)gy; if (gy < 0.0f && (float)r0 != gy) r0--;
                    float fx2 = gx - (float)c0;
                    float fy2 = gy - (float)r0;
                    float top = gland_at(r0,   c0) + (gland_at(r0,   c0+1) - gland_at(r0,   c0)) * fx2;
                    float bot = gland_at(r0+1, c0) + (gland_at(r0+1, c0+1) - gland_at(r0+1, c0)) * fx2;
                    float m   = top + (bot - top) * fy2;
                    if (m > mask) mask = m;
                }
                if (mask > 0.0f){
                    float a = mask * 0.92f;
                    r = (int)(r + (cloudC.r - r) * a);
                    g = (int)(g + (cloudC.g - g) * a);
                    b = (int)(b + (cloudC.b - b) * a);
                }
            }

            r = r * bright / 255; g = g * bright / 255; b = b * bright / 255;
            if (r > 255) r = 255; if (g > 255) g = 255; if (b > 255) b = 255;
            set_pixel(x, y, r, g, b);
        }
    }

    draw();
}
