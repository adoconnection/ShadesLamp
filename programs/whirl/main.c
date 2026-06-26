#include "api.h"

/* ---- Metadata JSON ---- */
static const char META[] =
    "{\"name\":\"Whirl\","
    "\"desc\":\"Swirling flame vortex driven by noise field\","
    "\"params\":["
        "{\"id\":0,\"name\":\"Speed\",\"type\":\"int\","
         "\"min\":1,\"max\":10,\"default\":5,"
         "\"desc\":\"Animation speed\"},"
        "{\"id\":1,\"name\":\"Mode\",\"type\":\"int\","
         "\"min\":0,\"max\":1,\"default\":1,"
         "\"desc\":\"0=single color, 1=multicolor\"},"
        "{\"id\":2,\"name\":\"Hue\",\"type\":\"int\","
         "\"min\":0,\"max\":255,\"default\":0,"
         "\"desc\":\"Base hue (single color mode)\"},"
        "{\"id\":3,\"name\":\"Brightness\",\"type\":\"int\","
         "\"min\":50,\"max\":255,\"default\":255,"
         "\"desc\":\"Overall brightness\"}"
    "]}";

EXPORT(get_meta_ptr)
int get_meta_ptr(void) { return (int)META; }

EXPORT(get_meta_len)
int get_meta_len(void) { return sizeof(META) - 1; }

/* ---- PRNG ---- */
static uint32_t rng_state = 91735;

static uint32_t rng_next(void) {
    uint32_t x = rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng_state = x;
    return x;
}

static int rng_range(int lo, int hi) {
    if (lo >= hi) return lo;
    return lo + (int)(rng_next() % (uint32_t)(hi - lo));
}

/* ---- Noise (hash-based, similar to inoise8) ---- */
static int hash_i(int x) {
    x = ((x >> 16) ^ x) * 0x45d9f3b;
    x = ((x >> 16) ^ x) * 0x45d9f3b;
    x = (x >> 16) ^ x;
    return x;
}

static int hash3d(int x, int y, int z) {
    return (hash_i(x * 374761393 + y * 668265263 + z * 1274126177) & 0xFF);
}

static int lerp8(int a, int b, int t) {
    return a + ((b - a) * t >> 8);
}

static int smooth8(int t) {
    int t2 = (t * t) >> 8;
    int t3 = (t2 * t) >> 8;
    return (3 * t2 - 2 * t3);
}

/* 3D value noise with 8.8 fixed point input, returns 0-255 */
static int noise3d_val(int fx, int fy, int fz) {
    int ix = fx >> 8, iy = fy >> 8, iz = fz >> 8;
    int frx = smooth8(fx & 0xFF);
    int fry = smooth8(fy & 0xFF);
    int frz = smooth8(fz & 0xFF);

    int v000 = hash3d(ix, iy, iz);
    int v100 = hash3d(ix+1, iy, iz);
    int v010 = hash3d(ix, iy+1, iz);
    int v110 = hash3d(ix+1, iy+1, iz);
    int v001 = hash3d(ix, iy, iz+1);
    int v101 = hash3d(ix+1, iy, iz+1);
    int v011 = hash3d(ix, iy+1, iz+1);
    int v111 = hash3d(ix+1, iy+1, iz+1);

    int a0 = lerp8(v000, v100, frx);
    int a1 = lerp8(v010, v110, frx);
    int b0 = lerp8(v001, v101, frx);
    int b1 = lerp8(v011, v111, frx);

    int c0 = lerp8(a0, a1, fry);
    int c1 = lerp8(b0, b1, fry);

    return lerp8(c0, c1, frz);
}

/* ---- Sine/Cosine (native, mapped to sin8/cos8 range 0-255, centre 128) ---- */
static int sin8(int x) {
    return 128 + (int)(127.0f * m_sin((float)(x & 255) * (6.28318530f / 256.0f)));
}
static int cos8(int x) {
    return 128 + (int)(127.0f * m_cos((float)(x & 255) * (6.28318530f / 256.0f)));
}

/* ---- HSV to RGB (native host primitive) ---- */
static void hsv2rgb(int h, int s, int v, int *r, int *g, int *b) {
    int sat = s < 0 ? 0 : (s > 255 ? 255 : s);
    int val = v < 0 ? 0 : (v > 255 ? 255 : v);
    int c = m_hsv(h & 255, sat, val);
    *r = (c >> 16) & 255; *g = (c >> 8) & 255; *b = c & 255;
}

/* ---- Framebuffer for fade/trail effect ---- */
#define MAX_W 32
#define MAX_H 32
static uint8_t fb_r[MAX_W * MAX_H];
static uint8_t fb_g[MAX_W * MAX_H];
static uint8_t fb_b[MAX_W * MAX_H];

/* ---- Boids ---- */
#define NUM_BOIDS 20
#define FP_SHIFT 8
#define FP_ONE (1 << FP_SHIFT)

/* Boid positions in 8.8 fixed point */
static int32_t boid_x[NUM_BOIDS];
static int32_t boid_y[NUM_BOIDS];

/* Noise field offsets */
static int32_t ff_x, ff_y, ff_z;
static int hue_shift;

static int g_w, g_h;

static void fb_set(int x, int y, int r, int g, int b) {
    if (x < 0 || x >= g_w || y < 0 || y >= g_h) return;
    int idx = x * MAX_H + y;
    /* Additive blend, capped at 255 */
    int nr = fb_r[idx] + r; if (nr > 255) nr = 255;
    int ng = fb_g[idx] + g; if (ng > 255) ng = 255;
    int nb = fb_b[idx] + b; if (nb > 255) nb = 255;
    fb_r[idx] = nr;
    fb_g[idx] = ng;
    fb_b[idx] = nb;
}

/* Sub-pixel draw: boid position is 8.8 fixed point */
static void draw_boid(int fx, int fy, int r, int g, int b) {
    int ix = fx >> FP_SHIFT;
    int iy = fy >> FP_SHIFT;
    int frx = fx & 0xFF;
    int fry = fy & 0xFF;

    int w00 = ((256 - frx) * (256 - fry)) >> 8;
    int w10 = (frx * (256 - fry)) >> 8;
    int w01 = ((256 - frx) * fry) >> 8;
    int w11 = (frx * fry) >> 8;

    fb_set(ix,     iy,     (r * w00) >> 8, (g * w00) >> 8, (b * w00) >> 8);
    fb_set(ix + 1, iy,     (r * w10) >> 8, (g * w10) >> 8, (b * w10) >> 8);
    fb_set(ix,     iy + 1, (r * w01) >> 8, (g * w01) >> 8, (b * w01) >> 8);
    fb_set(ix + 1, iy + 1, (r * w11) >> 8, (g * w11) >> 8, (b * w11) >> 8);
}

EXPORT(init)
void init(void) {
    rng_state = 91735;
    for (int i = 0; i < MAX_W * MAX_H; i++) {
        fb_r[i] = 0; fb_g[i] = 0; fb_b[i] = 0;
    }
    ff_x = rng_next() & 0xFFFF;
    ff_y = rng_next() & 0xFFFF;
    ff_z = rng_next() & 0xFFFF;
    hue_shift = 0;
    g_w = 0; g_h = 0;
}

static int32_t prev_tick = 0;

EXPORT(update)
void update(int tick_ms) {
    int speed      = get_param_i32(0);
    int mode       = get_param_i32(1);
    int base_hue   = get_param_i32(2);
    int brightness = get_param_i32(3);
    int W = get_width();
    int H = get_height();

    if (W > MAX_W) W = MAX_W;
    if (H > MAX_H) H = MAX_H;
    if (W < 1) W = 1;
    if (H < 1) H = 1;

    /* Reinitialize boids if dimensions changed */
    if (W != g_w || H != g_h) {
        g_w = W;
        g_h = H;
        for (int i = 0; i < NUM_BOIDS; i++) {
            boid_x[i] = rng_range(0, W) << FP_SHIFT;
            boid_y[i] = 0;
        }
    }

    int delta_ms = tick_ms - prev_tick;
    prev_tick = tick_ms;
    if (delta_ms <= 0 || delta_ms > 200) delta_ms = 30;

    /* Fade all pixels (dimAll 240/256 ~ 94%) */
    int fade = 240;
    for (int i = 0; i < W * MAX_H; i++) {
        fb_r[i] = (fb_r[i] * fade) >> 8;
        fb_g[i] = (fb_g[i] * fade) >> 8;
        fb_b[i] = (fb_b[i] * fade) >> 8;
    }

    /* Noise field scale */
    int ff_scale = 26;

    /* Update boids */
    for (int i = 0; i < NUM_BOIDS; i++) {
        int bx = boid_x[i] >> FP_SHIFT;
        int by = boid_y[i] >> FP_SHIFT;

        /* Sample noise at boid position to get angle */
        int ioffset = ff_scale * bx;
        int joffset = ff_scale * by;
        int angle = noise3d_val(
            ff_x + (ioffset << 2),
            ff_y + (joffset << 2),
            ff_z
        );

        /* Convert angle to velocity (sin8/cos8 return 0-255, center at 128) */
        int vx = sin8(angle) - 128;   /* -128..127 */
        int vy = -(cos8(angle) - 128); /* -128..127, negated for upward flow */

        /* Scale velocity by speed */
        int vel_scale = speed * 2;
        boid_x[i] += (vx * vel_scale) >> 5;
        boid_y[i] += (vy * vel_scale) >> 5;

        /* Determine color */
        int r, g, b;
        if (mode == 0) {
            /* Single color mode */
            int sat = (base_hue == 0 && mode == 0) ? 255 : 255;
            hsv2rgb(base_hue, 255, brightness, &r, &g, &b);
        } else {
            /* Multicolor: hue from noise angle + progressive shift */
            int h = angle + hue_shift;
            hsv2rgb(h, 255, brightness, &r, &g, &b);
        }

        /* Draw boid with sub-pixel precision */
        draw_boid(boid_x[i], boid_y[i], r, g, b);

        /* Respawn boid if it left the screen */
        int px = boid_x[i] >> FP_SHIFT;
        int py = boid_y[i] >> FP_SHIFT;
        if (px < -1 || px >= W + 1 || py < -1 || py >= H + 1) {
            boid_x[i] = rng_range(0, W) << FP_SHIFT;
            boid_y[i] = 0;
        }
    }

    /* Hue shift for multicolor mode */
    hue_shift += (delta_ms > 5) ? 1 : 0;

    /* Advance noise field */
    ff_x += speed;
    ff_y += speed;
    ff_z += speed;

    /* Render framebuffer to display */
    for (int x = 0; x < W; x++) {
        for (int y = 0; y < H; y++) {
            int idx = x * MAX_H + y;
            set_pixel(x, y, fb_r[idx], fb_g[idx], fb_b[idx]);
        }
    }
    draw();
}
