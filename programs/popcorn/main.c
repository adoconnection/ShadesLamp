#include "api.h"

/* ---- Metadata JSON ---- */
static const char META[] =
    "{\"name\":\"Popcorn\","
    "\"desc\":\"Particles pop up from the bottom and arc back down with gravity\","
    "\"params\":["
        "{\"id\":0,\"name\":\"Count\",\"type\":\"int\","
         "\"min\":1,\"max\":20,\"default\":8,"
         "\"desc\":\"Number of popcorn particles\"},"
        "{\"id\":1,\"name\":\"Gravity\",\"type\":\"int\","
         "\"min\":1,\"max\":20,\"default\":10,"
         "\"desc\":\"Gravity strength (higher = falls faster)\"},"
        "{\"id\":2,\"name\":\"Brightness\",\"type\":\"int\","
         "\"min\":1,\"max\":255,\"default\":220,"
         "\"desc\":\"Overall brightness\"}"
    "]}";

EXPORT(get_meta_ptr)
int get_meta_ptr(void) { return (int)META; }

EXPORT(get_meta_len)
int get_meta_len(void) { return sizeof(META) - 1; }

/* ---- PRNG (xorshift32) ---- */
static uint32_t rng_state = 31337;

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

/* ---- Particle state ---- */
#define MAX_PARTICLES 20

static float pop_x[MAX_PARTICLES];       /* horizontal position (float for smooth drift) */
static float pop_y[MAX_PARTICLES];       /* vertical position (0 = bottom) */
static float pop_vx[MAX_PARTICLES];      /* horizontal velocity */
static float pop_vy[MAX_PARTICLES];      /* vertical velocity */
static int   pop_hue[MAX_PARTICLES];     /* color hue */
static int   pop_active[MAX_PARTICLES];  /* is this particle alive */

/* Fade buffer for trails */
#define MAX_W 64
#define MAX_H 64

static uint8_t fb_r[MAX_W * MAX_H];
static uint8_t fb_g[MAX_W * MAX_H];
static uint8_t fb_b[MAX_W * MAX_H];

static void restart_particle(int i, int W, int H) {
    pop_x[i] = (float)random_range(0, W);
    pop_y[i] = 0.0f;

    /* Horizontal drift: random lean */
    float range = (float)(W * H + W * 2);
    int raw = random_range(-(int)range, (int)range + 1);
    pop_vx[i] = (float)raw / 256.0f;

    /* If at edge and moving outward, flip direction */
    if ((pop_x[i] < 1.0f && pop_vx[i] < 0.0f) ||
        (pop_x[i] > (float)(W - 2) && pop_vx[i] > 0.0f)) {
        pop_vx[i] = -pop_vx[i];
    }

    /* Upward launch velocity: proportional to height */
    pop_vy[i] = (float)(random8() * 8 + H * 10) / 256.0f;

    pop_hue[i] = random8();
    pop_active[i] = 1;
}

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

    /* Initialize particles at random positions */
    for (int i = 0; i < MAX_PARTICLES; i++) {
        pop_x[i] = (float)random_range(0, W);
        pop_y[i] = (float)random_range(0, H);
        pop_vx[i] = 0.0f;
        pop_vy[i] = -1.0f;  /* falling initially, will restart */
        pop_hue[i] = random8();
        pop_active[i] = 1;
    }
}

/* Saturating subtract */
static uint8_t qsub(uint8_t a, uint8_t b) {
    return a > b ? a - b : 0;
}

/* Absolute value for float */
static float f_abs(float x) {
    return x < 0.0f ? -x : x;
}

EXPORT(update)
void update(int tick_ms) {
    int count      = get_param_i32(0);
    int gravity_p  = get_param_i32(1);
    int bright     = get_param_i32(2);

    int W = get_width();
    int H = get_height();
    if (W > MAX_W) W = MAX_W;
    if (H > MAX_H) H = MAX_H;
    if (W < 1) W = 1;
    if (H < 1) H = 1;
    if (count > MAX_PARTICLES) count = MAX_PARTICLES;
    if (count < 1) count = 1;

    rng_state ^= (uint32_t)tick_ms;

    /* Gravity from param: 1-20, map to 0.02..0.40 per frame */
    float gravity = (float)gravity_p * 0.02f;

    /* Fade existing pixels (trail effect like GyverLamp's fadeToBlackBy 60) */
    for (int i = 0; i < W * H; i++) {
        fb_r[i] = qsub(fb_r[i], 60);
        fb_g[i] = qsub(fb_g[i], 60);
        fb_b[i] = qsub(fb_b[i], 60);
    }

    /* Update each particle */
    for (int i = 0; i < count; i++) {
        /* Apply horizontal movement */
        pop_x[i] += pop_vx[i];

        /* Wrap horizontally (cylinder) */
        if (pop_x[i] >= (float)(W - 1))
            pop_x[i] -= (float)(W - 1);
        if (pop_x[i] < 0.0f)
            pop_x[i] += (float)(W - 1);

        /* Apply vertical movement */
        pop_y[i] += pop_vy[i];

        /* Bounce off ceiling */
        if (pop_y[i] > (float)(H - 1)) {
            pop_y[i] = (float)(H - 1) * 2.0f - pop_y[i];
            pop_vy[i] = -pop_vy[i];
        }

        /* Bounce off floor with energy loss */
        if (pop_y[i] < 0.0f && pop_vy[i] < -0.7f) {
            pop_vy[i] = (-pop_vy[i]) * 0.9375f;
            pop_y[i] = -pop_y[i];
        }

        /* Settled on the floor? Restart */
        if (pop_y[i] <= -1.0f) {
            restart_particle(i, W, H);
        }

        /* Apply gravity (pulls down) */
        pop_vy[i] -= gravity;

        /* Apply viscosity/air resistance */
        pop_vx[i] *= 0.875f;
        pop_vy[i] *= 0.875f;

        /* Determine pixel position */
        int px = (int)pop_x[i];
        int py = (int)pop_y[i];
        if (px < 0) px = 0;
        if (px >= W) px = W - 1;
        if (py < 0) py = 0;
        if (py >= H) py = H - 1;

        /* Color: use palette-like coloring.
         * At the apex (near-zero vertical speed) show gray,
         * otherwise show the particle's hue color. Like GyverLamp. */
        int r, g, b;
        if (f_abs(pop_vy[i]) < 0.1f) {
            /* Near apex: grayish white */
            int gray = bright * 3 / 4;
            r = gray; g = gray; b = gray;
        } else {
            hsv_to_rgb(pop_hue[i], 255, bright, &r, &g, &b);
        }

        /* Write to framebuffer */
        int idx = py * W + px;
        if (idx >= 0 && idx < W * H) {
            /* Additive blend: take max of existing and new */
            if (r > fb_r[idx]) fb_r[idx] = (uint8_t)r;
            if (g > fb_g[idx]) fb_g[idx] = (uint8_t)g;
            if (b > fb_b[idx]) fb_b[idx] = (uint8_t)b;
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
