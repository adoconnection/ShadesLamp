#include "api.h"

/* ---- Metadata JSON ---- */
static const char META[] =
    "{\"name\":\"Clouds\","
    "\"desc\":\"Smooth cloud-like noise patterns\","
    "\"params\":["
        "{\"id\":0,\"name\":\"Speed\",\"type\":\"int\","
         "\"min\":1,\"max\":10,\"default\":5,"
         "\"desc\":\"Animation speed\"},"
        "{\"id\":1,\"name\":\"Scale\",\"type\":\"int\","
         "\"min\":1,\"max\":50,\"default\":20,"
         "\"desc\":\"Noise zoom level\"},"
        "{\"id\":2,\"name\":\"Brightness\",\"type\":\"int\","
         "\"min\":1,\"max\":255,\"default\":200,"
         "\"desc\":\"Overall brightness\"}"
    "]}";

EXPORT(get_meta_ptr)
int get_meta_ptr(void) { return (int)META; }

EXPORT(get_meta_len)
int get_meta_len(void) { return sizeof(META) - 1; }

/* ---- Simple PRNG (xorshift32) ---- */
static uint32_t rng_state = 73541;

static uint32_t rng_next(void) {
    uint32_t x = rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng_state = x;
    return x;
}

/* ---- Hash-based value noise ---- */
/* Simple integer hash for noise generation */
static int hash_i(int x) {
    x = ((x >> 16) ^ x) * 0x45d9f3b;
    x = ((x >> 16) ^ x) * 0x45d9f3b;
    x = (x >> 16) ^ x;
    return x;
}

/* 2D hash returning 0-255 */
static int hash2d(int x, int y) {
    return (hash_i(x * 374761393 + y * 668265263 + 1274126177) & 0xFF);
}

/* Linear interpolation: returns value in 0-255 range
   a, b are 0-255 values, t is 0-255 fraction */
static int lerp8(int a, int b, int t) {
    return a + ((b - a) * t >> 8);
}

/* Smoothstep approximation for t in 0-255 range -> 0-255 */
static int smooth8(int t) {
    /* 3t^2 - 2t^3, scaled to 0-255 */
    int t2 = (t * t) >> 8;
    int t3 = (t2 * t) >> 8;
    return (3 * t2 - 2 * t3);
}

/* Value noise 2D with fixed-point coordinates
   fx, fy are 8.8 fixed point (high byte = integer, low byte = fraction) */
static int value_noise2d(int fx, int fy) {
    int ix = fx >> 8;
    int iy = fy >> 8;
    int fracx = fx & 0xFF;
    int fracy = fy & 0xFF;

    /* Smooth the fractions */
    fracx = smooth8(fracx);
    fracy = smooth8(fracy);

    /* Get corner values */
    int v00 = hash2d(ix, iy);
    int v10 = hash2d(ix + 1, iy);
    int v01 = hash2d(ix, iy + 1);
    int v11 = hash2d(ix + 1, iy + 1);

    /* Bilinear interpolation */
    int top = lerp8(v00, v10, fracx);
    int bot = lerp8(v01, v11, fracx);
    return lerp8(top, bot, fracy);
}

/* Fractal noise (2 octaves) for richer patterns */
static int fractal_noise2d(int fx, int fy) {
    int n1 = value_noise2d(fx, fy);
    int n2 = value_noise2d(fx * 2, fy * 2);
    /* Weighted sum: 2/3 first octave + 1/3 second octave */
    return (n1 * 170 + n2 * 85) >> 8;
}

/* ---- Cloud color palette ---- */
/* Maps a noise value (0-255) to soft blue-white cloud colors */
static void cloud_color(int val, int brightness, int *r, int *g, int *b) {
    int r0, g0, b0;

    if (val < 85) {
        /* Deep sky blue to lighter blue */
        r0 = 30 + val;
        g0 = 50 + val;
        b0 = 120 + val;
    } else if (val < 170) {
        /* Lighter blue to near-white */
        int s = val - 85;
        r0 = 115 + s * 140 / 85;
        g0 = 135 + s * 120 / 85;
        b0 = 205 + s * 50 / 85;
    } else {
        /* Near-white to bright white */
        int s = val - 170;
        r0 = 255;
        g0 = 255;
        b0 = 255 - s / 4;
    }

    if (r0 > 255) r0 = 255;
    if (g0 > 255) g0 = 255;
    if (b0 > 255) b0 = 255;

    *r = r0 * brightness / 255;
    *g = g0 * brightness / 255;
    *b = b0 * brightness / 255;
}

/* ---- Smoothing buffer for temporal blending ---- */
#define MAX_W 64
#define MAX_H 64
static uint8_t prev_frame[MAX_W * MAX_H];

EXPORT(init)
void init(void) {
    rng_state = 73541;
    for (int i = 0; i < MAX_W * MAX_H; i++)
        prev_frame[i] = 128;
}

EXPORT(update)
void update(int tick_ms) {
    int speed      = get_param_i32(0);
    int scale      = get_param_i32(1);
    int brightness = get_param_i32(2);
    int W = get_width();
    int H = get_height();

    if (W > MAX_W) W = MAX_W;
    if (H > MAX_H) H = MAX_H;
    if (W < 1) W = 1;
    if (H < 1) H = 1;

    /* Time offset for animation (speed 1-10 mapped to slow-fast) */
    int time_offset = (tick_ms * speed) / 8;

    /* Scale factor: higher scale = more zoomed in (larger features) */
    /* scale param 1-50 -> noise scale multiplier */
    int noise_scale = 512 / (scale + 1) + 3;

    for (int x = 0; x < W; x++) {
        for (int y = 0; y < H; y++) {
            /* Compute noise coordinates with time animation */
            int nx = x * noise_scale + (time_offset >> 1);
            int ny = y * noise_scale + (time_offset >> 2);

            /* Get fractal noise value */
            int val = fractal_noise2d(nx, ny);

            /* Temporal smoothing: blend with previous frame for smoother look */
            int idx = x * MAX_H + y;
            int old_val = prev_frame[idx];
            /* 75% old + 25% new for very smooth clouds */
            val = (old_val * 192 + val * 64) >> 8;
            if (val > 255) val = 255;
            if (val < 0) val = 0;
            prev_frame[idx] = (uint8_t)val;

            int r, g, b;
            cloud_color(val, brightness, &r, &g, &b);
            set_pixel(x, y, r, g, b);
        }
    }

    draw();
}
