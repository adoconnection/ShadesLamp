#include "api.h"

/* ---- Metadata JSON ---- */
static const char META[] =
    "{\"name\":\"Forest\","
    "\"desc\":\"Deep forest noise with green palette\","
    "\"params\":["
        "{\"id\":0,\"name\":\"Speed\",\"type\":\"int\","
         "\"min\":1,\"max\":10,\"default\":4,"
         "\"desc\":\"Animation speed\"},"
        "{\"id\":1,\"name\":\"Scale\",\"type\":\"int\","
         "\"min\":1,\"max\":50,\"default\":25,"
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
static int hash_i(int x) {
    x = ((x >> 16) ^ x) * 0x45d9f3b;
    x = ((x >> 16) ^ x) * 0x45d9f3b;
    x = (x >> 16) ^ x;
    return x;
}

static int hash2d(int x, int y) {
    return (hash_i(x * 374761393 + y * 668265263 + 1274126177) & 0xFF);
}

static int lerp8(int a, int b, int t) {
    return a + ((b - a) * t >> 8);
}

static int smooth8(int t) {
    int t2 = (t * t) >> 8;
    int t3 = (t2 * t) >> 8;
    return (3 * t2 - 2 * t3);
}

static int value_noise2d(int fx, int fy) {
    int ix = fx >> 8;
    int iy = fy >> 8;
    int fracx = fx & 0xFF;
    int fracy = fy & 0xFF;

    fracx = smooth8(fracx);
    fracy = smooth8(fracy);

    int v00 = hash2d(ix, iy);
    int v10 = hash2d(ix + 1, iy);
    int v01 = hash2d(ix, iy + 1);
    int v11 = hash2d(ix + 1, iy + 1);

    int top = lerp8(v00, v10, fracx);
    int bot = lerp8(v01, v11, fracx);
    return lerp8(top, bot, fracy);
}

static int fractal_noise2d(int fx, int fy) {
    int n1 = value_noise2d(fx, fy);
    int n2 = value_noise2d(fx * 2, fy * 2);
    return (n1 * 170 + n2 * 85) >> 8;
}

/* ---- Forest color palette ---- */
/* Mimics FastLED ForestColors_p: dark greens, olive, sea green, lime */
static void forest_color(int val, int brightness, int *r, int *g, int *b) {
    int r0, g0, b0;

    if (val < 51) {
        /* Dark green (0,100,0) -> Dark olive green (85,107,47) */
        int t = val * 5;  /* 0-255 */
        r0 = (0 * (255 - t) + 85 * t) >> 8;
        g0 = (100 * (255 - t) + 107 * t) >> 8;
        b0 = (0 * (255 - t) + 47 * t) >> 8;
    } else if (val < 102) {
        /* Dark olive green (85,107,47) -> Forest green (34,139,34) */
        int t = (val - 51) * 5;
        r0 = (85 * (255 - t) + 34 * t) >> 8;
        g0 = (107 * (255 - t) + 139 * t) >> 8;
        b0 = (47 * (255 - t) + 34 * t) >> 8;
    } else if (val < 153) {
        /* Forest green (34,139,34) -> Sea green (46,139,87) */
        int t = (val - 102) * 5;
        r0 = (34 * (255 - t) + 46 * t) >> 8;
        g0 = 139;
        b0 = (34 * (255 - t) + 87 * t) >> 8;
    } else if (val < 204) {
        /* Sea green (46,139,87) -> Lime green (50,205,50) */
        int t = (val - 153) * 5;
        r0 = (46 * (255 - t) + 50 * t) >> 8;
        g0 = (139 * (255 - t) + 205 * t) >> 8;
        b0 = (87 * (255 - t) + 50 * t) >> 8;
    } else {
        /* Lime green (50,205,50) -> Yellow green (154,205,50) */
        int t = (val - 204) * 5;
        r0 = (50 * (255 - t) + 154 * t) >> 8;
        g0 = 205;
        b0 = 50;
    }

    if (r0 > 255) r0 = 255;
    if (g0 > 255) g0 = 255;
    if (b0 > 255) b0 = 255;
    if (r0 < 0) r0 = 0;
    if (g0 < 0) g0 = 0;
    if (b0 < 0) b0 = 0;

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

    int time_offset = (tick_ms * speed) / 8;

    int noise_scale = 512 / (scale + 1) + 3;

    for (int x = 0; x < W; x++) {
        for (int y = 0; y < H; y++) {
            int nx = x * noise_scale + (time_offset >> 1);
            int ny = y * noise_scale + (time_offset >> 2);

            int val = fractal_noise2d(nx, ny);

            int idx = x * MAX_H + y;
            int old_val = prev_frame[idx];
            val = (old_val * 192 + val * 64) >> 8;
            if (val > 255) val = 255;
            if (val < 0) val = 0;
            prev_frame[idx] = (uint8_t)val;

            int r, g, b;
            forest_color(val, brightness, &r, &g, &b);
            set_pixel(x, y, r, g, b);
        }
    }

    draw();
}
