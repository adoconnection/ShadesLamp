#include "api.h"

/* ---- Metadata JSON ---- */
static const char META[] =
    "{\"name\":\"DNA Helix\","
    "\"desc\":\"Double helix wrapped around the lamp, rotating at a steady pace\","
    "\"params\":["
        "{\"id\":0,\"name\":\"Speed\",\"type\":\"int\","
         "\"min\":1,\"max\":50,\"default\":5,"
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

/* Clean per-frame framebuffer (no persistent trails) */
static uint8_t fb_r[MAX_W][MAX_H];
static uint8_t fb_g[MAX_W][MAX_H];
static uint8_t fb_b[MAX_W][MAX_H];

EXPORT(init)
void init(void) {
}

/* Saturating add. */
static uint8_t qadd8(uint8_t a, int b) {
    int s = (int)a + b;
    return s > 255 ? 255 : (uint8_t)s;
}

/* Additive blend with horizontal cylinder wrap. Where two crossing lines
   overlap, their colors sum into a brighter "wall" node. */
static void put(int x, int y, int r, int g, int b, int W, int H) {
    x = ((x % W) + W) % W;
    if (y < 0 || y >= H) return;
    fb_r[x][y] = qadd8(fb_r[x][y], r);
    fb_g[x][y] = qadd8(fb_g[x][y], g);
    fb_b[x][y] = qadd8(fb_b[x][y], b);
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

    /* tick_ms is ABSOLUTE elapsed time (millis()), not a per-frame delta — use
       it directly as the time base; do NOT accumulate it. */
    if (tick_ms < 0) tick_ms = 0;

    /* Clear framebuffer */
    for (int x = 0; x < W; x++)
        for (int y = 0; y < H; y++) {
            fb_r[x][y] = 0; fb_g[x][y] = 0; fb_b[x][y] = 0;
        }

    uint32_t row_ang = 65536u / PITCH_ROWS;        /* twist added per row */
    uint32_t yoff_q8 = 0;                           /* no vertical motion */

    /* Rotation = sliding the whole pattern along X (around the cylinder), exactly
       like the Earth map scroll. The lattice shifts rigidly and wraps, so it
       reads as a steady spin with no internal shearing. */
    uint32_t Wfp = (uint32_t)(W * 256);
    int xoff = (int)(((uint32_t)tick_ms * (uint32_t)speed / 12u) % Wfp);

    /* Fixed base hue (rainbow gradient runs along the height) */
    int base_hue = 0;

    int core_fp  = 150;                            /* front strand half-width (~0.6px) */
    int core_fp2 = 105;                            /* back strand: thinner (looks farther) */

    for (int y = 0; y < H; y++) {
        uint32_t vy_q8 = ((uint32_t)y << 8) + yoff_q8;

        /* Group A spirals one way, group B the opposite way. Each group has a
           bright near strand and a dim anti-phase far strand (see-through). */
        uint32_t tA  = ((vy_q8 * row_ang) >> 8) & 0xFFFFu;
        uint32_t tA2 = (tA + 0x8000u) & 0xFFFFu;
        uint32_t tB  = (0x10000u - tA) & 0xFFFFu;          /* mirrored slope */
        uint32_t tB2 = (tB + 0x8000u) & 0xFFFFu;

        int xA1 = (int)((tA  * (uint32_t)W) >> 8) + xoff;
        int xA2 = (int)((tA2 * (uint32_t)W) >> 8) + xoff;
        int xB1 = (int)((tB  * (uint32_t)W) >> 8) + xoff;
        int xB2 = (int)((tB2 * (uint32_t)W) >> 8) + xoff;

        /* Complementary hues so the crossings sum to bright white "wall" nodes. */
        int hA = (hue == 0) ? base_hue + y * 2 : hue;
        int hB = hA + 128;
        int dim = bright * 45 / 100;

        int rA, gA, bA, rA2, gA2, bA2, rB, gB, bB, rB2, gB2, bB2;
        hsv_to_rgb(hA, 255, bright, &rA,  &gA,  &bA);
        hsv_to_rgb(hA, 255, dim,    &rA2, &gA2, &bA2);
        hsv_to_rgb(hB, 255, bright, &rB,  &gB,  &bB);
        hsv_to_rgb(hB, 255, dim,    &rB2, &gB2, &bB2);

        /* Dim far strands first, bright near strands on top. */
        draw_strand(xA2, y, rA2, gA2, bA2, core_fp2, W, H);
        draw_strand(xB2, y, rB2, gB2, bB2, core_fp2, W, H);
        draw_strand(xA1, y, rA,  gA,  bA,  core_fp,  W, H);
        draw_strand(xB1, y, rB,  gB,  bB,  core_fp,  W, H);
    }

    /* Output */
    for (int x = 0; x < W; x++)
        for (int y = 0; y < H; y++)
            set_pixel(x, y, fb_r[x][y], fb_g[x][y], fb_b[x][y]);

    draw();
}
