#include "api.h"

/* ---- Metadata JSON ---- */
static const char META[] =
    "{\"name\":\"Snow\","
    "\"desc\":\"Snowflakes drifting down with horizontal wobble\","
    "\"params\":["
        "{\"id\":0,\"name\":\"Density\",\"type\":\"int\","
         "\"min\":1,\"max\":30,\"default\":15,"
         "\"desc\":\"How many snowflakes appear (more=heavier snow)\"},"
        "{\"id\":1,\"name\":\"Speed\",\"type\":\"int\","
         "\"min\":1,\"max\":10,\"default\":4,"
         "\"desc\":\"How fast snowflakes fall\"},"
        "{\"id\":2,\"name\":\"Brightness\",\"type\":\"int\","
         "\"min\":1,\"max\":255,\"default\":200,"
         "\"desc\":\"Overall brightness\"}"
    "]}";

EXPORT(get_meta_ptr)
int get_meta_ptr(void) { return (int)META; }

EXPORT(get_meta_len)
int get_meta_len(void) { return sizeof(META) - 1; }

/* ---- PRNG (xorshift32) ---- */
static uint32_t rng_state = 91573;

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

/* ---- Framebuffer ---- */
#define MAX_W 64
#define MAX_H 64

/* Snowflake particles */
#define MAX_FLAKES 128

static int flake_x[MAX_FLAKES];       /* X position, fixed-point 8.8 */
static int flake_y[MAX_FLAKES];       /* Y position, fixed-point 8.8 */
static uint8_t flake_bright[MAX_FLAKES]; /* Individual brightness (size) */
static uint8_t flake_active[MAX_FLAKES];
static int flake_wobble[MAX_FLAKES];   /* wobble phase counter */

/* Accumulation buffer at bottom row - brightness that fades */
static uint8_t accum[MAX_W];

EXPORT(init)
void init(void) {
    rng_state = 91573;
    for (int i = 0; i < MAX_FLAKES; i++)
        flake_active[i] = 0;
    for (int i = 0; i < MAX_W; i++)
        accum[i] = 0;
}

static uint8_t qsub(uint8_t a, uint8_t b) {
    return (a > b) ? (uint8_t)(a - b) : 0;
}

static uint8_t qadd(uint8_t a, uint8_t b) {
    int s = (int)a + (int)b;
    return (s > 255) ? 255 : (uint8_t)s;
}

/* Simple sine approximation for wobble: returns -1, 0, or 1 */
static int wobble_offset(int phase) {
    int p = phase & 15;
    if (p < 4)  return 0;
    if (p < 8)  return 1;
    if (p < 12) return 0;
    return -1;
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

    /* Clear display */
    for (int x = 0; x < W; x++)
        for (int y = 0; y < H; y++)
            set_pixel(x, y, 0, 0, 0);

    /* Spawn new flakes at top */
    int spawn_chance = density * 3;  /* out of 1000 */
    for (int x = 0; x < W; x++) {
        if (random_range(0, 1000) < spawn_chance) {
            /* Find a free slot */
            for (int i = 0; i < MAX_FLAKES; i++) {
                if (!flake_active[i]) {
                    flake_active[i] = 1;
                    flake_x[i] = x << 8;
                    flake_y[i] = (H - 1) << 8;  /* top of matrix */
                    flake_bright[i] = (uint8_t)random_range(100, 255);
                    flake_wobble[i] = random_range(0, 16);
                    break;
                }
            }
        }
    }

    /* Update and render flakes */
    int fall_speed = speed * 40 + 20;

    for (int i = 0; i < MAX_FLAKES; i++) {
        if (!flake_active[i]) continue;

        /* Move down */
        flake_y[i] -= fall_speed;

        /* Wobble horizontally */
        flake_wobble[i]++;
        int wx = wobble_offset(flake_wobble[i]);
        flake_x[i] += wx * 40;

        /* Wrap X horizontally (cylinder) */
        int ix = flake_x[i] >> 8;
        if (ix < 0) flake_x[i] += W << 8;
        if (ix >= W) flake_x[i] -= W << 8;

        ix = flake_x[i] >> 8;
        int iy = flake_y[i] >> 8;

        /* Hit bottom - accumulate and deactivate */
        if (iy <= 0) {
            if (ix >= 0 && ix < W) {
                accum[ix] = qadd(accum[ix], (uint8_t)(flake_bright[i] / 2));
            }
            flake_active[i] = 0;
            continue;
        }

        /* Render snowflake */
        if (ix >= 0 && ix < W && iy >= 0 && iy < H) {
            int fb = flake_bright[i] * bright / 255;
            /* White with slight blue tint */
            int r = fb * 220 / 255;
            int g = fb * 230 / 255;
            int b = fb;
            set_pixel(ix, iy, r, g, b);
        }
    }

    /* Render and fade accumulation at bottom */
    for (int x = 0; x < W; x++) {
        if (accum[x] > 0) {
            int fb = accum[x] * bright / 255;
            int r = fb * 200 / 255;
            int g = fb * 210 / 255;
            int b = fb;
            set_pixel(x, 0, r, g, b);
            /* Fade accumulation */
            accum[x] = qsub(accum[x], 3);
        }
    }

    draw();
}
