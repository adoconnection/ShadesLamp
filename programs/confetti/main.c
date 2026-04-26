#include "api.h"

/* ---- Metadata JSON ---- */
static const char META[] =
    "{\"name\":\"Confetti\","
    "\"desc\":\"Random sparkles that fade out — classic confetti effect\","
    "\"params\":["
        "{\"id\":0,\"name\":\"Density\",\"type\":\"int\","
         "\"min\":1,\"max\":50,\"default\":20,"
         "\"desc\":\"How many new sparkles appear per frame\"},"
        "{\"id\":1,\"name\":\"Fade\",\"type\":\"int\","
         "\"min\":1,\"max\":50,\"default\":10,"
         "\"desc\":\"How quickly sparkles fade (higher = faster fade)\"},"
        "{\"id\":2,\"name\":\"Brightness\",\"type\":\"int\","
         "\"min\":1,\"max\":255,\"default\":220,"
         "\"desc\":\"Maximum sparkle brightness\"}"
    "]}";

EXPORT(get_meta_ptr)
int get_meta_ptr(void) { return (int)META; }

EXPORT(get_meta_len)
int get_meta_len(void) { return sizeof(META) - 1; }

/* ---- PRNG (xorshift32) ---- */
static uint32_t rng_state = 55391;

static uint32_t rng_next(void) {
    uint32_t x = rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng_state = x;
    return x;
}

static int random8(void) {
    return (int)(rng_next() & 0xFF);
}

static int random_range(int lo, int hi) {
    if (lo >= hi) return lo;
    return lo + (int)(rng_next() % (uint32_t)(hi - lo));
}

/* ---- HSV to RGB ---- */
static void hsv_to_rgb(int h, int s, int v, int *r, int *g, int *b) {
    if (s == 0) {
        *r = *g = *b = v;
        return;
    }
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

/* ---- Framebuffer for fade effect ---- */
#define MAX_W 64
#define MAX_H 64

static uint8_t fb_r[MAX_W * MAX_H];
static uint8_t fb_g[MAX_W * MAX_H];
static uint8_t fb_b[MAX_W * MAX_H];

EXPORT(init)
void init(void) {
    int W = get_width();
    int H = get_height();
    if (W > MAX_W) W = MAX_W;
    if (H > MAX_H) H = MAX_H;

    /* Clear framebuffer */
    for (int i = 0; i < W * H; i++) {
        fb_r[i] = 0;
        fb_g[i] = 0;
        fb_b[i] = 0;
    }
}

/* Saturating subtract for fade */
static uint8_t qsub(uint8_t val, uint8_t sub) {
    return val > sub ? val - sub : 0;
}

EXPORT(update)
void update(int tick_ms) {
    int density = get_param_i32(0);
    int fade    = get_param_i32(1);
    int bright  = get_param_i32(2);

    int W = get_width();
    int H = get_height();
    if (W > MAX_W) W = MAX_W;
    if (H > MAX_H) H = MAX_H;
    if (W < 1) W = 1;
    if (H < 1) H = 1;

    rng_state ^= (uint32_t)tick_ms;

    /* Fade all pixels toward black.
     * fade param 1-50 maps to fade step. Higher = faster fade.
     * GyverLamp uses dimAll(256 - FADE_OUT_SPEED) where FADE_OUT_SPEED=70.
     * We use a direct subtraction approach for clarity.
     */
    int fade_step = fade * 3;  /* range 3..150 */
    if (fade_step > 255) fade_step = 255;

    for (int i = 0; i < W * H; i++) {
        fb_r[i] = qsub(fb_r[i], (uint8_t)fade_step);
        fb_g[i] = qsub(fb_g[i], (uint8_t)fade_step);
        fb_b[i] = qsub(fb_b[i], (uint8_t)fade_step);
    }

    /* Spawn new sparkles */
    for (int i = 0; i < density; i++) {
        int x = random_range(0, W);
        int y = random_range(0, H);
        int idx = y * W + x;

        /* Only spawn if pixel is dark (like GyverLamp) */
        if (fb_r[idx] < 10 && fb_g[idx] < 10 && fb_b[idx] < 10) {
            int r, g, b;
            int hue = random8();
            hsv_to_rgb(hue, 255, bright, &r, &g, &b);
            fb_r[idx] = (uint8_t)r;
            fb_g[idx] = (uint8_t)g;
            fb_b[idx] = (uint8_t)b;
        }
    }

    /* Render framebuffer to display */
    for (int x = 0; x < W; x++) {
        for (int y = 0; y < H; y++) {
            int idx = y * W + x;
            set_pixel(x, y, fb_r[idx], fb_g[idx], fb_b[idx]);
        }
    }

    draw();
}
