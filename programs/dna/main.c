#include "api.h"

/* ---- Metadata JSON ---- */
static const char META[] =
    "{\"name\":\"DNA Helix\","
    "\"desc\":\"Double helix wrapped around the lamp, rotating at a steady pace\","
    "\"params\":["
        "{\"id\":0,\"name\":\"Speed\",\"type\":\"int\","
         "\"min\":1,\"max\":10,\"default\":5,"
         "\"desc\":\"Rotation speed of the helix\"},"
        "{\"id\":1,\"name\":\"Hue\",\"type\":\"int\","
         "\"min\":0,\"max\":255,\"default\":0,"
         "\"desc\":\"Base hue (0=rainbow cycle)\"},"
        "{\"id\":2,\"name\":\"Brightness\",\"type\":\"int\","
         "\"min\":1,\"max\":255,\"default\":200,"
         "\"desc\":\"Overall brightness\"}"
    "]}";

EXPORT(get_meta_ptr)
int get_meta_ptr(void) { return (int)META; }

EXPORT(get_meta_len)
int get_meta_len(void) { return sizeof(META) - 1; }

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

/* The lamp is a cylinder: X wraps around the circumference. The helix is a pair
   of diagonal strands that spiral around it; advancing the phase scrolls them
   smoothly in one direction (true rotation, no edge slow-down or reversal). */
#define PITCH_ROWS   16     /* rows per full revolution around the cylinder */
#define RUNG_STEP    4      /* draw a base-pair rung every N rows */

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

/* Anti-aliased strand ribbon centered at x_fp (Q8) on row y, wrapping in X.
   core_fp = solid half-width; the ribbon feathers to zero over one more pixel. */
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
    int speed  = get_param_i32(0);
    int hue    = get_param_i32(1);
    int bright = get_param_i32(2);
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

    /* Gentle rotation around the cylinder, scaled by speed (Q16, 65536 = one
       full revolution). ~speed/2 units per ms => speed=5 is a calm ~26s/turn. */
    uint32_t ang = ((uint32_t)tick_acc * (uint32_t)speed) / 2u;
    uint32_t row_ang = 65536u / PITCH_ROWS;        /* twist added per row */
    uint32_t halfW_fp = (uint32_t)(W * 256) / 2u;  /* half circumference (Q8) */

    /* Fixed base hue (rainbow gradient runs along the height) */
    int base_hue = 0;

    int core_fp  = 150;                            /* front strand half-width (~0.6px) */
    int core_fp2 = 105;                            /* back strand: thinner (looks farther) */

    for (int y = 0; y < H; y++) {
        uint32_t t1 = (ang + (uint32_t)y * row_ang) & 0xFFFFu;
        uint32_t t2 = (t1 + 0x8000u) & 0xFFFFu;    /* opposite strand: +half turn */

        int x1_fp = (int)((t1 * (uint32_t)W) >> 8);  /* 0 .. W*256 */
        int x2_fp = (int)((t2 * (uint32_t)W) >> 8);

        /* Colors */
        int h1, h2, hr;
        if (hue == 0) {
            h1 = base_hue + y * 2;
            h2 = h1 + 22;
        } else {
            h1 = hue;
            h2 = hue + 22;
        }
        hr = (h1 + h2) / 2 + 6;

        /* Front strand full brightness; the anti-phase strand is the far side
           of the helix, drawn ~2x dimmer to fake the lamp being see-through. */
        int r1, g1, b1, r2, g2, b2;
        hsv_to_rgb(h1, 255, bright, &r1, &g1, &b1);
        hsv_to_rgb(h2, 255, bright * 45 / 100, &r2, &g2, &b2);

        /* Base-pair rung: connects the two strands across the cylinder. It is
           brightest near the two backbones and fades toward the middle, so it
           reads as a link between the strands rather than a solid bar. */
        if ((y % RUNG_STEP) == 0) {
            int rv = bright * 70 / 256;
            if (rv > 4) {
                int lo = x1_fp;
                int hi = x1_fp + (int)halfW_fp;    /* go from strand1 to strand2 (+X) */
                int mid = (lo + hi) / 2;
                int half = (hi - lo) / 2;
                if (half < 1) half = 1;
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
                    /* end-weighted: 100% at the strands, ~40% in the middle */
                    int dmid = px * 256 - mid; if (dmid < 0) dmid = -dmid;
                    int w = 100 + 155 * dmid / half;
                    if (w > 255) w = 255;
                    int v = rv * w / 255;
                    int rr, rg, rb;
                    hsv_to_rgb(hr, 160, v, &rr, &rg, &rb);
                    put(px, y, rr * cov / 255, rg * cov / 255, rb * cov / 255, W, H);
                }
            }
        }

        /* Draw the dim far strand first, the bright near strand on top. */
        draw_strand(x2_fp, y, r2, g2, b2, core_fp2, W, H);
        draw_strand(x1_fp, y, r1, g1, b1, core_fp,  W, H);
    }

    /* Output */
    for (int x = 0; x < W; x++)
        for (int y = 0; y < H; y++)
            set_pixel(x, y, fb_r[x][y], fb_g[x][y], fb_b[x][y]);

    draw();
}
