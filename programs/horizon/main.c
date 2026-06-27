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
         "\"options\":[\"Ocean\",\"Bliss\",\"Azure Meadow\",\"Sunset\",\"Sunrise\"],\"default\":0,"
         "\"desc\":\"Scene colours\"},"
        "{\"id\":1,\"name\":\"Horizon\",\"type\":\"int\","
         "\"min\":0,\"max\":100,\"default\":40,"
         "\"desc\":\"Height of the lower (water/field) part, %\"},"
        "{\"id\":2,\"name\":\"Clouds\",\"type\":\"int\","
         "\"min\":0,\"max\":8,\"default\":3,"
         "\"desc\":\"How many clouds drift by\"},"
        "{\"id\":3,\"name\":\"Speed\",\"type\":\"int\","
         "\"min\":1,\"max\":100,\"default\":30,"
         "\"desc\":\"Cloud drift speed\"},"
        "{\"id\":4,\"name\":\"Brightness\",\"type\":\"int\","
         "\"min\":1,\"max\":255,\"default\":220,"
         "\"desc\":\"Overall brightness\"},"
        "{\"id\":5,\"name\":\"Cloud Size\",\"type\":\"int\","
         "\"min\":5,\"max\":100,\"default\":25,"
         "\"desc\":\"Cloud size (anchored to the top)\"},"
        "{\"id\":6,\"name\":\"Clustering\",\"type\":\"int\","
         "\"min\":0,\"max\":100,\"default\":25,"
         "\"desc\":\"How tightly clouds bunch together (0 = spread across)\"}"
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
static float   cl_jit[MAX_CLOUDS]; /* per-cloud jitter within its slot (0..1) */
static float   cl_y[MAX_CLOUDS];   /* centre y offset below the very top */
static uint8_t cl_flip[MAX_CLOUDS];/* mirror horizontally for variety */
static float   cl_drift = 0.0f;    /* shared horizontal drift (keeps spacing constant) */
static int   started = 0;
static int32_t prev_tick = 0;

EXPORT(init)
void init(void){ rng = 20260627; started = 0; prev_tick = 0; cl_drift = 0.0f; }

static void seed_clouds(int W, int H){
    (void)W;
    for (int i = 0; i < MAX_CLOUDS; i++){
        cl_jit[i]  = frand();                     /* nudges the cloud inside its even slot */
        cl_y[i]    = frand() * (float)H * 0.10f;   /* small offset of the cloud TOP below the very top */
        cl_flip[i] = (rng_next() & 1);
    }
    cl_drift = 0.0f;
}

EXPORT(update)
void update(int tick_ms){
    int preset = get_param_i32(0);
    int horiz  = get_param_i32(1);
    int clouds = get_param_i32(2);
    int speed  = get_param_i32(3);
    int bright = get_param_i32(4);
    int csize  = get_param_i32(5);
    int cluster= get_param_i32(6);
    if (csize < 5) csize = 25;       /* guard saves predating this param */
    if (csize > 100) csize = 100;
    if (cluster < 0) cluster = 0;
    if (cluster > 100) cluster = 100;
    if (clouds < 0) clouds = 0;      /* Clouds is now a count (0..MAX_CLOUDS) */
    if (clouds > MAX_CLOUDS) clouds = MAX_CLOUDS;

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
    } else if (preset == 3) {    /* Sunset — violet dusk fading to a hot orange glow,
                                  * mirrored on dark water; clouds catch warm light */
        skyTop = (Col){ 40, 24, 70}; skyHor = (Col){255,120, 50};
        grndHor= (Col){120, 50, 30}; grndBot= (Col){ 30, 14, 26};
        cloudC = (Col){255,180,140};
    } else if (preset == 4) {    /* Sunrise — soft morning blue over a peach/gold dawn,
                                  * cool water below; clouds tinted warm white */
        skyTop = (Col){ 70,110,180}; skyHor = (Col){255,190,130};
        grndHor= (Col){110, 90, 70}; grndBot= (Col){ 26, 30, 44};
        cloudC = (Col){255,215,190};
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

    /* drift clouds — one shared offset so the spacing the user dials in is kept */
    float spd = ((float)speed / 100.0f) * 6.0f + 0.4f;        /* px/sec */
    int nClouds = clouds;
    cl_drift += spd * dt;
    while (cl_drift >= (float)W) cl_drift -= (float)W;
    while (cl_drift < 0.0f)      cl_drift += (float)W;

    /* Place clouds in evenly spaced slots across the width, then squeeze them
     * toward the centre as "Clustering" rises: 0 = spread across the whole
     * width, 100 = bunched into a tight knot. */
    float spreadF = 1.0f - (float)cluster / 100.0f * 0.92f;   /* 1.0 .. 0.08 */
    float cl_x[MAX_CLOUDS];
    for (int i = 0; i < nClouds; i++){
        float slot = ((float)i + 0.5f + (cl_jit[i] - 0.5f) * 0.7f) / (float)nClouds; /* 0..1 */
        float f    = 0.5f + (slot - 0.5f) * spreadF;          /* band around centre */
        float xx   = f * (float)W + cl_drift;
        while (xx >= (float)W) xx -= (float)W;
        while (xx < 0.0f)      xx += (float)W;
        cl_x[i] = xx;
    }

    /* cloud scale: px per mask cell. Bigger = bigger cloud (up to ~full width).
     * The 5x5 mask spans 4 cells, so footprint ≈ 4*cscale px. */
    float cscale = 0.6f + (float)csize / 100.0f * ((float)W * 0.18f);
    if (cscale < 0.4f) cscale = 0.4f;
    /* anchor each cloud's TOP just under the very top, regardless of size */
    float cyc[MAX_CLOUDS];
    for (int i = 0; i < nClouds; i++)
        cyc[i] = (float)(H - 1) - cl_y[i] - 2.0f * cscale;   /* top row sits ≈ H-1-cl_y */

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
                    /* map pixel offset -> mask cells, scaled by cloud size */
                    float gx = ddx / cscale + 2.0f;               /* mask column coord */
                    float gy = ((float)y - cyc[i]) / cscale + 2.0f;/* mask row coord */
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
