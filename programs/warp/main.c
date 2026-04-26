#include "api.h"

/* ---- Metadata JSON ---- */
static const char META[] =
    "{\"name\":\"Warp\","
    "\"desc\":\"Hyperspace warp — stars streaking outward from center\","
    "\"params\":["
        "{\"id\":0,\"name\":\"Speed\",\"type\":\"int\","
         "\"min\":1,\"max\":100,\"default\":60,"
         "\"desc\":\"Warp speed\"},"
        "{\"id\":1,\"name\":\"Stars\",\"type\":\"int\","
         "\"min\":10,\"max\":80,\"default\":40,"
         "\"desc\":\"Number of stars\"},"
        "{\"id\":2,\"name\":\"Brightness\",\"type\":\"int\","
         "\"min\":1,\"max\":255,\"default\":230,"
         "\"desc\":\"Overall brightness\"},"
        "{\"id\":3,\"name\":\"Trail\",\"type\":\"int\","
         "\"min\":1,\"max\":50,\"default\":25,"
         "\"desc\":\"Streak length\"}"
    "]}";

EXPORT(get_meta_ptr)
int get_meta_ptr(void) { return (int)META; }

EXPORT(get_meta_len)
int get_meta_len(void) { return sizeof(META) - 1; }

/* ---- PRNG (xorshift32) ---- */
static uint32_t rng = 12345;
static uint32_t rng_next(void) {
    uint32_t x = rng; x ^= x << 13; x ^= x >> 17; x ^= x << 5; rng = x; return x;
}
static int rand_range(int lo, int hi) { if (lo >= hi) return lo; return lo + (int)(rng_next() % (uint32_t)(hi - lo)); }

/* ---- HSV to RGB ---- */
static void hsv2rgb(int hue, int sat, int val, int *r, int *g, int *b) {
    if (val == 0) { *r = *g = *b = 0; return; }
    if (sat == 0) { *r = *g = *b = val; return; }
    int h = hue & 0xFF; int region = h / 43; int frac = (h - region * 43) * 6;
    int p = (val * (255 - sat)) >> 8; int q = (val * (255 - ((sat * frac) >> 8))) >> 8;
    int t = (val * (255 - ((sat * (255 - frac)) >> 8))) >> 8;
    switch (region) {
        case 0: *r=val;*g=t;*b=p;break; case 1: *r=q;*g=val;*b=p;break;
        case 2: *r=p;*g=val;*b=t;break; case 3: *r=p;*g=q;*b=val;break;
        case 4: *r=t;*g=p;*b=val;break; default: *r=val;*g=p;*b=q;break;
    }
}

/* ---- Math helpers ---- */
#define TWO_PI 6.28318530f
#define PI 3.14159265f
#define HALF_PI 1.57079632f
static float fsin(float x) {
    while (x < 0.0f) x += TWO_PI; while (x >= TWO_PI) x -= TWO_PI;
    float sign = 1.0f; if (x > PI) { x -= PI; sign = -1.0f; }
    float num = 16.0f * x * (PI - x);
    float den = 5.0f * PI * PI - 4.0f * x * (PI - x);
    if (den == 0.0f) return 0.0f; return sign * num / den;
}
static float fcos(float x) { return fsin(x + HALF_PI); }

static float fsqrt(float x) {
    if (x <= 0.0f) return 0.0f;
    float g = x * 0.5f; g = 0.5f*(g+x/g); g = 0.5f*(g+x/g); g = 0.5f*(g+x/g); return g;
}

/* ---- Framebuffer ---- */
#define MAX_W 64
#define MAX_H 64
#define FB(x,y) ((x) * MAX_H + (y))

static uint8_t fb_r[MAX_W * MAX_H];
static uint8_t fb_g[MAX_W * MAX_H];
static uint8_t fb_b[MAX_W * MAX_H];

/* ---- Star state ---- */
#define MAX_STARS 80

static float star_angle[MAX_STARS];    /* radial angle from center */
static float star_dist[MAX_STARS];     /* distance from center */
static float star_speed[MAX_STARS];    /* individual speed multiplier */
static uint8_t star_active[MAX_STARS];

static int cur_w, cur_h;
static float cx, cy;                   /* center of warp */

/* Saturating subtract for uint8 fade */
static uint8_t qsub(uint8_t a, uint8_t b) {
    return (a > b) ? (uint8_t)(a - b) : 0;
}

static int clamp255(int v) {
    if (v < 0) return 0;
    if (v > 255) return 255;
    return v;
}

static void spawn_star(int i) {
    star_angle[i] = (float)rand_range(0, 6283) / 1000.0f;  /* 0 to ~2*PI */
    star_dist[i] = (float)rand_range(5, 25) / 10.0f;       /* start near center (0.5 - 2.5) */
    star_speed[i] = (float)rand_range(60, 140) / 100.0f;    /* 0.6 - 1.4 speed variation */
    star_active[i] = 1;
}

/* Wrap X coordinate for cylindrical display */
static int wrapx(int x) {
    while (x < 0) x += cur_w;
    while (x >= cur_w) x -= cur_w;
    return x;
}

/* Draw a pixel into the framebuffer with max blending */
static void fb_set_max(int x, int y, int r, int g, int b) {
    if (y < 0 || y >= cur_h) return;
    x = wrapx(x);
    int idx = FB(x, y);
    if ((uint8_t)r > fb_r[idx]) fb_r[idx] = (uint8_t)r;
    if ((uint8_t)g > fb_g[idx]) fb_g[idx] = (uint8_t)g;
    if ((uint8_t)b > fb_b[idx]) fb_b[idx] = (uint8_t)b;
}

EXPORT(init)
void init(void) {
    cur_w = get_width();
    cur_h = get_height();
    if (cur_w < 1) cur_w = 1;
    if (cur_h < 1) cur_h = 1;

    cx = (float)cur_w / 2.0f;
    cy = (float)cur_h / 2.0f;

    /* Clear framebuffer */
    for (int i = 0; i < MAX_W * MAX_H; i++) {
        fb_r[i] = 0;
        fb_g[i] = 0;
        fb_b[i] = 0;
    }

    /* Initialize all stars at varying distances for immediate visual */
    for (int i = 0; i < MAX_STARS; i++) {
        spawn_star(i);
        /* Spread initial distances so they don't all start clumped */
        float max_dist = fsqrt(cx * cx + cy * cy);
        star_dist[i] = (float)rand_range(5, (int)(max_dist * 10.0f)) / 10.0f;
    }
}

EXPORT(update)
void update(int tick_ms) {
    int speed  = get_param_i32(0);
    int nstars = get_param_i32(1);
    int bright = get_param_i32(2);
    int trail  = get_param_i32(3);

    cur_w = get_width();
    cur_h = get_height();
    if (cur_w < 1) cur_w = 1;
    if (cur_h < 1) cur_h = 1;
    if (cur_w > MAX_W) cur_w = MAX_W;
    if (cur_h > MAX_H) cur_h = MAX_H;
    if (nstars > MAX_STARS) nstars = MAX_STARS;
    if (nstars < 1) nstars = 1;

    rng ^= (uint32_t)tick_ms;

    cx = (float)cur_w / 2.0f;
    cy = (float)cur_h / 2.0f;

    float dt = (float)tick_ms / 1000.0f;
    float spd = (float)speed / 30.0f;  /* normalize: 60 -> 2.0 */

    /* Maximum distance a star can be from center before respawning */
    float max_dist = fsqrt(cx * cx + cy * cy) + 2.0f;

    /* ---- Fade framebuffer for trail effect ---- */
    /* Trail param controls fade speed: higher trail = slower fade = longer streaks */
    int fade = 320 / (trail + 3);
    if (fade < 3) fade = 3;
    if (fade > 80) fade = 80;

    for (int i = 0; i < cur_w * MAX_H; i++) {
        /* Only fade the active area, but do it simply with the flat index trick */
        fb_r[i] = qsub(fb_r[i], (uint8_t)fade);
        fb_g[i] = qsub(fb_g[i], (uint8_t)fade);
        fb_b[i] = qsub(fb_b[i], (uint8_t)fade);
    }
    /* Also fade the rest for safety (columns beyond cur_w in the buffer) */

    /* ---- Update stars ---- */
    for (int i = 0; i < nstars; i++) {
        if (!star_active[i]) {
            /* Stagger spawns */
            if (rand_range(0, 100) < 50) {
                spawn_star(i);
            }
            continue;
        }

        /* Move outward: speed increases with distance (perspective effect) */
        float accel = 1.0f + star_dist[i] * 0.3f;
        star_dist[i] += spd * star_speed[i] * accel * dt * 3.0f;

        /* Current screen position */
        float sx = cx + fcos(star_angle[i]) * star_dist[i];
        float sy = cy + fsin(star_angle[i]) * star_dist[i];

        /* Check if out of bounds (Y) or too far */
        if (star_dist[i] > max_dist) {
            spawn_star(i);
            continue;
        }

        /* ---- Draw the star streak ---- */
        /* Streak goes from current position back toward center */
        /* Streak length proportional to distance from center */
        float streak_frac = star_dist[i] / max_dist;  /* 0 near center, 1 at edge */
        float streak_len = 1.0f + streak_frac * (float)trail * 0.4f;
        if (streak_len < 1.0f) streak_len = 1.0f;

        /* Brightness increases with distance (stars get brighter as they approach) */
        float dist_bright = 0.15f + 0.85f * streak_frac;
        int star_val = (int)((float)bright * dist_bright);
        if (star_val > 255) star_val = 255;

        /* Direction back toward center */
        float dx = cx - sx;
        float dy = cy - sy;
        float len = fsqrt(dx * dx + dy * dy);
        if (len < 0.001f) len = 0.001f;
        float ndx = dx / len;
        float ndy = dy / len;

        /* Draw streak as a series of points from current pos back toward center */
        int num_points = (int)(streak_len + 1.0f);
        if (num_points < 2) num_points = 2;
        if (num_points > 30) num_points = 30;

        float step_size = streak_len / (float)num_points;

        for (int p = 0; p < num_points; p++) {
            float t = (float)p * step_size;
            float px = sx + ndx * t;
            float py = sy + ndy * t;

            int ix = (int)(px + 0.5f);
            int iy = (int)(py + 0.5f);

            if (iy < 0 || iy >= cur_h) continue;
            ix = wrapx(ix);

            /* Fade along the streak: brightest at head (p=0), dimmer toward tail */
            float fade_t = 1.0f - (float)p / (float)num_points;
            int pv = (int)((float)star_val * fade_t);
            if (pv < 1) continue;

            /* Stars are white-blue: slight blue tint near center, pure white at edges */
            int r, g, b;
            if (streak_frac < 0.3f) {
                /* Near center: slight blue tint */
                int blue_extra = (int)(30.0f * (1.0f - streak_frac / 0.3f));
                r = clamp255(pv - blue_extra / 2);
                g = clamp255(pv - blue_extra / 3);
                b = clamp255(pv);
            } else {
                /* Further out: pure bright white */
                r = pv;
                g = pv;
                b = pv;
            }

            fb_set_max(ix, iy, r, g, b);
        }

        /* Draw bright head pixel */
        {
            int hx = (int)(sx + 0.5f);
            int hy = (int)(sy + 0.5f);
            if (hy >= 0 && hy < cur_h) {
                hx = wrapx(hx);
                int hv = clamp255(star_val + 25);  /* extra bright head */
                fb_set_max(hx, hy, hv, hv, hv);
            }
        }
    }

    /* ---- Render framebuffer to display ---- */
    for (int x = 0; x < cur_w; x++) {
        for (int y = 0; y < cur_h; y++) {
            int idx = FB(x, y);
            set_pixel(x, y, fb_r[idx], fb_g[idx], fb_b[idx]);
        }
    }

    draw();
}
