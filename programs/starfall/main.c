#include "api.h"

/* ---- Metadata JSON ---- */
static const char META[] =
    "{\"name\":\"Starfall\","
    "\"desc\":\"Bright stars streaking down the sky with glowing trails\","
    "\"params\":["
        "{\"id\":0,\"name\":\"Count\",\"type\":\"int\","
         "\"min\":1,\"max\":12,\"default\":4,"
         "\"desc\":\"Maximum simultaneous falling stars\"},"
        "{\"id\":1,\"name\":\"Speed\",\"type\":\"int\","
         "\"min\":1,\"max\":100,\"default\":50,"
         "\"desc\":\"How fast stars fall (higher = faster)\"},"
        "{\"id\":2,\"name\":\"Glow\",\"type\":\"int\","
         "\"min\":0,\"max\":100,\"default\":70,"
         "\"desc\":\"Trail length (higher = longer)\"},"
        "{\"id\":3,\"name\":\"Color\",\"type\":\"select\","
         "\"default\":0,"
         "\"options\":[\"Random\",\"White\",\"Gold\",\"Cyan\",\"Magenta\"],"
         "\"desc\":\"Star color palette\"},"
        "{\"id\":4,\"name\":\"Brightness\",\"type\":\"int\","
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

static int random_range(int lo, int hi) {
    if (lo >= hi) return lo;
    return lo + (int)(rng_next() % (uint32_t)(hi - lo));
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

/* ---- Buffers ---- */
#define MAX_W 64
#define MAX_H 64
static uint8_t fb_r[MAX_W][MAX_H];
static uint8_t fb_g[MAX_W][MAX_H];
static uint8_t fb_b[MAX_W][MAX_H];

#define MAX_STARS 12
typedef struct {
    int32_t x_q8;       /* fixed-point 24.8 horizontal position */
    int32_t y_q8;       /* fixed-point 24.8 vertical position (Y=0 is bottom) */
    int32_t vy_q8;      /* fall velocity in Q8 units per frame */
    int32_t dx_q8;      /* horizontal drift per frame (slight diagonal) */
    uint8_t hue;        /* color hue */
    uint8_t sat;        /* color saturation */
    uint8_t active;
    uint8_t spawn_delay; /* frames until respawn */
} Star;

static Star stars[MAX_STARS];

static uint8_t qsub(int a, int b) {
    int v = a - b;
    if (v < 0) v = 0;
    if (v > 255) v = 255;
    return (uint8_t)v;
}

static uint8_t qmax(uint8_t a, int b) {
    if (b > 255) b = 255;
    if (b < 0) b = 0;
    return (a > (uint8_t)b) ? a : (uint8_t)b;
}

/* Pick hue/sat for a new star based on Color param.
 *  0=Random, 1=White, 2=Gold, 3=Cyan, 4=Magenta */
static void pick_color(int palette, uint8_t *hue, uint8_t *sat) {
    switch (palette) {
        case 1: *hue = 0;   *sat = 0;   break;                          /* white */
        case 2: *hue = 32;  *sat = 200; break;                          /* gold */
        case 3: *hue = 128; *sat = 200; break;                          /* cyan */
        case 4: *hue = 213; *sat = 220; break;                          /* magenta */
        default:                                                         /* random */
            *hue = (uint8_t)(rng_next() & 0xFF);
            *sat = 200;
            break;
    }
}

static void spawn_star(int idx, int W, int H, int speed_param, int palette) {
    Star *s = &stars[idx];
    s->active = 1;
    s->spawn_delay = 0;
    s->x_q8 = random_range(0, W) << 8;
    s->y_q8 = (H - 1 + random_range(0, 4)) << 8;   /* start slightly above top */
    /* Vertical speed proportional to Speed param + per-star variation.
     * speed=1 → ~0.15 px/frame, speed=100 → ~1.5 px/frame */
    int base_v = 30 + speed_param * 4;
    int jitter = random_range(-base_v / 4, base_v / 4);
    s->vy_q8 = base_v + jitter;
    /* Slight horizontal drift, ±0.05 px/frame */
    s->dx_q8 = (int32_t)random_range(-12, 12);
    pick_color(palette, &s->hue, &s->sat);
}

EXPORT(init)
void init(void) {
    rng_state = 37429;
    for (int x = 0; x < MAX_W; x++) {
        for (int y = 0; y < MAX_H; y++) {
            fb_r[x][y] = 0;
            fb_g[x][y] = 0;
            fb_b[x][y] = 0;
        }
    }
    for (int i = 0; i < MAX_STARS; i++) {
        stars[i].active = 0;
        stars[i].spawn_delay = (uint8_t)(i * 5);
    }
}

EXPORT(update)
void update(int tick_ms) {
    int count   = get_param_i32(0);  /* 1..12 */
    int speed   = get_param_i32(1);  /* 1..100 */
    int glow    = get_param_i32(2);  /* 0..100 */
    int palette = get_param_i32(3);  /* 0..4 */
    int bright  = get_param_i32(4);  /* 1..255 */

    int W = get_width();
    int H = get_height();
    if (W > MAX_W) W = MAX_W;
    if (H > MAX_H) H = MAX_H;
    if (W < 1) W = 1;
    if (H < 1) H = 1;
    if (count > MAX_STARS) count = MAX_STARS;
    if (count < 1) count = 1;

    /* Re-seed RNG occasionally to keep new spawns varied even if rng cycles */
    rng_state ^= (uint32_t)tick_ms * 2654435761u;

    /* Subtractive trail fade (same trick as Aurora).
     * glow 0..100 → step 22..3 per frame. */
    int step = 22 - (glow * 19) / 100;
    if (step < 1) step = 1;

    for (int x = 0; x < W; x++) {
        for (int y = 0; y < H; y++) {
            fb_r[x][y] = qsub(fb_r[x][y], step);
            fb_g[x][y] = qsub(fb_g[x][y], step);
            fb_b[x][y] = qsub(fb_b[x][y], step);
        }
    }

    /* Spawn / respawn stars up to `count` */
    for (int i = 0; i < count; i++) {
        if (stars[i].active) continue;
        if (stars[i].spawn_delay > 0) {
            stars[i].spawn_delay--;
            continue;
        }
        /* Probabilistic spawn so stars are staggered, not synchronized */
        if ((rng_next() & 0x1F) < 6) {
            spawn_star(i, W, H, speed, palette);
        }
    }

    /* Move and paint stars */
    for (int i = 0; i < MAX_STARS; i++) {
        Star *s = &stars[i];
        if (!s->active) continue;

        /* Stars beyond `count` are killed off */
        if (i >= count) { s->active = 0; continue; }

        s->x_q8 += s->dx_q8;
        s->y_q8 -= s->vy_q8;   /* Y=0 is bottom → falling = decreasing Y */

        /* Wrap X */
        int W_q8 = W << 8;
        if (s->x_q8 < 0) s->x_q8 += W_q8;
        if (s->x_q8 >= W_q8) s->x_q8 -= W_q8;

        int iy = s->y_q8 >> 8;
        /* Once the star drops fully off the bottom, retire it (with a delay) */
        if (iy < -2) {
            s->active = 0;
            s->spawn_delay = (uint8_t)random_range(0, 30);
            continue;
        }

        int ix = s->x_q8 >> 8;
        int frac_y = s->y_q8 & 0xFF;        /* 0..255 sub-pixel position */
        int weight_low = 255 - frac_y;       /* anti-aliasing into iy */
        int weight_high = frac_y;            /* anti-aliasing into iy+1 */

        /* Compute head color (bright white core mixed with hue tint) */
        int hr, hg, hb;
        hsv2rgb(s->hue, s->sat, 255, &hr, &hg, &hb);
        /* Mix in a strong white core */
        int head_r = (hr + 255) / 2;
        int head_g = (hg + 255) / 2;
        int head_b = (hb + 255) / 2;

        /* Anti-aliased head: split intensity between iy and iy+1 (top side) */
        if (ix >= 0 && ix < W) {
            if (iy >= 0 && iy < H) {
                int r = (head_r * weight_low) >> 8;
                int g = (head_g * weight_low) >> 8;
                int b = (head_b * weight_low) >> 8;
                fb_r[ix][iy] = qmax(fb_r[ix][iy], r);
                fb_g[ix][iy] = qmax(fb_g[ix][iy], g);
                fb_b[ix][iy] = qmax(fb_b[ix][iy], b);
            }
            int iy_up = iy + 1;
            if (iy_up >= 0 && iy_up < H) {
                int r = (head_r * weight_high) >> 8;
                int g = (head_g * weight_high) >> 8;
                int b = (head_b * weight_high) >> 8;
                fb_r[ix][iy_up] = qmax(fb_r[ix][iy_up], r);
                fb_g[ix][iy_up] = qmax(fb_g[ix][iy_up], g);
                fb_b[ix][iy_up] = qmax(fb_b[ix][iy_up], b);
            }
        }

        /* Seed a couple of trail pixels above the head (above = larger y).
         * Depth-fades toward the hue color, no white. */
        for (int t = 1; t <= 2; t++) {
            int ty = iy + t;
            if (ty < 0 || ty >= H) continue;
            int tx = ix;  /* small offset against drift for diagonal feel */
            tx -= (s->dx_q8 * t) >> 8;
            if (tx < 0) tx += W;
            if (tx >= W) tx -= W;
            if (tx < 0 || tx >= W) continue;

            int trail_v = 200 - t * 60;
            if (trail_v < 30) trail_v = 30;
            int tr, tg, tb;
            hsv2rgb(s->hue, s->sat ? s->sat : 60, trail_v, &tr, &tg, &tb);
            fb_r[tx][ty] = qmax(fb_r[tx][ty], tr);
            fb_g[tx][ty] = qmax(fb_g[tx][ty], tg);
            fb_b[tx][ty] = qmax(fb_b[tx][ty], tb);
        }
    }

    /* Render framebuffer with global brightness */
    for (int x = 0; x < W; x++) {
        for (int y = 0; y < H; y++) {
            int r = (fb_r[x][y] * bright) >> 8;
            int g = (fb_g[x][y] * bright) >> 8;
            int b = (fb_b[x][y] * bright) >> 8;
            set_pixel(x, y, r, g, b);
        }
    }

    draw();
}
