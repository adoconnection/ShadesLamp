#include "api.h"

/* ---- Metadata JSON ---- */
static const char META[] =
    "{\"name\":\"Matrix Rain\","
    "\"desc\":\"Classic Matrix movie green rain effect\","
    "\"params\":["
        "{\"id\":0,\"name\":\"Density\",\"type\":\"int\","
         "\"min\":1,\"max\":30,\"default\":10,"
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

/* ---- Framebuffer for fade trails ---- */
#define MAX_W 64
#define MAX_H 64

/* Green channel intensity per pixel (used as trail brightness) */
static uint8_t fb_g[MAX_W][MAX_H];

/* Per-column state: drop head Y position (fixed-point 8.8) */
#define MAX_DROPS 64
static int drop_y[MAX_DROPS];      /* head Y position in 1/256 units (top=0) */
static int drop_speed[MAX_DROPS];   /* speed in 1/256 units per tick */
static uint8_t drop_active[MAX_DROPS];

static int g_width, g_height;

EXPORT(init)
void init(void) {
    rng_state = 48271;
    g_width = get_width();
    g_height = get_height();
    if (g_width > MAX_W) g_width = MAX_W;
    if (g_height > MAX_H) g_height = MAX_H;

    for (int x = 0; x < MAX_W; x++)
        for (int y = 0; y < MAX_H; y++)
            fb_g[x][y] = 0;

    for (int i = 0; i < MAX_DROPS; i++)
        drop_active[i] = 0;
}

/* Saturating subtract */
static uint8_t qsub(uint8_t a, uint8_t b) {
    return (a > b) ? (uint8_t)(a - b) : 0;
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

    /* Fade all existing trails */
    int fade_amount = 18 + (10 - speed) * 3;  /* slower speed = faster fade for shorter trails */
    for (int x = 0; x < W; x++) {
        for (int y = 0; y < H; y++) {
            fb_g[x][y] = qsub(fb_g[x][y], (uint8_t)fade_amount);
        }
    }

    /* Move existing drops and paint head */
    int speed_inc = speed * 60;  /* speed in 1/256 units */
    for (int i = 0; i < W && i < MAX_DROPS; i++) {
        if (!drop_active[i]) {
            /* Chance to spawn a new drop in this column */
            if (random_range(0, 100) < density) {
                drop_active[i] = 1;
                drop_y[i] = 0;
                drop_speed[i] = speed_inc + random_range(-20, 20);
                if (drop_speed[i] < 30) drop_speed[i] = 30;
            }
            continue;
        }

        /* Current integer Y */
        int iy = drop_y[i] >> 8;

        if (iy >= 0 && iy < H) {
            /* Paint head as bright white-green */
            fb_g[i][iy] = 255;
        }

        /* Move drop down */
        drop_y[i] += drop_speed[i];
        int new_iy = drop_y[i] >> 8;

        /* Fill any skipped rows (for fast speeds) */
        for (int fill = iy + 1; fill <= new_iy && fill < H; fill++) {
            if (fill >= 0) fb_g[i][fill] = 255;
        }

        /* If drop fell off bottom, deactivate */
        if (new_iy >= H + 5) {
            drop_active[i] = 0;
        }
    }

    /* Render framebuffer to LEDs */
    for (int x = 0; x < W; x++) {
        for (int y = 0; y < H; y++) {
            int val = fb_g[x][y];
            if (val == 0) {
                set_pixel(x, y, 0, 0, 0);
                continue;
            }

            int r, g, b;

            /* Head pixel: bright white-green */
            if (val >= 240) {
                r = val * bright / 255;
                g = val * bright / 255;
                b = (val / 2) * bright / 255;
            }
            /* Upper trail: bright green */
            else if (val >= 140) {
                r = 0;
                g = val * bright / 255;
                b = 0;
            }
            /* Lower trail: dimmer green with slight teal tint */
            else {
                r = 0;
                g = val * bright / 255;
                b = (val / 8) * bright / 255;
            }

            set_pixel(x, y, r, g, b);
        }
    }

    draw();
}
