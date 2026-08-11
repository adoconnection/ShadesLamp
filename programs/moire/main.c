#include "api.h"

/*
 * Moire - Two overlapping geometric patterns that create beautiful
 * moire interference as they slowly shift. Each pixel's color is
 * computed from the combination of two wave functions emanating
 * from slightly offset centers. No framebuffer needed.
 * Designed for a cylindrical LED matrix (Y=0 is bottom).
 */

static const char META[] =
    "{\"name\":\"Moire\","
    "\"desc\":\"Interference patterns from two overlapping wave sources\","
    "\"params\":["
        "{\"id\":0,\"name\":\"Speed\",\"type\":\"int\","
         "\"min\":1,\"max\":100,\"default\":25,"
         "\"desc\":\"Pattern shift speed\"},"
        "{\"id\":1,\"name\":\"Brightness\",\"type\":\"int\","
         "\"min\":1,\"max\":255,\"default\":220,"
         "\"desc\":\"Overall brightness\"},"
        "{\"id\":2,\"name\":\"Pattern\",\"type\":\"int\","
         "\"min\":0,\"max\":2,\"default\":0,"
         "\"options\":[\"Circles\",\"Lines\",\"Grid\"],"
         "\"desc\":\"Base pattern type\"},"
        "{\"id\":3,\"name\":\"Separation\",\"type\":\"int\","
         "\"min\":1,\"max\":30,\"default\":10,"
         "\"desc\":\"Distance between pattern centers\"},"
        "{\"id\":4,\"name\":\"Density\",\"type\":\"int\","
         "\"min\":1,\"max\":20,\"default\":6,"
         "\"desc\":\"Fringe density (pattern tightness)\"}"
    "]}";

EXPORT(get_meta_ptr)
int get_meta_ptr(void) { return (int)META; }
EXPORT(get_meta_len)
int get_meta_len(void) { return sizeof(META) - 1; }

/* ---- Math helpers ---- */
#define TWO_PI   6.28318530f
#define PI       3.14159265f
#define HALF_PI  1.57079632f

static float fsin(float x) { return m_sin(x); }
static float fcos(float x) { return m_cos(x); }

/* ---- Interpolated sine table: the per-pixel trig runs in-wasm instead of
 * one host call per pixel. 1024 steps + lerp keeps the error ~5e-6 —
 * invisible after the 0..255 quantization. ---- */
static float SINT[1025];
static inline float tsin(float a) {
    float u = a * (1024.0f / TWO_PI);
    int   i = (int)u;
    float f = u - (float)i;
    if (f < 0.0f) { f += 1.0f; i -= 1; }
    int i0 = i & 1023;
    return SINT[i0] + (SINT[i0 + 1] - SINT[i0]) * f;
}
static inline float tcos(float a) { return tsin(a + HALF_PI); }

/* ---- HSV to RGB: in-wasm copy of the host m_hsv algorithm (bit-identical
 * output, no host-call round trip per pixel) ---- */
static void hsv2rgb(int hue, int sat, int val, int *r, int *g, int *b) {
    if (val < 0) val = 0; if (val > 255) val = 255;
    if (sat == 0) { *r = *g = *b = val; return; }
    int hh = hue & 0xFF;
    int region = hh / 43;
    int rem = (hh - region * 43) * 6;
    int p = (val * (255 - sat)) >> 8;
    int q = (val * (255 - ((sat * rem) >> 8))) >> 8;
    int t = (val * (255 - ((sat * (255 - rem)) >> 8))) >> 8;
    switch (region) {
        case 0:  *r = val; *g = t;   *b = p;   break;
        case 1:  *r = q;   *g = val; *b = p;   break;
        case 2:  *r = p;   *g = val; *b = t;   break;
        case 3:  *r = p;   *g = q;   *b = val; break;
        case 4:  *r = t;   *g = p;   *b = val; break;
        default: *r = val; *g = p;   *b = q;   break;
    }
}

#define MAX_W 64
#define MAX_H 64
static uint8_t FB[MAX_W * MAX_H * 3];
EXPORT(get_framebuffer)
int get_framebuffer(void) { return (int)FB; }

EXPORT(init)
void init(void) {
    for (int i = 0; i <= 1024; i++) {
        SINT[i] = m_sin((float)(i & 1023) * (TWO_PI / 1024.0f));
    }
}

/* shortest signed horizontal delta on a cylinder of width W (wraps seamlessly) */
static float wrap_dx(float dx, float W) {
    while (dx >  W * 0.5f) dx -= W;
    while (dx < -W * 0.5f) dx += W;
    return dx;
}

/* The moire fringe is the *difference* of two sources' phases (the envelope),
 * the visible ripple is their *sum* (the carrier). Source phases are computed
 * inline in the pixel loop from per-column/per-row precomputed deltas. */

EXPORT(update)
void update(int tick_ms) {
    int speed      = get_param_i32(0);
    int bright     = get_param_i32(1);
    int pattern    = get_param_i32(2);
    int separation = get_param_i32(3);
    int density    = get_param_i32(4);

    int W = get_width();
    int H = get_height();
    if (W > MAX_W) W = MAX_W;
    if (H > MAX_H) H = MAX_H;
    if (W < 1) W = 1;
    if (H < 1) H = 1;
    if (pattern < 0) pattern = 0;
    if (pattern > 2) pattern = 2;
    if (separation < 1) separation = 1;
    if (separation > 30) separation = 30;
    if (density < 1) density = 9;        /* guard saves predating this param */
    if (density > 20) density = 20;

    /* Time phase */
    float t  = (float)tick_ms * (float)speed * 0.00009f;
    float rp = t * 2.0f;                  /* travelling-wave carrier (the ripples) */

    float cx = (float)W * 0.5f;
    float cy = (float)H * 0.5f;

    /* Two sources sit on opposite sides of centre and slowly orbit, so the
     * interference fringes drift. Same freq -> clean two-source moire. */
    float sep = (float)separation * 0.5f;
    float ox = fcos(t * 0.5f) * sep;
    float oy = fsin(t * 0.5f) * sep * (float)H / (float)W; /* keep offset on-screen */
    float ax = cx + ox, ay = cy + oy;
    float bx = cx - ox, by = cy - oy;

    /* fringe density: higher -> tighter rings/lines */
    float freq = 0.30f + (float)density * 0.16f;

    /* per-source rotation for line/grid patterns (slightly different -> moire) */
    float angle_a = t * 0.25f;
    float angle_b = t * 0.25f + 0.45f;

    float base_hue = t * 12.0f;

    /* Frame constants that the old code recomputed per pixel (host trig) */
    float ca = fcos(angle_a), sa = fsin(angle_a);
    float cb = fcos(angle_b), sb = fsin(angle_b);

    /* Per-column horizontal deltas: the cylinder wrap runs W times per
     * frame instead of W*H times. dax/dbx for the two offset sources,
     * dgx for the grid's shared centre; squared copies for Circles. */
    static float dax[MAX_W], dbx[MAX_W], dgx[MAX_W];
    static float dax2[MAX_W], dbx2[MAX_W];
    for (int x = 0; x < W; x++) {
        dax[x] = wrap_dx((float)x - ax, (float)W);
        dbx[x] = wrap_dx((float)x - bx, (float)W);
        dgx[x] = wrap_dx((float)x - cx, (float)W);
        dax2[x] = dax[x] * dax[x];
        dbx2[x] = dbx[x] * dbx[x];
    }

    for (int y = 0; y < H; y++) {
        float dy_g = (float)y - cy;
        float da_y = (float)y - ay, da_y2 = da_y * da_y;
        float db_y = (float)y - by, db_y2 = db_y * db_y;
        uint8_t* row = FB + (uint32_t)y * W * 3;

        for (int x = 0; x < W; x++) {
            float env, car;
            if (pattern == 2) {
                /* Grid: two perpendicular line-moires (rotational grid moire).
                 * A small angle difference between the two grids is what
                 * actually produces the interference. */
                float dx = dgx[x];
                float pa1 = ( dx*ca + dy_g*sa) * freq, pb1 = ( dx*cb + dy_g*sb) * freq;
                float pa2 = (-dx*sa + dy_g*ca) * freq, pb2 = (-dx*sb + dy_g*cb) * freq;
                env = tcos((pa1 - pb1) * 0.5f) * tcos((pa2 - pb2) * 0.5f);
                car = 0.5f * tsin((pa1 + pb1) * 0.5f - rp)
                    + 0.5f * tsin((pa2 + pb2) * 0.5f - rp);
            } else {
                float pa, pb;
                if (pattern == 1) {              /* Lines: rotated axis projection */
                    pa = (dax[x] * ca + da_y * sa) * freq;
                    pb = (dbx[x] * cb + db_y * sb) * freq;
                } else {                         /* Circles: radial distance
                                                    (native wasm sqrt, no host call) */
                    pa = __builtin_sqrtf(dax2[x] + da_y2) * freq;
                    pb = __builtin_sqrtf(dbx2[x] + db_y2) * freq;
                }
                env = tcos((pa - pb) * 0.5f);    /* standing moire fringe */
                car = tsin((pa + pb) * 0.5f - rp); /* travelling ripple */
            }

            /* clean fringe brightness from the envelope; ripple = gentle shimmer */
            float fringe = env * env;            /* 0..1, dark nodal lines */
            float shimmer = 0.72f + 0.28f * (car * 0.5f + 0.5f);
            int val = (int)((float)bright * (0.06f + 0.94f * fringe) * shimmer);
            if (val < 0) val = 0;
            if (val > 255) val = 255;

            /* hue follows the fringe sign + slow time drift */
            int hue = (int)(base_hue + env * 44.0f + car * 8.0f) & 0xFF;
            int sat = 235;

            int r, g, b;
            hsv2rgb(hue, sat, val, &r, &g, &b);
            row[x * 3] = (uint8_t)r; row[x * 3 + 1] = (uint8_t)g; row[x * 3 + 2] = (uint8_t)b;
        }
    }

    draw();
}
