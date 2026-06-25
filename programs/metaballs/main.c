#include "api.h"

/*
 * Metaballs — organic morphing blobs via isosurface math.
 *
 * Colours are blended in RGB (not hue-averaged), so overlapping blobs mix
 * like light: a red blob over a blue one reads purple, and deep overlaps
 * bloom toward a white-hot core. A curated palette picks pleasing colour
 * combinations, the Hue knob rotates the whole palette, and Size scales the
 * blobs live.
 */

static const char META[] =
    "{\"name\":\"Metaballs\","
    "\"desc\":\"Organic morphing blobs with light-mixing colours\","
    "\"params\":["
        "{\"id\":0,\"name\":\"Palette\",\"type\":\"select\","
         "\"default\":4,"
         "\"options\":[\"Rainbow\",\"Lava\",\"Ocean\",\"Toxic\",\"Plasma\",\"Sunset\",\"Aurora\",\"Ice\"],"
         "\"desc\":\"Colour combination\"},"
        "{\"id\":1,\"name\":\"Hue\",\"type\":\"int\","
         "\"min\":0,\"max\":255,\"default\":0,"
         "\"desc\":\"Rotate the whole palette\"},"
        "{\"id\":2,\"name\":\"Size\",\"type\":\"int\","
         "\"min\":2,\"max\":14,\"default\":6,"
         "\"desc\":\"Blob size\"},"
        "{\"id\":3,\"name\":\"Count\",\"type\":\"int\","
         "\"min\":2,\"max\":6,\"default\":4,"
         "\"desc\":\"Number of metaballs\"},"
        "{\"id\":4,\"name\":\"Speed\",\"type\":\"int\","
         "\"min\":1,\"max\":10,\"default\":4,"
         "\"desc\":\"Movement speed\"},"
        "{\"id\":5,\"name\":\"Brightness\",\"type\":\"int\","
         "\"min\":1,\"max\":255,\"default\":200,"
         "\"desc\":\"Overall brightness\"}"
    "]}";

EXPORT(get_meta_ptr)
int get_meta_ptr(void) { return (int)META; }

EXPORT(get_meta_len)
int get_meta_len(void) { return sizeof(META) - 1; }

/* ---- PRNG (xorshift32) ---- */
static uint32_t rng_state = 73541;

static uint32_t rng_next(void) {
    uint32_t x = rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng_state = x;
    return x;
}

static int random_range(int lo, int hi) {
    if (lo >= hi) return lo;
    return lo + (int)(rng_next() % (uint32_t)(hi - lo));
}

/* ---- Sine lookup table (256 entries, values -127..127) ---- */
static const signed char sin_table[256] = {
      0,   3,   6,   9,  12,  16,  19,  22,  25,  28,  31,  34,  37,  40,  43,  46,
     49,  51,  54,  57,  60,  63,  65,  68,  71,  73,  76,  78,  81,  83,  85,  88,
     90,  92,  94,  96,  98, 100, 102, 104, 106, 107, 109, 111, 112, 113, 115, 116,
    117, 118, 120, 121, 122, 122, 123, 124, 125, 125, 126, 126, 126, 127, 127, 127,
    127, 127, 127, 127, 126, 126, 126, 125, 125, 124, 123, 122, 122, 121, 120, 118,
    117, 116, 115, 113, 112, 111, 109, 107, 106, 104, 102, 100,  98,  96,  94,  92,
     90,  88,  85,  83,  81,  78,  76,  73,  71,  68,  65,  63,  60,  57,  54,  51,
     49,  46,  43,  40,  37,  34,  31,  28,  25,  22,  19,  16,  12,   9,   6,   3,
      0,  -3,  -6,  -9, -12, -16, -19, -22, -25, -28, -31, -34, -37, -40, -43, -46,
    -49, -51, -54, -57, -60, -63, -65, -68, -71, -73, -76, -78, -81, -83, -85, -88,
    -90, -92, -94, -96, -98,-100,-102,-104,-106,-107,-109,-111,-112,-113,-115,-116,
   -117,-118,-120,-121,-122,-122,-123,-124,-125,-125,-126,-126,-126,-127,-127,-127,
   -127,-127,-127,-127,-126,-126,-126,-125,-125,-124,-123,-122,-122,-121,-120,-118,
   -117,-116,-115,-113,-112,-111,-109,-107,-106,-104,-102,-100, -98, -96, -94, -92,
    -90, -88, -85, -83, -81, -78, -76, -73, -71, -68, -65, -63, -60, -57, -54, -51,
    -49, -46, -43, -40, -37, -34, -31, -28, -25, -22, -19, -16, -12,  -9,  -6,  -3
};

static int isin(int angle) { return (int)sin_table[angle & 255]; }
static int icos(int angle) { return (int)sin_table[(angle + 64) & 255]; }

/* ---- HSV to RGB (h,s,v in 0..255) ---- */
static void hsv_to_rgb(int h, int s, int v, int *r, int *g, int *b) {
    h = h & 255;
    if (s == 0) { *r = *g = *b = v; return; }
    int region = h / 43;
    int remainder = (h - region * 43) * 6;
    int p = (v * (255 - s)) >> 8;
    int q = (v * (255 - ((s * remainder) >> 8))) >> 8;
    int t = (v * (255 - ((s * (255 - remainder)) >> 8))) >> 8;
    switch (region) {
        case 0:  *r = v; *g = t; *b = p; break;
        case 1:  *r = q; *g = v; *b = p; break;
        case 2:  *r = p; *g = v; *b = t; break;
        case 3:  *r = p; *g = q; *b = v; break;
        case 4:  *r = t; *g = p; *b = v; break;
        default: *r = v; *g = p; *b = q; break;
    }
}

/* ---- Palettes: {base hue, hue spread across the blobs, saturation} ----
 * Blobs get distinct hues spread within [base, base+spread]; the Hue knob
 * adds a global offset to base so the whole scheme rotates together. */
#define NUM_PAL 8
static const int PAL_BASE[NUM_PAL]   = {  0,   0, 140,  70, 200, 240, 100, 150};
static const int PAL_SPREAD[NUM_PAL] = {255,  45,  60,  50,  60,  64, 110,  50};
static const int PAL_SAT[NUM_PAL]    = {255, 255, 255, 255, 255, 255, 255, 150};

/* ---- Blob state ---- */
#define MAX_BLOBS 6

/* Positions and velocities in fixed-point (* 256) */
static int blob_x[MAX_BLOBS];
static int blob_y[MAX_BLOBS];
static int blob_vx[MAX_BLOBS];
static int blob_vy[MAX_BLOBS];
static int blob_size_pct[MAX_BLOBS]; /* per-blob size variety, ~70..130 (%) */

/* Per-frame derived colour (RGB at full value) for each blob */
static int blob_r[MAX_BLOBS];
static int blob_g[MAX_BLOBS];
static int blob_b[MAX_BLOBS];
static int blob_rsq[MAX_BLOBS];      /* r^2 * 65536 for the influence calc */

static int matrix_w, matrix_h;
static uint32_t frame;

EXPORT(init)
void init(void) {
    matrix_w = get_width();
    matrix_h = get_height();
    if (matrix_w < 1) matrix_w = 1;
    if (matrix_h < 1) matrix_h = 1;
    frame = 0;
    rng_state = 73541;

    for (int i = 0; i < MAX_BLOBS; i++) {
        blob_x[i] = random_range(0, matrix_w * 256);
        blob_y[i] = random_range(0, matrix_h * 256);
        blob_vx[i] = random_range(-200, 200);
        blob_vy[i] = random_range(-200, 200);
        if (blob_vx[i] >= 0 && blob_vx[i] < 40) blob_vx[i] = 40;
        if (blob_vx[i] < 0 && blob_vx[i] > -40) blob_vx[i] = -40;
        if (blob_vy[i] >= 0 && blob_vy[i] < 40) blob_vy[i] = 40;
        if (blob_vy[i] < 0 && blob_vy[i] > -40) blob_vy[i] = -40;
        blob_size_pct[i] = random_range(70, 131);
    }
}

EXPORT(update)
void update(int tick_ms) {
    int pal    = get_param_i32(0);
    int hue    = get_param_i32(1);
    int size   = get_param_i32(2);
    int count  = get_param_i32(3);
    int speed  = get_param_i32(4);
    int bright = get_param_i32(5);

    int W = get_width();
    int H = get_height();
    if (W < 1) W = 1;
    if (H < 1) H = 1;
    if (count < 2) count = 2;
    if (count > MAX_BLOBS) count = MAX_BLOBS;
    if (pal < 0) pal = 0;
    if (pal >= NUM_PAL) pal = NUM_PAL - 1;
    if (size < 1) size = 1;

    rng_state ^= (uint32_t)tick_ms;
    frame++;

    int base   = PAL_BASE[pal];
    int spread = PAL_SPREAD[pal];
    int sat    = PAL_SAT[pal];

    /* Move blobs and derive their colour + radius for this frame */
    int speed_mult = speed * 3;
    for (int i = 0; i < count; i++) {
        blob_x[i] += (blob_vx[i] * speed_mult) / 10;
        blob_y[i] += (blob_vy[i] * speed_mult) / 10;

        /* Horizontal cylinder wrap */
        if (blob_x[i] < 0) blob_x[i] += W * 256;
        if (blob_x[i] >= W * 256) blob_x[i] -= W * 256;

        /* Vertical bounce */
        if (blob_y[i] < 0) {
            blob_y[i] = -blob_y[i];
            blob_vy[i] = -blob_vy[i];
        }
        if (blob_y[i] >= (H - 1) * 256) {
            blob_y[i] = (H - 1) * 256 * 2 - blob_y[i];
            blob_vy[i] = -blob_vy[i];
        }

        /* Slight wobble */
        blob_vx[i] += isin((int)(frame * 3 + i * 60)) / 32;
        blob_vy[i] += icos((int)(frame * 5 + i * 45)) / 32;
        if (blob_vx[i] > 300) blob_vx[i] = 300;
        if (blob_vx[i] < -300) blob_vx[i] = -300;
        if (blob_vy[i] > 300) blob_vy[i] = 300;
        if (blob_vy[i] < -300) blob_vy[i] = -300;

        /* Hue spread across the blobs within the palette range, + Hue knob */
        int pos = (count > 1) ? (i * spread) / (count - 1) : spread / 2;
        int h = base + hue + pos;
        hsv_to_rgb(h, sat, 255, &blob_r[i], &blob_g[i], &blob_b[i]);

        /* Effective radius (pixels): Size * per-blob variety. Kept integer so
         * r^2 * 65536 stays well within int32 (max ~18 -> ~21M). */
        int rad = (size * blob_size_pct[i]) / 100;
        if (rad < 1) rad = 1;
        if (rad > 18) rad = 18;
        blob_rsq[i] = rad * rad * 65536;
    }

    int half_w = W * 128;

    /* Render */
    for (int py = 0; py < H; py++) {
        for (int px = 0; px < W; px++) {
            int px256 = px * 256 + 128;
            int py256 = py * 256 + 128;

            int raw_total = 0;       /* uncapped field strength */
            long rsum = 0, gsum = 0, bsum = 0;
            int wsum = 0;

            for (int i = 0; i < count; i++) {
                int dxx = blob_x[i] - px256;
                if (dxx > half_w) dxx -= W * 256;
                if (dxx < -half_w) dxx += W * 256;
                int dyy = blob_y[i] - py256;

                int dx_r = dxx / 16;
                int dy_r = dyy / 16;
                int dist_sq = dx_r * dx_r + dy_r * dy_r;
                if (dist_sq < 1) dist_sq = 1;

                int influence = blob_rsq[i] / dist_sq;   /* 256 * (r/d)^2 */
                if (influence > 255) influence = 255;

                raw_total += influence;
                rsum += (long)influence * blob_r[i];
                gsum += (long)influence * blob_g[i];
                bsum += (long)influence * blob_b[i];
                wsum += influence;
            }

            if (raw_total > 24) {
                int total = raw_total > 255 ? 255 : raw_total;
                int val = total * bright / 255;

                /* Base colour = RGB-blend of all blobs (light mixing) */
                int cr = wsum ? (int)(rsum / wsum) : 0;
                int cg = wsum ? (int)(gsum / wsum) : 0;
                int cb = wsum ? (int)(bsum / wsum) : 0;

                int r = cr * val / 255;
                int g = cg * val / 255;
                int b = cb * val / 255;

                /* Hot core: strong overlaps bloom toward white */
                int over = raw_total - 255;
                if (over > 0) {
                    int wgt = over > 200 ? 200 : over;
                    r += (255 - r) * wgt / 255;
                    g += (255 - g) * wgt / 255;
                    b += (255 - b) * wgt / 255;
                }

                if (r > 255) r = 255;
                if (g > 255) g = 255;
                if (b > 255) b = 255;
                set_pixel(px, py, r, g, b);
            } else {
                set_pixel(px, py, 0, 0, 0);
            }
        }
    }

    draw();
}
