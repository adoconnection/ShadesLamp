#include "api.h"

/*
 * Core — Orbiting particles around the equator with tilted orbits.
 * Each particle follows a sinusoidal path across the matrix (cylinder projection).
 * Inclination determines how far from the equator each orbit goes.
 * Palette: warm fire tones (same as Flame Particle).
 */

/* ---- Metadata ---- */
static const char META[] =
    "{\"name\":\"Core\","
    "\"desc\":\"Particles orbiting around the equator with tilted orbits\","
    "\"params\":["
        "{\"id\":0,\"name\":\"Hue\",\"type\":\"int\","
         "\"min\":0,\"max\":255,\"default\":25,"
         "\"desc\":\"Base hue (0=red, 25=fire-orange)\"},"
        "{\"id\":1,\"name\":\"Particles\",\"type\":\"int\","
         "\"min\":1,\"max\":30,\"default\":14,"
         "\"desc\":\"Number of orbiting particles\"},"
        "{\"id\":2,\"name\":\"Speed\",\"type\":\"int\","
         "\"min\":1,\"max\":100,\"default\":40,"
         "\"desc\":\"Median orbital speed\"},"
        "{\"id\":3,\"name\":\"Brightness\",\"type\":\"int\","
         "\"min\":1,\"max\":255,\"default\":220,"
         "\"desc\":\"Overall brightness\"}"
    "]}";

EXPORT(get_meta_ptr)
int get_meta_ptr(void) { return (int)META; }
EXPORT(get_meta_len)
int get_meta_len(void) { return sizeof(META) - 1; }

/* ---- PRNG (xorshift32) ---- */
static uint32_t rng = 55721;
static uint32_t rng_next(void) {
    uint32_t x = rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    rng = x; return x;
}

/* ---- HSV -> RGB (same as flame_particle) ---- */
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

/* ---- Sine approximation (Bhaskara I's formula, max error ~0.2%) ---- */
#define TWO_PI  6.28318530f
#define PI      3.14159265f
#define HALF_PI 1.57079632f

static float fsin(float x) {
    /* Normalize to [0, 2pi] */
    while (x < 0.0f) x += TWO_PI;
    while (x >= TWO_PI) x -= TWO_PI;
    float sign = 1.0f;
    if (x > PI) { x -= PI; sign = -1.0f; }
    float num = 16.0f * x * (PI - x);
    float den = 5.0f * PI * PI - 4.0f * x * (PI - x);
    if (den == 0.0f) return 0.0f;
    return sign * num / den;
}

/* ---- Particle state ---- */
#define MAX_PARTICLES 30

static float p_angle[MAX_PARTICLES];    /* orbital angle (radians) */
static float p_incl[MAX_PARTICLES];     /* inclination (-1.4 to +1.4 rad, ~80 deg) */
static float p_speed[MAX_PARTICLES];    /* speed multiplier (negative = reverse) */
static uint8_t p_hue[MAX_PARTICLES];    /* hue offset from base */
static uint8_t p_bri[MAX_PARTICLES];    /* particle brightness */

/* ---- HSV framebuffer for trails ---- */
#define MAX_W 64
#define MAX_H 64
static uint8_t fb_hue[MAX_W * MAX_H];
static uint8_t fb_sat[MAX_W * MAX_H];
static uint8_t fb_val[MAX_W * MAX_H];

static int cur_w, cur_h;
#define FB(x,y) ((x) * cur_h + (y))

/* ---- Wu antialiased particle draw (max-V strategy) ---- */
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

        /* Horizontal wrap (cylinder) */
        while (px < 0) px += cur_w;
        while (px >= cur_w) px -= cur_w;
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

EXPORT(init)
void init(void) {
    /* Clear framebuffer */
    for (int i = 0; i < MAX_W * MAX_H; i++) {
        fb_hue[i] = 0; fb_sat[i] = 0; fb_val[i] = 0;
    }

    rng = 55721;

    /* Initialize particles with random orbits */
    for (int i = 0; i < MAX_PARTICLES; i++) {
        /* Random starting angle */
        p_angle[i] = (float)(rng_next() % 10000) / 10000.0f * TWO_PI;

        /* Random inclination: -80 to +80 degrees (avoid pure equatorial = boring) */
        float raw = ((float)(rng_next() % 10000) / 10000.0f) * 2.0f - 1.0f;
        p_incl[i] = raw * 1.4f;

        /* Speed multiplier: 0.3x to 2.5x of base, some go backwards */
        p_speed[i] = 0.3f + (float)(rng_next() % 10000) / 4545.0f;
        if (rng_next() & 1) p_speed[i] = -p_speed[i];

        /* Brightness: 180-255 */
        p_bri[i] = (uint8_t)(180 + rng_next() % 76);

        /* Hue offset: 0-30 from base */
        p_hue[i] = (uint8_t)(rng_next() % 30);
    }
}

EXPORT(update)
void update(int tick_ms) {
    int base_hue = get_param_i32(0);
    int num_p    = get_param_i32(1);
    int speed    = get_param_i32(2);
    int bright   = get_param_i32(3);

    cur_w = get_width();
    cur_h = get_height();
    if (cur_w > MAX_W) cur_w = MAX_W;
    if (cur_h > MAX_H) cur_h = MAX_H;
    if (cur_w < 1) cur_w = 1;
    if (cur_h < 1) cur_h = 1;
    if (num_p > MAX_PARTICLES) num_p = MAX_PARTICLES;

    float equator = (float)cur_h / 2.0f;
    float max_amp = equator - 1.0f;
    if (max_amp < 1.0f) max_amp = 1.0f;

    /* Radians per frame; speed=40 -> ~0.016 rad/frame -> full orbit ~13s */
    float base_speed = (float)speed * 0.0004f;

    rng ^= (uint32_t)tick_ms;

    /* ---- Fade framebuffer (~90%: trails last ~0.7s at 30fps) ---- */
    for (int x = 0; x < cur_w; x++)
        for (int y = 0; y < cur_h; y++)
            fb_val[FB(x, y)] = (uint8_t)((int)fb_val[FB(x, y)] * 230 >> 8);

    /* ---- Update and draw particles ---- */
    for (int i = 0; i < num_p; i++) {
        /* Advance orbital angle */
        p_angle[i] += base_speed * p_speed[i];
        if (p_angle[i] >= TWO_PI) p_angle[i] -= TWO_PI;
        if (p_angle[i] < 0.0f) p_angle[i] += TWO_PI;

        /* Map angle to X (linear around the cylinder) */
        float x = p_angle[i] / TWO_PI * (float)cur_w;

        /* Y offset from equator: sinusoidal, amplitude depends on inclination */
        float y = equator + fsin(p_angle[i]) * max_amp * fsin(p_incl[i]);

        /* Clamp */
        if (x >= (float)cur_w) x -= (float)cur_w;
        if (y < 0.0f) y = 0.0f;
        if (y >= (float)cur_h - 0.01f) y = (float)cur_h - 0.01f;

        uint8_t hue = (uint8_t)(base_hue + p_hue[i]);
        wu_draw(x, y, p_bri[i], hue);
    }

    /* ---- Render framebuffer to pixels ---- */
    for (int x = 0; x < cur_w; x++) {
        for (int y = 0; y < cur_h; y++) {
            int fi = FB(x, y);
            int v = (int)fb_val[fi] * bright / 255;
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
