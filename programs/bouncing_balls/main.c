#include "api.h"

/* ---- Metadata JSON ---- */
static const char META[] =
    "{\"name\":\"Bouncing Balls\","
    "\"desc\":\"Balls bouncing with gravity, angle and size\","
    "\"params\":["
        "{\"id\":0,\"name\":\"Count\",\"type\":\"int\","
         "\"min\":1,\"max\":16,\"default\":6,"
         "\"desc\":\"Number of bouncing balls\"},"
        "{\"id\":1,\"name\":\"Size\",\"type\":\"int\","
         "\"min\":1,\"max\":6,\"default\":2,"
         "\"desc\":\"Ball radius in pixels\"},"
        "{\"id\":2,\"name\":\"Gravity\",\"type\":\"int\","
         "\"min\":10,\"max\":120,\"default\":45,"
         "\"desc\":\"Higher = faster, snappier bounces\"},"
        "{\"id\":3,\"name\":\"Angle\",\"type\":\"int\","
         "\"min\":0,\"max\":100,\"default\":35,"
         "\"desc\":\"Sideways kick (0 = straight up)\"},"
        "{\"id\":4,\"name\":\"Brightness\",\"type\":\"int\","
         "\"min\":1,\"max\":255,\"default\":220,"
         "\"desc\":\"Overall brightness\"},"
        "{\"id\":5,\"name\":\"Hue\",\"type\":\"int\","
         "\"min\":0,\"max\":255,\"default\":0,"
         "\"desc\":\"Base hue (0 = rainbow)\"},"
        "{\"id\":6,\"name\":\"Walls\",\"type\":\"select\","
         "\"options\":[\"Wrap around\",\"Bounce\"],"
         "\"default\":0,"
         "\"desc\":\"Wrap around the cylinder or bounce off sides\"}"
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

/* random float in [0,1) */
static float random_float(void) {
    return (float)(rng_next() & 0xFFFF) / 65536.0f;
}

/* random float in [-1,1) */
static float random_signed(void) {
    return random_float() * 2.0f - 1.0f;
}

/* ---- HSV to RGB ---- */
static void hsv_to_rgb(int h, int s, int v, int *r, int *g, int *b) {
    if (s == 0) {
        *r = *g = *b = v;
        return;
    }
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

/* ---- Light square root (Newton, few iters; inputs are small distances) ---- */
static float f_sqrt(float x) {
    if (x <= 0.0f) return 0.0f;
    float guess = x > 1.0f ? x * 0.5f : 1.0f;
    for (int i = 0; i < 5; i++) {
        guess = 0.5f * (guess + x / guess);
    }
    return guess;
}

/* ---- Ball state (all in pixel space; Y=0 is the bottom) ---- */
#define MAX_BALLS 16

static float ball_x[MAX_BALLS];     /* horizontal position */
static float ball_y[MAX_BALLS];     /* vertical position, 0 = floor */
static float ball_vx[MAX_BALLS];    /* horizontal velocity (px/s) */
static float ball_vy[MAX_BALLS];    /* vertical velocity (px/s) */
static int   ball_hue[MAX_BALLS];   /* color hue 0-255 */
static float ball_cor[MAX_BALLS];   /* coefficient of restitution */
static float ball_rest[MAX_BALLS];  /* remaining pause timer at the floor (s) */

/* ---- Framebuffer (for glow + trail fading) ---- */
#define MAX_W 64
#define MAX_H 64
static uint8_t fb_r[MAX_W * MAX_H];
static uint8_t fb_g[MAX_W * MAX_H];
static uint8_t fb_b[MAX_W * MAX_H];

static int32_t prev_tick;

static void fb_clear(int w, int h) {
    for (int i = 0; i < w * h; i++) {
        fb_r[i] = 0; fb_g[i] = 0; fb_b[i] = 0;
    }
}

static void fb_dim(int w, int h, int factor) {
    /* factor: 0-256, fraction of brightness kept each frame */
    for (int i = 0; i < w * h; i++) {
        fb_r[i] = (uint8_t)((int)fb_r[i] * factor / 256);
        fb_g[i] = (uint8_t)((int)fb_g[i] * factor / 256);
        fb_b[i] = (uint8_t)((int)fb_b[i] * factor / 256);
    }
}

/* Additive (saturating) blend of a pixel scaled by coverage 0..1 */
static void fb_add(int x, int y, int w, int h, int r, int g, int b, float cov) {
    if (x < 0 || y < 0 || x >= w || y >= h) return;
    int idx = y * w + x;
    int nr = (int)fb_r[idx] + (int)(r * cov);
    int ng = (int)fb_g[idx] + (int)(g * cov);
    int nb = (int)fb_b[idx] + (int)(b * cov);
    if (nr > 255) nr = 255;
    if (ng > 255) ng = 255;
    if (nb > 255) nb = 255;
    fb_r[idx] = (uint8_t)nr;
    fb_g[idx] = (uint8_t)ng;
    fb_b[idx] = (uint8_t)nb;
}

/* Launch (or relaunch) a ball from the floor */
static void launch_ball(int i, int W, int H, float gravity, float angle01) {
    float h_max = (float)(H - 1);
    if (h_max < 1.0f) h_max = 1.0f;

    /* upward speed to reach ~70-100% of the top */
    float reach = 0.70f + 0.30f * random_float();
    ball_vy[i] = f_sqrt(2.0f * gravity * h_max * reach);

    /* sideways kick proportional to the Angle param */
    ball_vx[i] = random_signed() * angle01 * ball_vy[i] * 0.8f;

    ball_rest[i] = 0.0f;
    ball_cor[i] = 0.86f + 0.10f * random_float();
    ball_hue[i] = random8();
}

EXPORT(init)
void init(void) {
    prev_tick = 0;

    int W = get_width();
    int H = get_height();
    if (W > MAX_W) W = MAX_W;
    if (H > MAX_H) H = MAX_H;
    if (W < 1) W = 1;
    if (H < 1) H = 1;

    fb_clear(W, H);

    for (int i = 0; i < MAX_BALLS; i++) {
        ball_x[i] = random_float() * (float)(W - 1);
        ball_y[i] = 0.0f;
        launch_ball(i, W, H, 45.0f, 0.35f);
        /* stagger initial heights so they don't all bounce in sync */
        ball_y[i] = random_float() * (float)(H - 1);
    }
}

EXPORT(update)
void update(int tick_ms) {
    int count   = get_param_i32(0);
    int size    = get_param_i32(1);
    int grav_p  = get_param_i32(2);
    int angle_p = get_param_i32(3);
    int bright  = get_param_i32(4);
    int hue_p   = get_param_i32(5);
    int wrap_x  = (get_param_i32(6) == 0);   /* 0 = wrap around cylinder, 1 = bounce */

    int W = get_width();
    int H = get_height();
    if (W > MAX_W) W = MAX_W;
    if (H > MAX_H) H = MAX_H;
    if (W < 1) W = 1;
    if (H < 1) H = 1;
    if (count > MAX_BALLS) count = MAX_BALLS;
    if (count < 1) count = 1;
    if (size < 1) size = 1;

    float gravity = (float)grav_p;        /* px/s^2 */
    float angle01 = (float)angle_p / 100.0f;
    float radius  = (float)size;

    rng_state ^= (uint32_t)tick_ms;

    /* per-frame delta time */
    int32_t delta_ms = tick_ms - prev_tick;
    if (delta_ms <= 0 || delta_ms > 200) delta_ms = 33;
    prev_tick = tick_ms;
    float dt = (float)delta_ms / 1000.0f;

    /* hue drift for rainbow mode */
    int hue_drift = (tick_ms / 16) & 0xFF;

    /* fade existing pixels for the trail */
    fb_dim(W, H, 200);

    float floor_y = 0.0f;
    float ceil_y  = (float)(H - 1);
    float left_x  = 0.0f;
    float right_x = (float)(W - 1);

    for (int i = 0; i < count; i++) {
        if (ball_rest[i] > 0.0f) {
            /* ball is briefly resting on the floor before the next jump */
            ball_rest[i] -= dt;
            if (ball_rest[i] <= 0.0f) {
                launch_ball(i, W, H, gravity, angle01);
            }
        } else {
            /* integrate physics */
            ball_vy[i] -= gravity * dt;

            /* Smooth cushioning near the top boundary: as the ball rises into
             * the upper margin, add extra deceleration that grows toward the
             * ceiling, so it eases into a gentle, hanging apex instead of
             * shooting straight up to the edge. */
            float cushion = ceil_y * 0.32f;
            if (cushion > 1.0f && ball_vy[i] > 0.0f) {
                float dist_top = ceil_y - ball_y[i];
                if (dist_top < 0.0f) dist_top = 0.0f;
                if (dist_top < cushion) {
                    float ease = 1.0f - dist_top / cushion;  /* 0 at margin .. 1 at ceiling */
                    ease = ease * ease;                       /* gentle start, firmer near edge */
                    ball_vy[i] -= ball_vy[i] * 5.0f * ease * dt;
                }
            }

            ball_y[i]  += ball_vy[i] * dt;
            ball_x[i]  += ball_vx[i] * dt;

            /* side walls: wrap around the cylinder or bounce */
            if (wrap_x) {
                float wf = (float)W;
                if (ball_x[i] < 0.0f)      ball_x[i] += wf;
                else if (ball_x[i] >= wf)  ball_x[i] -= wf;
            } else {
                if (ball_x[i] < left_x) {
                    ball_x[i] = left_x;
                    ball_vx[i] = -ball_vx[i];
                } else if (ball_x[i] > right_x) {
                    ball_x[i] = right_x;
                    ball_vx[i] = -ball_vx[i];
                }
            }

            /* bounce off the ceiling */
            if (ball_y[i] > ceil_y) {
                ball_y[i] = ceil_y;
                ball_vy[i] = -ball_vy[i] * ball_cor[i];
            }

            /* bounce off the floor */
            if (ball_y[i] < floor_y) {
                ball_y[i] = floor_y;
                ball_vy[i] = -ball_vy[i] * ball_cor[i];

                /* lost most of its energy -> settle and pause before relaunch */
                if (ball_vy[i] < 1.5f) {
                    ball_vy[i] = 0.0f;
                    ball_vx[i] = 0.0f;
                    ball_rest[i] = 0.15f + random_float() * 0.55f;
                }
            }
        }

        /* color */
        int r, g, b;
        int hue;
        if (hue_p == 0) {
            hue = (ball_hue[i] + hue_drift) & 0xFF;
        } else {
            hue = (hue_p + i * 24) & 0xFF;
        }
        hsv_to_rgb(hue, 255, bright, &r, &g, &b);

        /* squash a little while resting for a soft landing feel */
        float ry = radius;
        if (ball_rest[i] > 0.0f) ry = radius * 0.6f;

        /* draw an anti-aliased disc */
        float cx = ball_x[i];
        float cy = ball_y[i];
        int x0 = (int)(cx - radius - 1.0f);
        int x1 = (int)(cx + radius + 1.0f);
        int y0 = (int)(cy - ry - 1.0f);
        int y1 = (int)(cy + ry + 1.0f);
        if (y0 < 0) y0 = 0;
        if (y1 >= H) y1 = H - 1;
        if (!wrap_x) {
            if (x0 < 0) x0 = 0;
            if (x1 >= W) x1 = W - 1;
        }

        float rx = radius;
        for (int py = y0; py <= y1; py++) {
            for (int px = x0; px <= x1; px++) {
                float dx = ((float)px - cx) / rx;
                float dy = ((float)py - cy) / (ry < 0.5f ? 0.5f : ry);
                float d = f_sqrt(dx * dx + dy * dy);   /* normalized distance */
                float cov = (1.0f - d) * 1.4f;          /* soft edge + bright core */
                if (cov <= 0.0f) continue;
                if (cov > 1.0f) cov = 1.0f;
                /* wrap the column index so balls glow across the cylinder seam */
                int wx = px;
                if (wrap_x) {
                    wx %= W;
                    if (wx < 0) wx += W;
                }
                fb_add(wx, py, W, H, r, g, b, cov);
            }
        }
    }

    /* render framebuffer */
    for (int x = 0; x < W; x++) {
        for (int y = 0; y < H; y++) {
            int idx = y * W + x;
            set_pixel(x, y, fb_r[idx], fb_g[idx], fb_b[idx]);
        }
    }

    draw();
}
