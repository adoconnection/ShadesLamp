#include "api.h"

/* ---- Metadata JSON ---- */
static const char META[] =
    "{\"name\":\"Bouncing Balls\","
    "\"desc\":\"Balls with gravity bouncing off the bottom\","
    "\"params\":["
        "{\"id\":0,\"name\":\"Count\",\"type\":\"int\","
         "\"min\":1,\"max\":16,\"default\":5,"
         "\"desc\":\"Number of bouncing balls\"},"
        "{\"id\":1,\"name\":\"Brightness\",\"type\":\"int\","
         "\"min\":1,\"max\":255,\"default\":220,"
         "\"desc\":\"Overall brightness\"},"
        "{\"id\":2,\"name\":\"Hue\",\"type\":\"int\","
         "\"min\":0,\"max\":255,\"default\":0,"
         "\"desc\":\"Base hue (0 = rainbow)\"}"
    "]}";

EXPORT(get_meta_ptr)
int get_meta_ptr(void) { return (int)META; }

EXPORT(get_meta_len)
int get_meta_len(void) { return sizeof(META) - 1; }

/* ---- PRNG (xorshift32) ---- */
static uint32_t rng_state = 48271;

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

/* ---- Approximate square root (Newton's method) ---- */
static float f_sqrt(float x) {
    if (x <= 0.0f) return 0.0f;
    float guess = x * 0.5f;
    for (int i = 0; i < 10; i++) {
        guess = 0.5f * (guess + x / guess);
    }
    return guess;
}

/* ---- Ball state ---- */
#define MAX_BALLS 16

static float ball_pos_y[MAX_BALLS];     /* vertical position in [0..H-1] */
static float ball_speed_y[MAX_BALLS];   /* vertical velocity */
static int   ball_x[MAX_BALLS];         /* horizontal column */
static int   ball_hue[MAX_BALLS];       /* color hue 0-255 */
static float ball_cor[MAX_BALLS];       /* coefficient of restitution (bounciness) */
static int   ball_shift_ok[MAX_BALLS];  /* allow horizontal shift at apex */

/* Physics constants */
#define GRAVITY       9.81f
#define START_HEIGHT  1.0f

/* Fade buffer: store RGB per pixel for trail fading */
#define MAX_W 64
#define MAX_H 64
static uint8_t fb_r[MAX_W * MAX_H];
static uint8_t fb_g[MAX_W * MAX_H];
static uint8_t fb_b[MAX_W * MAX_H];

static int fb_w, fb_h;

static void fb_clear(int w, int h) {
    fb_w = w; fb_h = h;
    for (int i = 0; i < w * h; i++) {
        fb_r[i] = 0; fb_g[i] = 0; fb_b[i] = 0;
    }
}

static void fb_dim(int w, int h, int factor) {
    /* factor: 0-255, how much to keep. 200 = slow fade, 100 = fast fade */
    for (int i = 0; i < w * h; i++) {
        fb_r[i] = (uint8_t)((int)fb_r[i] * factor / 256);
        fb_g[i] = (uint8_t)((int)fb_g[i] * factor / 256);
        fb_b[i] = (uint8_t)((int)fb_b[i] * factor / 256);
    }
}

static void fb_set(int x, int y, int w, int r, int g, int b) {
    int idx = y * w + x;
    fb_r[idx] = (uint8_t)r;
    fb_g[idx] = (uint8_t)g;
    fb_b[idx] = (uint8_t)b;
}

static float impact_v0;
static int hue_offset;

EXPORT(init)
void init(void) {
    impact_v0 = f_sqrt(2.0f * GRAVITY * START_HEIGHT);
    hue_offset = 0;

    int W = get_width();
    int H = get_height();
    if (W > MAX_W) W = MAX_W;
    if (H > MAX_H) H = MAX_H;

    fb_clear(W, H);

    for (int i = 0; i < MAX_BALLS; i++) {
        ball_pos_y[i] = 0.0f;
        ball_speed_y[i] = impact_v0;
        ball_x[i] = random_range(0, W);
        ball_hue[i] = random8();
        ball_cor[i] = 0.90f - (float)i / (float)(MAX_BALLS * MAX_BALLS);
        if (ball_cor[i] < 0.5f) ball_cor[i] = 0.5f;
        ball_shift_ok[i] = 0;
    }
}

EXPORT(update)
void update(int tick_ms) {
    int count  = get_param_i32(0);
    int bright = get_param_i32(1);
    int hue_p  = get_param_i32(2);

    int W = get_width();
    int H = get_height();
    if (W > MAX_W) W = MAX_W;
    if (H > MAX_H) H = MAX_H;
    if (W < 1) W = 1;
    if (H < 1) H = 1;
    if (count > MAX_BALLS) count = MAX_BALLS;
    if (count < 1) count = 1;

    rng_state ^= (uint32_t)tick_ms;

    /* Advance hue offset slowly for rainbow mode */
    hue_offset++;

    /* Time step: ~33ms at 30fps, convert to seconds for physics */
    float dt = (float)tick_ms / 1000.0f;
    if (dt > 0.1f) dt = 0.1f;  /* clamp for safety */
    if (dt < 0.001f) dt = 0.016f;

    /* Fade existing pixels (trail effect) */
    fb_dim(W, H, 190);

    /* Update each ball */
    for (int i = 0; i < count; i++) {
        /* Apply gravity: v += g*dt, pos += v*dt */
        ball_speed_y[i] -= GRAVITY * dt;
        ball_pos_y[i] += ball_speed_y[i] * dt;

        /* Map position: ball_pos_y is in "meters" [0..START_HEIGHT], map to [0..H-1] */
        float pixel_y = ball_pos_y[i] * (float)(H - 1) / START_HEIGHT;

        /* Bounce off ground */
        if (ball_pos_y[i] < 0.0f) {
            ball_pos_y[i] = 0.0f;
            ball_speed_y[i] = -ball_speed_y[i] * ball_cor[i];

            /* If barely moving, relaunch */
            if (ball_speed_y[i] < 0.05f) {
                /* Randomize bounciness for next cycle */
                ball_cor[i] = 0.90f - (float)random_range(0, 9) / (float)(random_range(4, 9) * random_range(4, 9));
                if (ball_cor[i] < 0.5f) ball_cor[i] = 0.5f;
                ball_speed_y[i] = impact_v0;
                ball_shift_ok[i] = 1;
                ball_hue[i] = random8();
            }
            pixel_y = 0.0f;
        }

        /* Horizontal shift at apex */
        if (ball_shift_ok[i] && pixel_y >= (float)(H - 2)) {
            ball_shift_ok[i] = 0;
            if (ball_hue[i] & 0x01) {
                ball_x[i] = (ball_x[i] - 1 + W) % W;
            } else {
                ball_x[i] = (ball_x[i] + 1) % W;
            }
        }

        /* Clamp pixel position */
        int py = (int)pixel_y;
        if (py < 0) py = 0;
        if (py >= H) py = H - 1;

        /* Determine color */
        int r, g, b;
        int hue;
        if (hue_p == 0) {
            /* Rainbow mode: each ball gets its own shifting hue */
            hue = (ball_hue[i] + hue_offset) & 0xFF;
        } else {
            hue = (hue_p + i * 30) & 0xFF;
        }
        hsv_to_rgb(hue, 255, bright, &r, &g, &b);

        /* Draw ball into framebuffer */
        fb_set(ball_x[i], py, W, r, g, b);
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
