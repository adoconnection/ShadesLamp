#include "api.h"

/* ---- Metadata JSON ---- */
static const char META[] =
    "{\"name\":\"Random Blink\","
    "\"desc\":\"Random colored flashes\","
    "\"params\":["
        "{\"id\":0,\"name\":\"Speed\",\"type\":\"int\","
         "\"min\":1,\"max\":100,\"default\":50,"
         "\"desc\":\"Blink frequency\"},"
        "{\"id\":1,\"name\":\"Brightness\",\"type\":\"int\","
         "\"min\":1,\"max\":255,\"default\":200,"
         "\"desc\":\"Max brightness\"},"
        "{\"id\":2,\"name\":\"Fade\",\"type\":\"bool\","
         "\"default\":1,"
         "\"desc\":\"Smooth fade out\"}"
    "]}";

EXPORT(get_meta_ptr)
int get_meta_ptr(void) {
    return (int)META;
}

EXPORT(get_meta_len)
int get_meta_len(void) {
    return sizeof(META) - 1;
}

/* ---- Max supported pixels: 32x32 = 1024 ---- */
#define MAX_PIXELS 1024

/* Per-pixel state: target color RGB and current brightness (0-255) */
static int px_r[MAX_PIXELS];
static int px_g[MAX_PIXELS];
static int px_b[MAX_PIXELS];
static int px_bright[MAX_PIXELS];

/* PRNG state (xorshift32) */
static uint32_t rng_state;
static int initialized;

static uint32_t xorshift32(void) {
    uint32_t x = rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng_state = x;
    return x;
}

/* Return pseudo-random number in range [0, max) */
static int rand_range(int max) {
    if (max <= 0) return 0;
    return (int)(xorshift32() % (uint32_t)max);
}

EXPORT(init)
void init(void) {
    rng_state = 12345;
    initialized = 0;

    for (int i = 0; i < MAX_PIXELS; i++) {
        px_r[i] = 0;
        px_g[i] = 0;
        px_b[i] = 0;
        px_bright[i] = 0;
    }
}

EXPORT(update)
void update(int tick_ms) {
    int speed      = get_param_i32(0);
    int brightness = get_param_i32(1);
    int fade       = get_param_i32(2);
    int w = get_width();
    int h = get_height();
    int total = w * h;
    if (total > MAX_PIXELS) total = MAX_PIXELS;
    if (total < 1) total = 1;

    /* Seed PRNG from tick_ms on first real call for variety */
    if (!initialized) {
        rng_state = (uint32_t)tick_ms ^ 0xDEADBEEF;
        if (rng_state == 0) rng_state = 1;
        initialized = 1;
    }

    /* Determine how many pixels to light up this tick.
     * Higher speed = more pixels lit per frame.
     * At speed=100, light roughly total/4 pixels per tick.
     * At speed=1, light roughly 1 pixel every few ticks.
     */
    int lights_per_tick = (speed * total) / 400;
    if (lights_per_tick < 1) lights_per_tick = 1;

    /* Light up random pixels */
    for (int i = 0; i < lights_per_tick; i++) {
        int idx = rand_range(total);
        px_r[idx] = rand_range(256);
        px_g[idx] = rand_range(256);
        px_b[idx] = rand_range(256);
        px_bright[idx] = brightness;
    }

    /* Render pixels and apply fade */
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int idx = x + y * w;
            if (idx >= MAX_PIXELS) break;

            int cur = px_bright[idx];
            if (cur > 0) {
                /* Scale color by current brightness / 255 */
                int r = (px_r[idx] * cur) / 255;
                int g = (px_g[idx] * cur) / 255;
                int b = (px_b[idx] * cur) / 255;
                set_pixel(x, y, r, g, b);

                /* Apply fade: reduce brightness for next frame */
                if (fade) {
                    /* Fade rate depends on speed: faster speed = faster fade */
                    int fade_amount = 5 + speed / 5;
                    cur -= fade_amount;
                    if (cur < 0) cur = 0;
                    px_bright[idx] = cur;
                } else {
                    /* No fade: pixel stays on for a random duration, then turns off */
                    /* Simple approach: 10% chance to turn off each tick */
                    if (rand_range(100) < 10) {
                        px_bright[idx] = 0;
                    }
                }
            } else {
                set_pixel(x, y, 0, 0, 0);
            }
        }
    }

    draw();
}
