#include "api.h"

/* ---- Metadata JSON ---- */
static const char META[] =
    "{\"name\":\"Starfall\","
    "\"desc\":\"Bright stars falling diagonally with glowing trails\","
    "\"params\":["
        "{\"id\":0,\"name\":\"Count\",\"type\":\"int\","
         "\"min\":1,\"max\":10,\"default\":4,"
         "\"desc\":\"Number of simultaneous falling stars\"},"
        "{\"id\":1,\"name\":\"Speed\",\"type\":\"int\","
         "\"min\":1,\"max\":10,\"default\":6,"
         "\"desc\":\"How fast stars fall\"},"
        "{\"id\":2,\"name\":\"Brightness\",\"type\":\"int\","
         "\"min\":1,\"max\":255,\"default\":220,"
         "\"desc\":\"Overall brightness\"}"
    "]}";

EXPORT(get_meta_ptr)
int get_meta_ptr(void) { return (int)META; }

EXPORT(get_meta_len)
int get_meta_len(void) { return sizeof(META) - 1; }

/* ---- PRNG (xorshift32) ---- */
static uint32_t rng_state = 37429;

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
    /* h: 0-255, s: 0-255, v: 0-255 */
    if (s == 0) {
        *r = v; *g = v; *b = v;
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

/* ---- Framebuffer for trails ---- */
#define MAX_W 64
#define MAX_H 64

static uint8_t fb_r[MAX_W][MAX_H];
static uint8_t fb_g[MAX_W][MAX_H];
static uint8_t fb_b[MAX_W][MAX_H];

/* Star particles */
#define MAX_STARS 10

static int star_x[MAX_STARS];        /* fixed-point 8.8 */
static int star_y[MAX_STARS];        /* fixed-point 8.8 */
static int star_dx[MAX_STARS];       /* horizontal speed (fixed-point) */
static int star_dy[MAX_STARS];       /* vertical speed (fixed-point, negative=down) */
static uint8_t star_hue[MAX_STARS];  /* color hue */
static uint8_t star_active[MAX_STARS];

static uint8_t qsub(uint8_t a, uint8_t b) {
    return (a > b) ? (uint8_t)(a - b) : 0;
}

static void spawn_star(int idx, int W, int H) {
    star_active[idx] = 1;
    /* Start from top area, random X */
    star_x[idx] = random_range(0, W) << 8;
    star_y[idx] = (H - 1) << 8;
    star_hue[idx] = (uint8_t)random8();
    /* Diagonal direction: slight drift right or left, falling down */
    star_dx[idx] = random_range(-80, 80);
    star_dy[idx] = 0;  /* dy is set in update based on speed param */
}

EXPORT(init)
void init(void) {
    rng_state = 37429;

    for (int x = 0; x < MAX_W; x++)
        for (int y = 0; y < MAX_H; y++) {
            fb_r[x][y] = 0;
            fb_g[x][y] = 0;
            fb_b[x][y] = 0;
        }

    for (int i = 0; i < MAX_STARS; i++)
        star_active[i] = 0;
}

EXPORT(update)
void update(int tick_ms) {
    int count  = get_param_i32(0);   /* 1-10 */
    int speed  = get_param_i32(1);   /* 1-10 */
    int bright = get_param_i32(2);   /* 1-255 */
    int W = get_width();
    int H = get_height();
    if (W > MAX_W) W = MAX_W;
    if (H > MAX_H) H = MAX_H;
    if (W < 1) W = 1;
    if (H < 1) H = 1;
    if (count > MAX_STARS) count = MAX_STARS;

    rng_state ^= (uint32_t)tick_ms;

    /* Fade trails */
    int fade = 25 + speed * 4;
    for (int x = 0; x < W; x++) {
        for (int y = 0; y < H; y++) {
            fb_r[x][y] = qsub(fb_r[x][y], (uint8_t)fade);
            fb_g[x][y] = qsub(fb_g[x][y], (uint8_t)fade);
            fb_b[x][y] = qsub(fb_b[x][y], (uint8_t)fade);
        }
    }

    /* Ensure 'count' stars are active */
    int active_count = 0;
    for (int i = 0; i < count; i++)
        if (star_active[i]) active_count++;

    for (int i = 0; i < count; i++) {
        if (!star_active[i]) {
            /* Stagger spawns a bit */
            if (random_range(0, 100) < 40) {
                spawn_star(i, W, H);
            }
        }
    }

    /* Update and draw stars */
    int fall_speed = speed * 50 + 30;

    for (int i = 0; i < count; i++) {
        if (!star_active[i]) continue;

        /* Move diagonally down */
        star_x[i] += star_dx[i];
        star_y[i] -= fall_speed;

        /* Wrap X (cylinder) */
        int ix = star_x[i] >> 8;
        if (ix < 0) star_x[i] += W << 8;
        if (ix >= W) star_x[i] -= W << 8;

        ix = star_x[i] >> 8;
        int iy = star_y[i] >> 8;

        /* Deactivate if fallen off */
        if (iy < -2) {
            star_active[i] = 0;
            continue;
        }

        /* Paint head as bright pixel in framebuffer */
        if (ix >= 0 && ix < W && iy >= 0 && iy < H) {
            int r, g, b;
            hsv_to_rgb(star_hue[i], 200, 255, &r, &g, &b);
            fb_r[ix][iy] = 255;
            fb_g[ix][iy] = (uint8_t)g;
            fb_b[ix][iy] = (uint8_t)b;

            /* Bright white core glow */
            fb_r[ix][iy] = 255;
            fb_g[ix][iy] = (uint8_t)((g + 255) / 2);
            fb_b[ix][iy] = (uint8_t)((b + 255) / 2);
        }

        /* Also paint a couple of pixels behind as colored trail seed */
        for (int t = 1; t <= 2; t++) {
            int tx = (star_x[i] - star_dx[i] * t) >> 8;
            int ty = iy + t;
            if (tx < 0) tx += W;
            if (tx >= W) tx -= W;
            if (tx >= 0 && tx < W && ty >= 0 && ty < H) {
                int r, g, b;
                int trail_v = 220 - t * 40;
                if (trail_v < 80) trail_v = 80;
                hsv_to_rgb(star_hue[i], 230, trail_v, &r, &g, &b);
                if ((uint8_t)r > fb_r[tx][ty]) fb_r[tx][ty] = (uint8_t)r;
                if ((uint8_t)g > fb_g[tx][ty]) fb_g[tx][ty] = (uint8_t)g;
                if ((uint8_t)b > fb_b[tx][ty]) fb_b[tx][ty] = (uint8_t)b;
            }
        }
    }

    /* Render framebuffer */
    for (int x = 0; x < W; x++) {
        for (int y = 0; y < H; y++) {
            int r = fb_r[x][y] * bright / 255;
            int g = fb_g[x][y] * bright / 255;
            int b = fb_b[x][y] * bright / 255;
            set_pixel(x, y, r, g, b);
        }
    }

    draw();
}
