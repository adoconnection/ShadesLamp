#include "api.h"

/* ---- Metadata JSON ---- */
static const char META[] =
    "{\"name\":\"Colored Rain\","
    "\"desc\":\"Matrix rain with colorful trails — each column gets a random hue\","
    "\"params\":["
        "{\"id\":0,\"name\":\"Density\",\"type\":\"int\","
         "\"min\":1,\"max\":30,\"default\":12,"
         "\"desc\":\"How often new drops spawn (more=denser rain)\"},"
        "{\"id\":1,\"name\":\"Speed\",\"type\":\"int\","
         "\"min\":1,\"max\":10,\"default\":5,"
         "\"desc\":\"How fast drops fall\"},"
        "{\"id\":2,\"name\":\"Brightness\",\"type\":\"int\","
         "\"min\":1,\"max\":255,\"default\":200,"
         "\"desc\":\"Overall brightness\"}"
    "]}";

EXPORT(get_meta_ptr)
int get_meta_ptr(void) { return (int)META; }

EXPORT(get_meta_len)
int get_meta_len(void) { return sizeof(META) - 1; }

/* ---- PRNG (xorshift32) ---- */
static uint32_t rng_state = 62917;

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

/* Per-pixel brightness (0-255), represents trail intensity */
static uint8_t fb_val[MAX_W][MAX_H];

/* Per-column hue and drop state */
#define MAX_DROPS 64

static int drop_y[MAX_DROPS];          /* head Y position in 1/256 units */
static int drop_speed[MAX_DROPS];       /* fall speed in 1/256 units */
static uint8_t drop_hue[MAX_DROPS];    /* column hue */
static uint8_t drop_active[MAX_DROPS];

static uint8_t qsub(uint8_t a, uint8_t b) {
    return (a > b) ? (uint8_t)(a - b) : 0;
}

EXPORT(init)
void init(void) {
    rng_state = 62917;

    for (int x = 0; x < MAX_W; x++)
        for (int y = 0; y < MAX_H; y++)
            fb_val[x][y] = 0;

    for (int i = 0; i < MAX_DROPS; i++)
        drop_active[i] = 0;
}

EXPORT(update)
void update(int tick_ms) {
    int density = get_param_i32(0);   /* 1-30 */
    int speed   = get_param_i32(1);   /* 1-10 */
    int bright  = get_param_i32(2);   /* 1-255 */
    int W = get_width();
    int H = get_height();
    if (W > MAX_W) W = MAX_W;
    if (H > MAX_H) H = MAX_H;
    if (W < 1) W = 1;
    if (H < 1) H = 1;

    rng_state ^= (uint32_t)tick_ms;

    /* Fade all trails */
    int fade_amount = 16 + (10 - speed) * 3;
    for (int x = 0; x < W; x++) {
        for (int y = 0; y < H; y++) {
            fb_val[x][y] = qsub(fb_val[x][y], (uint8_t)fade_amount);
        }
    }

    /* Move drops and paint heads */
    int speed_inc = speed * 60;
    for (int i = 0; i < W && i < MAX_DROPS; i++) {
        if (!drop_active[i]) {
            /* Chance to spawn */
            if (random_range(0, 100) < density) {
                drop_active[i] = 1;
                drop_y[i] = (H - 1) << 8;
                drop_speed[i] = speed_inc + random_range(-20, 20);
                if (drop_speed[i] < 30) drop_speed[i] = 30;
                drop_hue[i] = (uint8_t)random8(); /* random color for this drop */
            }
            continue;
        }

        int iy = drop_y[i] >> 8;

        /* Paint head */
        if (iy >= 0 && iy < H) {
            fb_val[i][iy] = 255;
        }

        /* Move */
        drop_y[i] -= drop_speed[i];
        int new_iy = drop_y[i] >> 8;

        /* Fill skipped rows */
        for (int fill = iy - 1; fill >= new_iy && fill >= 0; fill--) {
            if (fill < H) fb_val[i][fill] = 255;
        }

        /* Deactivate if off bottom */
        if (new_iy < -5) {
            drop_active[i] = 0;
        }
    }

    /* Render framebuffer */
    for (int x = 0; x < W; x++) {
        /* Determine hue for this column - use the active drop's hue,
           or the last known hue (it stays in drop_hue[x]) */
        int hue = drop_hue[x];

        for (int y = 0; y < H; y++) {
            int val = fb_val[x][y];
            if (val == 0) {
                set_pixel(x, y, 0, 0, 0);
                continue;
            }

            int r, g, b;

            if (val >= 240) {
                /* Head: white-ish tinted with the hue */
                int hr, hg, hb;
                hsv_to_rgb(hue, 80, 255, &hr, &hg, &hb);
                r = hr * bright / 255;
                g = hg * bright / 255;
                b = hb * bright / 255;
            } else {
                /* Trail: saturated color, brightness proportional to val */
                int sat = 220;
                if (val < 80) sat = 255;  /* deeper saturation for fading tail */
                hsv_to_rgb(hue, sat, val, &r, &g, &b);
                r = r * bright / 255;
                g = g * bright / 255;
                b = b * bright / 255;
            }

            set_pixel(x, y, r, g, b);
        }
    }

    draw();
}
