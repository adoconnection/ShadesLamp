#include "api.h"

/* ---- Metadata JSON ---- */
static const char META[] =
    "{\"name\":\"Rainbow Comet\","
    "\"desc\":\"Bright comet heads rise along columns leaving rainbow fading trails\","
    "\"params\":["
        "{\"id\":0,\"name\":\"Speed\",\"type\":\"int\","
         "\"min\":1,\"max\":10,\"default\":5,"
         "\"desc\":\"Movement speed of the comets\"},"
        "{\"id\":1,\"name\":\"Trail\",\"type\":\"int\","
         "\"min\":5,\"max\":40,\"default\":20,"
         "\"desc\":\"Length of the fading trail\"},"
        "{\"id\":2,\"name\":\"Brightness\",\"type\":\"int\","
         "\"min\":1,\"max\":255,\"default\":220,"
         "\"desc\":\"Overall brightness\"}"
    "]}";

EXPORT(get_meta_ptr)
int get_meta_ptr(void) { return (int)META; }

EXPORT(get_meta_len)
int get_meta_len(void) { return sizeof(META) - 1; }

/* ---- PRNG (xorshift32) ---- */
static uint32_t rng_state = 54321;

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

/* Framebuffer for trail fading (stores hue per pixel + brightness) */
static uint8_t fb_r[MAX_W][MAX_H];
static uint8_t fb_g[MAX_W][MAX_H];
static uint8_t fb_b[MAX_W][MAX_H];

/* Comet head position per column (fractional: upper 16 bits = integer y) */
static int comet_y[MAX_W]; /* position in 8.8 fixed point */
static uint8_t comet_hue[MAX_W]; /* hue offset per column */
static uint32_t tick_acc;

EXPORT(init)
void init(void) {
    for (int x = 0; x < MAX_W; x++) {
        for (int y = 0; y < MAX_H; y++) {
            fb_r[x][y] = 0;
            fb_g[x][y] = 0;
            fb_b[x][y] = 0;
        }
        comet_y[x] = 0;
        comet_hue[x] = 0;
    }
    tick_acc = 0;
    rng_state = 98765;
}

EXPORT(update)
void update(int tick_ms) {
    int speed  = get_param_i32(0);
    int trail  = get_param_i32(1);
    int bright = get_param_i32(2);
    int W = get_width();
    int H = get_height();

    if (W > MAX_W) W = MAX_W;
    if (H > MAX_H) H = MAX_H;
    if (W < 1) W = 1;
    if (H < 1) H = 1;

    rng_state ^= (uint32_t)tick_ms;
    tick_acc += (uint32_t)tick_ms;

    /* Fade factor: trail length controls how fast pixels dim.
       Longer trail = less dimming per frame. */
    int fade = 280 / (trail + 1);
    if (fade < 2) fade = 2;
    if (fade > 60) fade = 60;

    /* Dim the framebuffer (trail effect) */
    for (int x = 0; x < W; x++) {
        for (int y = 0; y < H; y++) {
            int r = fb_r[x][y];
            int g = fb_g[x][y];
            int b = fb_b[x][y];
            r -= fade; if (r < 0) r = 0;
            g -= fade; if (g < 0) g = 0;
            b -= fade; if (b < 0) b = 0;
            fb_r[x][y] = (uint8_t)r;
            fb_g[x][y] = (uint8_t)g;
            fb_b[x][y] = (uint8_t)b;
        }
    }

    /* Move comets upward */
    /* Speed controls how many pixels to move per frame (in 8.8 fixed point) */
    int step = speed * 40 + 30; /* roughly 70..430 in fixed 8.8 => ~0.27..1.68 pixels/frame */

    for (int x = 0; x < W; x++) {
        comet_y[x] += step;
        int yy = (comet_y[x] >> 8) % H; /* wrap around (cylinder) */

        /* Advance hue for rainbow effect */
        comet_hue[x] = (uint8_t)((tick_acc / (12 - speed)) + x * 25);

        /* Draw comet head (bright core spanning ~2 pixels) */
        int r, g, b;
        hsv_to_rgb(comet_hue[x], 255, bright, &r, &g, &b);

        /* Main pixel */
        int px_r = fb_r[x][yy] + r;
        int px_g = fb_g[x][yy] + g;
        int px_b = fb_b[x][yy] + b;
        fb_r[x][yy] = (uint8_t)(px_r > 255 ? 255 : px_r);
        fb_g[x][yy] = (uint8_t)(px_g > 255 ? 255 : px_g);
        fb_b[x][yy] = (uint8_t)(px_b > 255 ? 255 : px_b);

        /* Secondary pixel (slightly dimmer, one below head) */
        int yy2 = (yy + H - 1) % H;
        int dim = bright * 2 / 3;
        hsv_to_rgb(comet_hue[x] + 10, 255, dim, &r, &g, &b);
        px_r = fb_r[x][yy2] + r;
        px_g = fb_g[x][yy2] + g;
        px_b = fb_b[x][yy2] + b;
        fb_r[x][yy2] = (uint8_t)(px_r > 255 ? 255 : px_r);
        fb_g[x][yy2] = (uint8_t)(px_g > 255 ? 255 : px_g);
        fb_b[x][yy2] = (uint8_t)(px_b > 255 ? 255 : px_b);
    }

    /* Output framebuffer */
    for (int x = 0; x < W; x++) {
        for (int y = 0; y < H; y++) {
            set_pixel(x, y, fb_r[x][y], fb_g[x][y], fb_b[x][y]);
        }
    }

    draw();
}
