#include "api.h"

/* ---- Metadata JSON ---- */
static const char META[] =
    "{\"name\":\"DNA Helix\","
    "\"desc\":\"Double helix with two intertwined sine strands and connecting rungs\","
    "\"params\":["
        "{\"id\":0,\"name\":\"Speed\",\"type\":\"int\","
         "\"min\":1,\"max\":10,\"default\":5,"
         "\"desc\":\"Vertical scroll speed of the helix\"},"
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

/* ---- Sin lookup table (256 entries, values -127..+127) ---- */
/* Approximation of sin(i * 2*PI / 256) * 127 */
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

static int sin8(int angle) {
    return sin_table[angle & 255];
}

/* ---- HSV to RGB ---- */
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

static uint32_t tick_acc;

/* Framebuffer for smooth fading */
static uint8_t fb_r[MAX_W][MAX_H];
static uint8_t fb_g[MAX_W][MAX_H];
static uint8_t fb_b[MAX_W][MAX_H];

EXPORT(init)
void init(void) {
    tick_acc = 0;
    for (int x = 0; x < MAX_W; x++)
        for (int y = 0; y < MAX_H; y++) {
            fb_r[x][y] = 0;
            fb_g[x][y] = 0;
            fb_b[x][y] = 0;
        }
}

/* Saturating add for framebuffer */
static uint8_t qadd(uint8_t a, int b) {
    int s = (int)a + b;
    if (s > 255) return 255;
    if (s < 0) return 0;
    return (uint8_t)s;
}

static void fb_add_pixel(int x, int y, int r, int g, int b, int W, int H) {
    /* Horizontal wrap for cylinder */
    x = ((x % W) + W) % W;
    if (y < 0 || y >= H) return;
    fb_r[x][y] = qadd(fb_r[x][y], r);
    fb_g[x][y] = qadd(fb_g[x][y], g);
    fb_b[x][y] = qadd(fb_b[x][y], b);
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
    if (W < 1) W = 1;
    if (H < 1) H = 1;

    tick_acc += (uint32_t)tick_ms;

    /* Fade framebuffer */
    int fade = 18 + speed * 4;
    for (int x = 0; x < W; x++) {
        for (int y = 0; y < H; y++) {
            int r = fb_r[x][y] - fade;
            int g = fb_g[x][y] - fade;
            int b = fb_b[x][y] - fade;
            fb_r[x][y] = (uint8_t)(r < 0 ? 0 : r);
            fb_g[x][y] = (uint8_t)(g < 0 ? 0 : g);
            fb_b[x][y] = (uint8_t)(b < 0 ? 0 : b);
        }
    }

    /* Phase offset scrolls vertically with time */
    int phase = (int)(tick_acc * speed / 80);

    /* Frequency for the sine wave along the vertical axis:
       Higher values = more compressed helix. Use ~3000 scaled to height. */
    int freq = 3000;

    /* Draw the two strands for each row */
    for (int y = 0; y < H; y++) {
        /* Sine argument for this row + scroll offset */
        int angle = (y * freq / H + phase) & 255;
        int angle2 = (angle + 128) & 255; /* opposite strand, 180 degrees offset */

        /* Strand X positions: map sin (-127..127) to (0..W-1) */
        int cx = W / 2;
        int amplitude = (W - 2) / 2;
        if (amplitude < 1) amplitude = 1;

        int x1 = cx + (sin8(angle) * amplitude / 127);
        int x2 = cx + (sin8(angle2) * amplitude / 127);

        /* Hue: if param is 0, cycle rainbow; otherwise fixed hue */
        int h1, h2;
        if (hue == 0) {
            h1 = (y * 255 / H + (int)(tick_acc / 29)) & 255;
            h2 = (h1 + 128) & 255;
        } else {
            h1 = hue;
            h2 = (hue + 128) & 255;
        }

        /* Brightness modulation along the strand for depth effect */
        int depth1 = sin8(angle);  /* -127..127 */
        int depth2 = sin8(angle2);
        /* Map depth to brightness: front (+127) = bright, back (-127) = dim */
        int v1 = bright * (200 + depth1) / 327;
        int v2 = bright * (200 + depth2) / 327;
        if (v1 < 20) v1 = 20;
        if (v2 < 20) v2 = 20;

        /* Draw strand 1 */
        int r, g, b;
        hsv_to_rgb(h1, 255, v1, &r, &g, &b);
        fb_add_pixel(x1, y, r, g, b, W, H);

        /* Draw strand 2 */
        hsv_to_rgb(h2, 255, v2, &r, &g, &b);
        fb_add_pixel(x2, y, r, g, b, W, H);

        /* Draw connecting rungs every few rows */
        if ((y % 4) == 0) {
            /* Rung connects the two strands */
            int min_x = x1 < x2 ? x1 : x2;
            int max_x = x1 > x2 ? x1 : x2;
            /* Rung brightness depends on which strand is in front */
            int rung_v = bright / 3;
            int rung_h;
            if (hue == 0) {
                rung_h = (y * 255 / H + (int)(tick_acc / 29) + 64) & 255;
            } else {
                rung_h = (hue + 64) & 255;
            }
            hsv_to_rgb(rung_h, 180, rung_v, &r, &g, &b);
            for (int rx = min_x + 1; rx < max_x; rx++) {
                fb_add_pixel(rx, y, r, g, b, W, H);
            }
        }
    }

    /* Output framebuffer */
    for (int x = 0; x < W; x++)
        for (int y = 0; y < H; y++)
            set_pixel(x, y, fb_r[x][y], fb_g[x][y], fb_b[x][y]);

    draw();
}
