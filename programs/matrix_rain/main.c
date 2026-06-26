#include "api.h"

/* ---- Metadata JSON ---- */
static const char META[] =
    "{\"name\":\"Matrix Rain\","
    "\"desc\":\"Falling code rain — Matrix green, colourful, rainbow or white\","
    "\"params\":["
        "{\"id\":0,\"name\":\"Style\",\"type\":\"select\","
         "\"options\":[\"Matrix\",\"Colored\",\"Rainbow\",\"White\"],\"default\":0,"
         "\"desc\":\"Rain colour style\"},"
        "{\"id\":1,\"name\":\"Density\",\"type\":\"int\","
         "\"min\":1,\"max\":30,\"default\":10,"
         "\"desc\":\"How often new drops spawn (more=denser rain)\"},"
        "{\"id\":2,\"name\":\"Speed\",\"type\":\"int\","
         "\"min\":1,\"max\":10,\"default\":5,"
         "\"desc\":\"How fast drops fall\"},"
        "{\"id\":3,\"name\":\"Tail\",\"type\":\"int\","
         "\"min\":1,\"max\":20,\"default\":8,"
         "\"desc\":\"Trail length\"},"
        "{\"id\":4,\"name\":\"Brightness\",\"type\":\"int\","
         "\"min\":1,\"max\":255,\"default\":200,"
         "\"desc\":\"Overall brightness\"}"
    "]}";

EXPORT(get_meta_ptr)
int get_meta_ptr(void) { return (int)META; }

EXPORT(get_meta_len)
int get_meta_len(void) { return sizeof(META) - 1; }

/* ---- PRNG (xorshift32) ---- */
static uint32_t rng_state = 48271;
static uint32_t rng_next(void) {
    uint32_t x = rng_state;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    rng_state = x; return x;
}
static int random8(void) { return (int)(rng_next() & 0xFF); }
static int random_range(int lo, int hi) {
    if (lo >= hi) return lo;
    return lo + (int)(rng_next() % (uint32_t)(hi - lo));
}

/* ---- HSV to RGB (h,s,v: 0-255) ---- */
static void hsv_to_rgb(int h, int s, int v, int *r, int *g, int *b) {
    if (s == 0) { *r = v; *g = v; *b = v; return; }
    h &= 0xFF;
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

/* ---- Framebuffer for fade trails ---- */
#define MAX_W 64
#define MAX_H 64
static uint8_t fb_val[MAX_W][MAX_H];   /* per-pixel trail intensity */

/* ---- Per-column drop state ---- */
#define MAX_DROPS 64
static int drop_y[MAX_DROPS];          /* head Y position in 1/256 units (top=H-1) */
static int drop_speed[MAX_DROPS];      /* speed in 1/256 units per tick */
static uint8_t drop_hue[MAX_DROPS];    /* per-column hue (for coloured styles) */
static uint8_t drop_active[MAX_DROPS];

EXPORT(init)
void init(void) {
    rng_state = 48271;
    for (int x = 0; x < MAX_W; x++)
        for (int y = 0; y < MAX_H; y++)
            fb_val[x][y] = 0;
    for (int i = 0; i < MAX_DROPS; i++) drop_active[i] = 0;
}

static uint8_t qsub(uint8_t a, uint8_t b) { return (a > b) ? (uint8_t)(a - b) : 0; }

EXPORT(update)
void update(int tick_ms) {
    int style   = get_param_i32(0);   /* 0 matrix, 1 colored, 2 rainbow, 3 white */
    int density = get_param_i32(1);   /* 1-30 */
    int speed   = get_param_i32(2);   /* 1-10 */
    int tail    = get_param_i32(3);   /* 1-20 */
    int bright  = get_param_i32(4);   /* 1-255 */
    int W = get_width();
    int H = get_height();
    if (W > MAX_W) W = MAX_W;
    if (H > MAX_H) H = MAX_H;
    if (W < 1) W = 1;
    if (H < 1) H = 1;
    if (tail < 1) tail = 8;            /* guard saves predating this param */

    rng_state ^= (uint32_t)tick_ms;

    /* Fade all trails — longer Tail = slower fade */
    int fade_amount = (int)(72.0f / (float)tail);
    if (fade_amount < 3)  fade_amount = 3;
    if (fade_amount > 90) fade_amount = 90;
    for (int x = 0; x < W; x++)
        for (int y = 0; y < H; y++)
            fb_val[x][y] = qsub(fb_val[x][y], (uint8_t)fade_amount);

    /* Move existing drops and paint head (drops fall from top Y=H-1 to bottom) */
    int speed_inc = speed * 60;
    for (int i = 0; i < W && i < MAX_DROPS; i++) {
        if (!drop_active[i]) {
            if (random_range(0, 100) < density) {
                drop_active[i] = 1;
                drop_y[i] = (H - 1) << 8;
                drop_speed[i] = speed_inc + random_range(-20, 20);
                if (drop_speed[i] < 30) drop_speed[i] = 30;
                /* assign the column's hue for coloured styles */
                if (style == 2) drop_hue[i] = (uint8_t)((i * 256) / W);   /* Rainbow by column */
                else            drop_hue[i] = (uint8_t)random8();          /* Colored: random */
            }
            continue;
        }

        int iy = drop_y[i] >> 8;
        if (iy >= 0 && iy < H) fb_val[i][iy] = 255;

        drop_y[i] -= drop_speed[i];
        int new_iy = drop_y[i] >> 8;

        for (int fill = iy - 1; fill >= new_iy && fill >= 0; fill--)
            if (fill < H) fb_val[i][fill] = 255;

        if (new_iy < -5) drop_active[i] = 0;
    }

    /* Render framebuffer to LEDs */
    for (int x = 0; x < W; x++) {
        int hue = drop_hue[x];
        for (int y = 0; y < H; y++) {
            int val = fb_val[x][y];
            if (val == 0) { set_pixel(x, y, 0, 0, 0); continue; }

            int r, g, b;
            if (style == 0) {                         /* Matrix green */
                if (val >= 240) {
                    r = val * bright / 255; g = r; b = (val / 2) * bright / 255;
                } else if (val >= 140) {
                    r = 0; g = val * bright / 255; b = 0;
                } else {
                    r = 0; g = val * bright / 255; b = (val / 8) * bright / 255;
                }
            } else if (style == 3) {                  /* White */
                int v = val * bright / 255;
                r = g = b = v;
            } else {                                   /* Colored / Rainbow */
                if (val >= 240) {
                    hsv_to_rgb(hue, 80, 255, &r, &g, &b);  /* whitish head tinted */
                } else {
                    int sat = (val < 80) ? 255 : 220;
                    hsv_to_rgb(hue, sat, val, &r, &g, &b);
                }
                r = r * bright / 255; g = g * bright / 255; b = b * bright / 255;
            }

            set_pixel(x, y, r, g, b);
        }
    }

    draw();
}
