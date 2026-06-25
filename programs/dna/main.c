#include "api.h"

/* ---- Metadata JSON ---- */
static const char META[] =
    "{\"name\":\"DNA Helix\","
    "\"desc\":\"Clean double helix: two twisting strands with base-pair rungs\","
    "\"params\":["
        "{\"id\":0,\"name\":\"Hue\",\"type\":\"int\","
         "\"min\":0,\"max\":255,\"default\":0,"
         "\"desc\":\"Base hue (0=rainbow cycle)\"},"
        "{\"id\":1,\"name\":\"Brightness\",\"type\":\"int\","
         "\"min\":1,\"max\":255,\"default\":200,"
         "\"desc\":\"Overall brightness\"}"
    "]}";

EXPORT(get_meta_ptr)
int get_meta_ptr(void) { return (int)META; }

EXPORT(get_meta_len)
int get_meta_len(void) { return sizeof(META) - 1; }

/* ---- Sin lookup table (256 entries, values -127..+127) ---- */
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

/* Smooth sine: angle in Q8 fixed point (65536 = full circle). Returns -127..127
   with linear interpolation between table entries for sub-pixel smoothness. */
static int sin_q(int a) {
    int idx  = (a >> 8) & 255;
    int frac = a & 255;
    int s0 = sin_table[idx];
    int s1 = sin_table[(idx + 1) & 255];
    return s0 + (((s1 - s0) * frac) >> 8);
}
static int cos_q(int a) { return sin_q(a + (64 << 8)); }

/* ---- HSV to RGB (h,s,v in 0..255) ---- */
static void hsv_to_rgb(int h, int s, int v, int *r, int *g, int *b) {
    h = h & 255;
    if (s == 0) { *r = v; *g = v; *b = v; return; }
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

/* ---- State ---- */
#define MAX_W 64
#define MAX_H 64

/* Rows per full turn of the helix. The lamp is a cylinder (X wraps), default
   16x32. 16 rows/turn => ~2 readable turns on a 32-high panel. */
#define PITCH_ROWS   16
#define RUNG_STEP    3      /* draw a base-pair rung every N rows */
#define EDGE_MARGIN  384    /* keep strands this far (Q8: 1.5px) from the X edges */

static uint32_t tick_acc;

/* Clean per-frame framebuffer (no persistent trails) */
static uint8_t fb_r[MAX_W][MAX_H];
static uint8_t fb_g[MAX_W][MAX_H];
static uint8_t fb_b[MAX_W][MAX_H];

EXPORT(init)
void init(void) {
    tick_acc = 0;
}

/* Lighten (max) blend with horizontal cylinder wrap. */
static void put(int x, int y, int r, int g, int b, int W, int H) {
    x = ((x % W) + W) % W;
    if (y < 0 || y >= H) return;
    if (r > fb_r[x][y]) fb_r[x][y] = (uint8_t)r;
    if (g > fb_g[x][y]) fb_g[x][y] = (uint8_t)g;
    if (b > fb_b[x][y]) fb_b[x][y] = (uint8_t)b;
}

/* Draw one anti-aliased strand pixel-ribbon on row y.
   x_fp is the strand center in Q8. core_fp = solid half-width, the ribbon
   feathers to zero over one more pixel. r,g,b are full-brightness colors. */
static void draw_strand(int x_fp, int y, int r, int g, int b,
                        int core_fp, int W, int H) {
    int feather = 230;                 /* ~0.9px soft edge */
    int reach = core_fp + feather;
    int p0 = (x_fp - reach) >> 8;
    int p1 = (x_fp + reach + 255) >> 8;
    for (int px = p0; px <= p1; px++) {
        int d = px * 256 - x_fp;
        if (d < 0) d = -d;
        int cov;
        if (d <= core_fp) cov = 255;
        else if (d < reach) cov = 255 * (reach - d) / feather;
        else continue;
        put(px, y, r * cov / 255, g * cov / 255, b * cov / 255, W, H);
    }
}

EXPORT(update)
void update(int tick_ms) {
    int hue    = get_param_i32(0);
    int bright = get_param_i32(1);
    int W = get_width();
    int H = get_height();

    if (W > MAX_W) W = MAX_W;
    if (H > MAX_H) H = MAX_H;
    if (W < 2) W = 2;
    if (H < 1) H = 1;

    tick_acc += (uint32_t)tick_ms;

    /* Clear framebuffer */
    for (int x = 0; x < W; x++)
        for (int y = 0; y < H; y++) {
            fb_r[x][y] = 0; fb_g[x][y] = 0; fb_b[x][y] = 0;
        }

    /* Geometry (Q8 fixed point) */
    int cx_fp  = (W - 1) * 128;                    /* horizontal center */
    int amp_fp = cx_fp - EDGE_MARGIN;              /* sine amplitude */
    if (amp_fp < 256) amp_fp = 256;

    int angstep = 65536 / PITCH_ROWS;              /* phase advance per row */
    /* Helix rotation: advancing the phase sweeps the strands and reads as a
       twist. Fixed calm rate of ~2.5 units/ms (65536 = one full turn) gives a
       steady ~26s per turn. Bounded mod full-circle so the int never overflows. */
    int phase = (int)((((uint32_t)tick_acc * 5u) / 2u) & 0xFFFFu);

    /* Animated base hue for rainbow mode */
    int base_hue = (int)((tick_acc / 50) & 255);

    for (int y = 0; y < H; y++) {
        int a1 = phase + y * angstep;
        int a2 = a1 + (128 << 8);                  /* opposite strand, 180deg */

        int s1 = sin_q(a1), s2 = sin_q(a2);
        int z1 = cos_q(a1), z2 = cos_q(a2);        /* depth: +front, -back */

        int x1_fp = cx_fp + amp_fp * s1 / 127;
        int x2_fp = cx_fp + amp_fp * s2 / 127;

        /* Depth -> brightness (back strand ~35%, front 100%) and thickness */
        int f1 = 90 + (166 * (z1 + 127)) / 254;    /* 90..256 */
        int f2 = 90 + (166 * (z2 + 127)) / 254;
        int v1 = bright * f1 / 256;
        int v2 = bright * f2 / 256;
        int core1 = 80 + 70 * (z1 + 127) / 254;    /* front strand a touch thicker */
        int core2 = 80 + 70 * (z2 + 127) / 254;

        /* Colors */
        int h1, h2, hr;
        if (hue == 0) {
            h1 = base_hue + y * 2;
            h2 = h1 + 28;
        } else {
            h1 = hue;
            h2 = hue + 28;
        }
        hr = (h1 + h2) / 2 + 8;

        int r1, g1, b1, r2, g2, b2;
        hsv_to_rgb(h1, 255, v1, &r1, &g1, &b1);
        hsv_to_rgb(h2, 255, v2, &r2, &g2, &b2);

        /* Rung: connects the strands; fades to nothing at crossings (where the
           base pair turns edge-on) and is brightest when strands are far apart. */
        if ((y % RUNG_STEP) == 0) {
            int lo = x1_fp < x2_fp ? x1_fp : x2_fp;
            int hi = x1_fp < x2_fp ? x2_fp : x1_fp;
            int sep = hi - lo;
            int alpha = sep * 255 / (2 * amp_fp);
            if (alpha > 255) alpha = 255;
            int rv = bright * 105 / 256 * alpha / 255;
            if (rv > 4) {
                int rr, rg, rb;
                hsv_to_rgb(hr, 170, rv, &rr, &rg, &rb);
                int q0 = lo >> 8;
                int q1 = (hi + 255) >> 8;
                for (int px = q0; px <= q1; px++) {
                    int da = px * 256 - lo;
                    int db = hi - px * 256;
                    int m = da < db ? da : db;     /* soft 0.5px ends */
                    int cov;
                    if (m >= 128) cov = 255;
                    else if (m <= -128) continue;
                    else cov = (m + 128) * 255 / 256;
                    put(px, y, rr * cov / 255, rg * cov / 255, rb * cov / 255, W, H);
                }
            }
        }

        /* Draw back strand first, front strand last so it wins at crossings. */
        if (z1 <= z2) {
            draw_strand(x1_fp, y, r1, g1, b1, core1, W, H);
            draw_strand(x2_fp, y, r2, g2, b2, core2, W, H);
        } else {
            draw_strand(x2_fp, y, r2, g2, b2, core2, W, H);
            draw_strand(x1_fp, y, r1, g1, b1, core1, W, H);
        }
    }

    /* Output */
    for (int x = 0; x < W; x++)
        for (int y = 0; y < H; y++)
            set_pixel(x, y, fb_r[x][y], fb_g[x][y], fb_b[x][y]);

    draw();
}
