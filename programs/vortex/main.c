#include "api.h"

/*
 * Vortex - Particles spiraling into or out from the center,
 * like a whirlpool on a cylindrical LED matrix.
 * Uses HSV framebuffer with fade for trails and Wu antialiasing.
 */

static const char META[] =
    "{\"name\":\"Vortex\","
    "\"desc\":\"Particles spiraling into or out from center like a whirlpool\","
    "\"params\":["
        "{\"id\":0,\"name\":\"Hue\",\"type\":\"int\","
         "\"min\":0,\"max\":255,\"default\":160,"
         "\"desc\":\"Base color (160=blue)\"},"
        "{\"id\":1,\"name\":\"Particles\",\"type\":\"int\","
         "\"min\":5,\"max\":50,\"default\":30,"
         "\"desc\":\"Number of particles\"},"
        "{\"id\":2,\"name\":\"Speed\",\"type\":\"int\","
         "\"min\":1,\"max\":100,\"default\":50,"
         "\"desc\":\"Spiral speed\"},"
        "{\"id\":3,\"name\":\"Brightness\",\"type\":\"int\","
         "\"min\":1,\"max\":255,\"default\":220,"
         "\"desc\":\"Overall brightness\"},"
        "{\"id\":4,\"name\":\"Direction\",\"type\":\"int\","
         "\"min\":0,\"max\":1,\"default\":0,"
         "\"options\":[\"Inward\",\"Outward\"],"
         "\"desc\":\"Spiral direction\"}"
    "]}";

EXPORT(get_meta_ptr)
int get_meta_ptr(void) { return (int)META; }
EXPORT(get_meta_len)
int get_meta_len(void) { return sizeof(META) - 1; }

/* ---- PRNG (xorshift32) ---- */
static uint32_t rng = 12345;
static uint32_t rng_next(void) {
    uint32_t x = rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    rng = x; return x;
}

/* ---- Math helpers ---- */
#define TWO_PI   6.28318530f
#define PI       3.14159265f
#define HALF_PI  1.57079632f

static float fsin(float x) {
    while (x < 0.0f) x += TWO_PI;
    while (x >= TWO_PI) x -= TWO_PI;
    float sign = 1.0f;
    if (x > PI) { x -= PI; sign = -1.0f; }
    float num = 16.0f * x * (PI - x);
    float den = 5.0f * PI * PI - 4.0f * x * (PI - x);
    if (den == 0.0f) return 0.0f;
    return sign * num / den;
}

static float fcos(float x) { return fsin(x + HALF_PI); }

static float fsqrt(float x) {
    if (x <= 0.0f) return 0.0f;
    float guess = x * 0.5f;
    guess = 0.5f * (guess + x / guess);
    guess = 0.5f * (guess + x / guess);
    guess = 0.5f * (guess + x / guess);
    return guess;
}

/* ---- HSV to RGB ---- */
static void hsv2rgb(int hue, int sat, int val, int *r, int *g, int *b) {
    if (val == 0) { *r = *g = *b = 0; return; }
    if (sat == 0) { *r = *g = *b = val; return; }
    int h = hue & 0xFF;
    int region = h / 43;
    int frac = (h - region * 43) * 6;
    int p = (val * (255 - sat)) >> 8;
    int q = (val * (255 - ((sat * frac) >> 8))) >> 8;
    int t = (val * (255 - ((sat * (255 - frac)) >> 8))) >> 8;
    switch (region) {
        case 0:  *r = val; *g = t;   *b = p;   break;
        case 1:  *r = q;   *g = val; *b = p;   break;
        case 2:  *r = p;   *g = val; *b = t;   break;
        case 3:  *r = p;   *g = q;   *b = val; break;
        case 4:  *r = t;   *g = p;   *b = val; break;
        default: *r = val; *g = p;   *b = q;   break;
    }
}

/* ---- Framebuffer (HSV) ---- */
#define MAX_W 64
#define MAX_H 64

static uint8_t fb_hue[MAX_W * MAX_H];
static uint8_t fb_sat[MAX_W * MAX_H];
static uint8_t fb_val[MAX_W * MAX_H];

static int cur_w, cur_h;
static int32_t prev_tick;
#define FB(x,y) ((x) * cur_h + (y))

/* ---- Particles ---- */
#define MAX_P 50

static float p_angle[MAX_P];      /* orbital angle (radians) */
static float p_radius[MAX_P];     /* distance from center */
static float p_speed_mult[MAX_P]; /* individual speed multiplier */
static uint8_t p_hue_off[MAX_P];  /* hue offset from base */

/* ---- Wu antialiased pixel draw ---- */
static void wu_draw(float fx, float fy, uint8_t bright, uint8_t hue_val) {
    int ix0 = (int)fx;
    int iy0 = (int)fy;
    if (fx < 0.0f) ix0--;
    if (fy < 0.0f) iy0--;

    int xx = (int)((fx - (float)ix0) * 255.0f);
    int yy = (int)((fy - (float)iy0) * 255.0f);
    int ixx = 255 - xx;
    int iyy = 255 - yy;

    int wu[4] = {
        (ixx * iyy + ixx + iyy) >> 8,
        (xx  * iyy + xx  + iyy) >> 8,
        (ixx * yy  + ixx + yy)  >> 8,
        (xx  * yy  + xx  + yy)  >> 8
    };

    for (int i = 0; i < 4; i++) {
        int px = ix0 + (i & 1);
        int py = iy0 + ((i >> 1) & 1);

        /* Horizontal cylinder wrap */
        while (px < 0) px += cur_w;
        while (px >= cur_w) px -= cur_w;
        /* Vertical: no wrap */
        if (py < 0 || py >= cur_h) continue;

        int weighted = (int)bright * wu[i] >> 8;
        int fi = FB(px, py);

        if (weighted >= (int)fb_val[fi]) {
            fb_hue[fi] = hue_val;
            fb_sat[fi] = 255;
            fb_val[fi] = (uint8_t)weighted;
        }
    }
}

/* ---- Spawn a particle at the edge (inward) or center (outward) ---- */
static void spawn_particle(int i, int direction, float max_radius) {
    p_angle[i] = (float)(rng_next() % 10000) / 10000.0f * TWO_PI;
    p_speed_mult[i] = 0.5f + (float)(rng_next() % 10000) / 10000.0f; /* 0.5x to 1.5x */
    p_hue_off[i] = (uint8_t)(rng_next() % 40); /* 0-39 hue variation */

    if (direction == 0) {
        /* Inward: spawn at edge */
        p_radius[i] = max_radius * (0.7f + (float)(rng_next() % 3000) / 10000.0f);
    } else {
        /* Outward: spawn near center */
        p_radius[i] = 1.0f + (float)(rng_next() % 2000) / 10000.0f * max_radius * 0.15f;
    }
}

EXPORT(init)
void init(void) {
    cur_w = get_width();
    cur_h = get_height();
    if (cur_w > MAX_W) cur_w = MAX_W;
    if (cur_h > MAX_H) cur_h = MAX_H;
    if (cur_w < 1) cur_w = 1;
    if (cur_h < 1) cur_h = 1;

    /* Clear framebuffer */
    for (int i = 0; i < MAX_W * MAX_H; i++) {
        fb_hue[i] = 0;
        fb_sat[i] = 0;
        fb_val[i] = 0;
    }

    float cx = (float)cur_w / 2.0f;
    float cy = (float)cur_h / 2.0f;
    float max_radius = fsqrt(cx * cx + cy * cy);

    prev_tick = 0;

    /* Initialize particles spread across the field */
    for (int i = 0; i < MAX_P; i++) {
        p_angle[i] = (float)(rng_next() % 10000) / 10000.0f * TWO_PI;
        p_radius[i] = 1.0f + (float)(rng_next() % 10000) / 10000.0f * (max_radius - 1.0f);
        p_speed_mult[i] = 0.5f + (float)(rng_next() % 10000) / 10000.0f;
        p_hue_off[i] = (uint8_t)(rng_next() % 40);
    }
}

EXPORT(update)
void update(int tick_ms) {
    int base_hue  = get_param_i32(0);
    int num_p     = get_param_i32(1);
    int speed     = get_param_i32(2);
    int bright    = get_param_i32(3);
    int direction = get_param_i32(4);

    cur_w = get_width();
    cur_h = get_height();
    if (cur_w > MAX_W) cur_w = MAX_W;
    if (cur_h > MAX_H) cur_h = MAX_H;
    if (cur_w < 1) cur_w = 1;
    if (cur_h < 1) cur_h = 1;
    if (num_p > MAX_P) num_p = MAX_P;
    if (num_p < 1) num_p = 1;
    if (direction < 0) direction = 0;
    if (direction > 1) direction = 1;

    float cx = (float)cur_w / 2.0f;
    float cy = (float)cur_h / 2.0f;
    float max_radius = fsqrt(cx * cx + cy * cy);

    int32_t delta_ms = tick_ms - prev_tick;
    if (delta_ms <= 0 || delta_ms > 200) delta_ms = 33;
    prev_tick = tick_ms;
    float dt = (float)delta_ms / 1000.0f;
    float base_angular_speed = (float)speed * 0.06f; /* radians/sec */
    float base_radial_speed  = (float)speed * 0.08f;  /* pixels/sec */

    rng ^= (uint32_t)tick_ms;

    /* Fade framebuffer for trails */
    int fade = 230; /* ~90% retention per frame */
    for (int i = 0; i < cur_w * cur_h; i++) {
        int fi = FB(i / cur_h, i % cur_h);
        int v = ((int)fb_val[fi] * fade) >> 8;
        fb_val[fi] = (uint8_t)v;
    }

    /* Update and draw particles */
    for (int i = 0; i < num_p; i++) {
        /* Angular speed increases as particle approaches center (conservation of angular momentum) */
        float inv_r = (p_radius[i] > 1.0f) ? (max_radius / p_radius[i]) : max_radius;
        /* Clamp the angular speed boost to avoid insane speeds at very small radii */
        if (inv_r > 5.0f) inv_r = 5.0f;

        float angular_speed = base_angular_speed * p_speed_mult[i] * inv_r;
        p_angle[i] += angular_speed * dt;
        if (p_angle[i] >= TWO_PI) p_angle[i] -= TWO_PI;
        if (p_angle[i] < 0.0f) p_angle[i] += TWO_PI;

        /* Radial movement */
        if (direction == 0) {
            /* Inward spiral */
            p_radius[i] -= base_radial_speed * p_speed_mult[i] * dt;
            if (p_radius[i] < 1.0f) {
                spawn_particle(i, direction, max_radius);
            }
        } else {
            /* Outward spiral */
            p_radius[i] += base_radial_speed * p_speed_mult[i] * dt;
            if (p_radius[i] > max_radius) {
                spawn_particle(i, direction, max_radius);
            }
        }

        /* Convert polar to cartesian (relative to center) */
        float fx = cx + fcos(p_angle[i]) * p_radius[i];
        float fy = cy + fsin(p_angle[i]) * p_radius[i];

        /* Particle brightness: brighter near center (inward), brighter near edge (outward) */
        float norm_r = p_radius[i] / max_radius;
        int p_bright;
        if (direction == 0) {
            /* Inward: brighter as it approaches center */
            p_bright = (int)((float)bright * (1.0f - norm_r * 0.5f));
        } else {
            /* Outward: brighter as it moves away */
            p_bright = (int)((float)bright * (0.5f + norm_r * 0.5f));
        }
        if (p_bright > 255) p_bright = 255;
        if (p_bright < 1) p_bright = 1;

        /* Hue: base + particle offset + slight variation by radius */
        uint8_t hue = (uint8_t)(base_hue + p_hue_off[i] + (int)(norm_r * 20.0f));

        wu_draw(fx, fy, (uint8_t)p_bright, hue);
    }

    /* Render framebuffer to display */
    for (int x = 0; x < cur_w; x++) {
        for (int y = 0; y < cur_h; y++) {
            int fi = FB(x, y);
            int v = (int)fb_val[fi];
            if (v < 1) {
                set_pixel(x, y, 0, 0, 0);
            } else {
                int r, g, b;
                hsv2rgb(fb_hue[fi], fb_sat[fi], v, &r, &g, &b);
                set_pixel(x, y, r, g, b);
            }
        }
    }

    draw();
}
