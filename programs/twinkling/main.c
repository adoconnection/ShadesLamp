#include "api.h"

/* ---- Metadata JSON ---- */
static const char META[] =
    "{\"name\":\"Twinkling Stars\","
    "\"desc\":\"Stars appear and smoothly brighten then dim against dark sky\","
    "\"params\":["
        "{\"id\":0,\"name\":\"Density\",\"type\":\"int\","
         "\"min\":1,\"max\":30,\"default\":15,"
         "\"desc\":\"Star spawn density per frame\"},"
        "{\"id\":1,\"name\":\"Speed\",\"type\":\"int\","
         "\"min\":1,\"max\":10,\"default\":5,"
         "\"desc\":\"Twinkle animation speed\"},"
        "{\"id\":2,\"name\":\"Brightness\",\"type\":\"int\","
         "\"min\":1,\"max\":255,\"default\":200,"
         "\"desc\":\"Maximum star brightness\"}"
    "]}";

EXPORT(get_meta_ptr)
int get_meta_ptr(void) { return (int)META; }

EXPORT(get_meta_len)
int get_meta_len(void) { return sizeof(META) - 1; }

/* ---- PRNG (xorshift32) ---- */
static uint32_t rng_state = 55391;

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
    h = h & 255;
    if (s == 0) { *r = *g = *b = v; return; }
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

/* ---- Star state ----
 * Each pixel can hold a star. We track:
 * - phase: 0 = empty, 1..255 = active star lifecycle
 * - hue: color of the star
 * - max_phase: how many ticks the full cycle lasts (rise+fall)
 * Using a compact per-pixel array
 */
#define MAX_W 64
#define MAX_H 64

/* Star phase: 0=off, 1..max = triangle envelope position */
static uint8_t star_phase[MAX_W * MAX_H];
/* Star hue (0=white, or colored) */
static uint8_t star_hue[MAX_W * MAX_H];
/* Star saturation (0=white, 255=fully colored) */
static uint8_t star_sat[MAX_W * MAX_H];
/* Star max phase (total lifecycle length) */
static uint8_t star_max[MAX_W * MAX_H];

EXPORT(init)
void init(void) {
    int total = MAX_W * MAX_H;
    for (int i = 0; i < total; i++) {
        star_phase[i] = 0;
        star_hue[i] = 0;
        star_sat[i] = 0;
        star_max[i] = 0;
    }
}

EXPORT(update)
void update(int tick_ms) {
    int density = get_param_i32(0);
    int speed   = get_param_i32(1);
    int bright  = get_param_i32(2);

    int W = get_width();
    int H = get_height();
    if (W > MAX_W) W = MAX_W;
    if (H > MAX_H) H = MAX_H;
    if (W < 1) W = 1;
    if (H < 1) H = 1;

    rng_state ^= (uint32_t)tick_ms;

    int total_pixels = W * H;

    /* Advance existing stars */
    int advance = speed;  /* phase steps per frame */
    if (advance < 1) advance = 1;

    for (int i = 0; i < total_pixels; i++) {
        if (star_phase[i] > 0) {
            int new_phase = (int)star_phase[i] + advance;
            if (new_phase >= (int)star_max[i]) {
                star_phase[i] = 0;  /* star finished, go dark */
            } else {
                star_phase[i] = (uint8_t)new_phase;
            }
        }
    }

    /* Spawn new stars */
    /* density controls probability: higher = more stars spawned per frame */
    int spawn_count = density * total_pixels / 800;
    if (spawn_count < 1) spawn_count = 1;

    for (int s = 0; s < spawn_count; s++) {
        /* Random chance based on density */
        if (random8() < (int)(density * 8)) {
            int idx = random_range(0, total_pixels);
            if (star_phase[idx] == 0) {
                /* Spawn a new star */
                star_phase[idx] = 1;
                /* Lifecycle length: 40-120 phase ticks */
                star_max[idx] = (uint8_t)random_range(40, 120);

                /* Color: mostly white with occasional colored stars */
                int color_chance = random8();
                if (color_chance < 60) {
                    /* Colored star */
                    star_hue[idx] = (uint8_t)random8();
                    star_sat[idx] = (uint8_t)random_range(100, 200);
                } else {
                    /* White star */
                    star_hue[idx] = 0;
                    star_sat[idx] = 0;
                }
            }
        }
    }

    /* Render */
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            int idx = y * W + x;
            if (star_phase[idx] == 0) {
                /* Dark background with very subtle blue tint */
                set_pixel(x, y, 0, 0, 1);
            } else {
                /* Triangle envelope: rise to peak at halfway, then fall */
                int phase = (int)star_phase[idx];
                int max_p = (int)star_max[idx];
                int half = max_p / 2;
                int envelope;
                if (half == 0) half = 1;

                if (phase <= half) {
                    /* Rising */
                    envelope = phase * 255 / half;
                } else {
                    /* Falling */
                    envelope = (max_p - phase) * 255 / (max_p - half);
                }
                if (envelope > 255) envelope = 255;
                if (envelope < 0) envelope = 0;

                /* Apply brightness */
                int val = envelope * bright / 255;
                if (val > 255) val = 255;

                int r, g, b;
                if (star_sat[idx] == 0) {
                    /* White star */
                    r = val;
                    g = val;
                    b = val;
                } else {
                    /* Colored star */
                    hsv_to_rgb(star_hue[idx], star_sat[idx], val, &r, &g, &b);
                }

                set_pixel(x, y, r, g, b);
            }
        }
    }

    draw();
}
