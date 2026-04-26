#include "api.h"

/* ---- Metadata JSON ---- */
static const char META[] =
    "{\"name\":\"Flame\","
    "\"desc\":\"Fire2012 flame simulation\","
    "\"params\":["
        "{\"id\":0,\"name\":\"Cooling\",\"type\":\"int\","
         "\"min\":20,\"max\":150,\"default\":70,"
         "\"desc\":\"How much air cools as it rises (less=taller flames)\"},"
        "{\"id\":1,\"name\":\"Sparking\",\"type\":\"int\","
         "\"min\":50,\"max\":230,\"default\":130,"
         "\"desc\":\"Chance of new sparks (more=roaring fire)\"},"
        "{\"id\":2,\"name\":\"Brightness\",\"type\":\"int\","
         "\"min\":1,\"max\":255,\"default\":200,"
         "\"desc\":\"Overall brightness\"}"
    "]}";

EXPORT(get_meta_ptr)
int get_meta_ptr(void) { return (int)META; }

EXPORT(get_meta_len)
int get_meta_len(void) { return sizeof(META) - 1; }

/* ---- Fire palette (heat value 0-255 -> RGB) ---- */
/* Black -> Red -> Yellow -> White */
static void heat_to_rgb(int heat, int brightness, int *r, int *g, int *b) {
    int t = heat;
    if (t > 255) t = 255;
    if (t < 0) t = 0;

    int r0, g0, b0;

    if (t < 85) {
        /* Black to Red */
        r0 = t * 3;
        g0 = 0;
        b0 = 0;
    } else if (t < 170) {
        /* Red to Yellow */
        int s = t - 85;
        r0 = 255;
        g0 = s * 3;
        b0 = 0;
    } else {
        /* Yellow to White */
        int s = t - 170;
        r0 = 255;
        g0 = 255;
        b0 = s * 3;
        if (b0 > 255) b0 = 255;
    }

    *r = r0 * brightness / 255;
    *g = g0 * brightness / 255;
    *b = b0 * brightness / 255;
}

/* ---- Simple PRNG (xorshift32) ---- */
static uint32_t rng_state = 12345;

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

/* ---- Heat map: one column per x, one cell per y ---- */
/* Max supported: 64 columns x 64 rows = 4096 bytes in WASM linear memory */
#define MAX_W 64
#define MAX_H 64
static uint8_t heat[MAX_W][MAX_H];

EXPORT(init)
void init(void) {
    /* Clear heat map */
    for (int x = 0; x < MAX_W; x++)
        for (int y = 0; y < MAX_H; y++)
            heat[x][y] = 0;

    /* Seed RNG with something */
    rng_state = 73541;
}

/* Saturating subtract */
static uint8_t qsub(uint8_t a, uint8_t b) {
    return (a > b) ? a - b : 0;
}

/* Saturating add */
static uint8_t qadd(uint8_t a, uint8_t b) {
    int s = (int)a + (int)b;
    return (s > 255) ? 255 : (uint8_t)s;
}

EXPORT(update)
void update(int tick_ms) {
    int cooling  = get_param_i32(0);
    int sparking = get_param_i32(1);
    int bright   = get_param_i32(2);
    int W = get_width();
    int H = get_height();

    if (W > MAX_W) W = MAX_W;
    if (H > MAX_H) H = MAX_H;
    if (W < 1) W = 1;
    if (H < 1) H = 1;

    /* Seed RNG a little with tick to add variety */
    rng_state ^= (uint32_t)tick_ms;

    int fire_base = H / 6;
    if (fire_base < 2) fire_base = 2;
    if (fire_base > 6) fire_base = 6;

    /* Process each column independently */
    for (int x = 0; x < W; x++) {

        /* Step 1: Cool down every cell a little */
        for (int y = 0; y < H; y++) {
            int cooldown = random_range(0, ((cooling * 10) / H) + 2);
            heat[x][y] = qsub(heat[x][y], (uint8_t)cooldown);
        }

        /* Step 2: Heat drifts up and diffuses */
        for (int y = H - 1; y > 1; y--) {
            heat[x][y] = ((int)heat[x][y - 1] + (int)heat[x][y - 1] + (int)heat[x][y - 2]) / 3;
        }

        /* Step 3: Randomly ignite new sparks near the bottom */
        if (random8() < sparking) {
            int j = random_range(0, fire_base);
            heat[x][j] = qadd(heat[x][j], (uint8_t)random_range(160, 255));
        }
    }

    /* Step 4: Map heat to pixels with neighbor blending */
    for (int x = 0; x < W; x++) {
        int nx = (x + 1) % W; /* wrap horizontally (cylinder!) */
        for (int y = 0; y < H; y++) {
            /* Blend 70% this column + 30% neighbor for smoother look */
            int blended = ((int)heat[x][y] * 7 + (int)heat[nx][y] * 3) / 10;

            int r, g, b;
            heat_to_rgb(blended, bright, &r, &g, &b);
            set_pixel(x, y, r, g, b);
        }
    }

    draw();
}
