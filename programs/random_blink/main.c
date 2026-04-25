#include "api.h"

/* ---- Metadata JSON ---- */
static const char META[] =
    "{\"name\":\"Random Blink\","
    "\"desc\":\"Random color flash with smooth fade out\","
    "\"params\":["
        "{\"id\":0,\"name\":\"Fade Time\",\"type\":\"int\","
         "\"min\":200,\"max\":3000,\"default\":1000,"
         "\"desc\":\"Fade out duration (ms)\"},"
        "{\"id\":1,\"name\":\"Pause Time\",\"type\":\"int\","
         "\"min\":0,\"max\":3000,\"default\":500,"
         "\"desc\":\"Dark pause between flashes (ms)\"},"
        "{\"id\":2,\"name\":\"Brightness\",\"type\":\"int\","
         "\"min\":1,\"max\":255,\"default\":200,"
         "\"desc\":\"Peak brightness\"}"
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

/* Per-pixel state */
static int px_r[MAX_PIXELS];
static int px_g[MAX_PIXELS];
static int px_b[MAX_PIXELS];
static int px_phase[MAX_PIXELS];  /* 0=fading, 1=pausing */
static int px_timer[MAX_PIXELS];  /* frames remaining in current phase */
static int px_total_frames[MAX_PIXELS]; /* total frames for current phase (for fade curve) */

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

static int rand_range(int max) {
    if (max <= 0) return 0;
    return (int)(xorshift32() % (uint32_t)max);
}

static void pick_new_color(int idx) {
    px_r[idx] = rand_range(256);
    px_g[idx] = rand_range(256);
    px_b[idx] = rand_range(256);
}

EXPORT(init)
void init(void) {
    rng_state = 12345;
    initialized = 0;

    for (int i = 0; i < MAX_PIXELS; i++) {
        px_r[i] = 0;
        px_g[i] = 0;
        px_b[i] = 0;
        px_phase[i] = 1;  /* start in pause so each pixel picks its own color */
        px_timer[i] = 0;
        px_total_frames[i] = 1;
    }
}

EXPORT(update)
void update(int tick_ms) {
    int fade_ms    = get_param_i32(0);  /* 200..3000 */
    int pause_ms   = get_param_i32(1);  /* 0..3000 */
    int brightness = get_param_i32(2);  /* 1..255 */

    int w = get_width();
    int h = get_height();
    int total = w * h;
    if (total > MAX_PIXELS) total = MAX_PIXELS;
    if (total < 1) total = 1;

    /* Clamp params */
    if (fade_ms < 200) fade_ms = 200;
    if (pause_ms < 0) pause_ms = 0;
    if (brightness < 1) brightness = 1;
    if (brightness > 255) brightness = 255;

    /* ~30 FPS → 33ms per frame */
    int fade_frames = fade_ms / 33;
    if (fade_frames < 1) fade_frames = 1;
    int pause_frames = pause_ms / 33;

    /* Seed PRNG from tick_ms on first call */
    if (!initialized) {
        rng_state = (uint32_t)tick_ms ^ 0xDEADBEEF;
        if (rng_state == 0) rng_state = 1;

        /* Stagger pixels so they don't all flash at once */
        for (int i = 0; i < total; i++) {
            int stagger = rand_range(fade_frames + pause_frames);
            if (stagger < pause_frames) {
                px_phase[i] = 1;
                px_timer[i] = stagger;
                px_total_frames[i] = pause_frames > 0 ? pause_frames : 1;
            } else {
                px_phase[i] = 0;
                px_timer[i] = stagger - pause_frames;
                px_total_frames[i] = fade_frames;
                pick_new_color(i);
            }
        }
        initialized = 1;
    }

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int idx = x + y * w;
            if (idx >= MAX_PIXELS) break;

            if (px_phase[idx] == 0) {
                /* FADING: brightness goes from max to 0 */
                int t = px_timer[idx];
                int tf = px_total_frames[idx];

                /* Quadratic ease-out curve for smoother fade */
                /* fraction = (tf - t) / tf, brightness = fraction^2 * max */
                int remaining = tf - t;
                if (remaining < 0) remaining = 0;
                int br = (brightness * remaining * remaining) / (tf * tf);

                int r = (px_r[idx] * br) / 255;
                int g = (px_g[idx] * br) / 255;
                int b = (px_b[idx] * br) / 255;
                set_pixel(x, y, r, g, b);

                px_timer[idx] = t + 1;
                if (t + 1 >= tf) {
                    /* Fade complete → enter pause */
                    px_phase[idx] = 1;
                    px_timer[idx] = 0;
                    px_total_frames[idx] = pause_frames > 0 ? pause_frames : 1;

                    /* Add some randomness to pause duration (±30%) */
                    if (pause_frames > 0) {
                        int variation = pause_frames * 30 / 100;
                        if (variation > 0) {
                            int adj = rand_range(variation * 2 + 1) - variation;
                            int pf = pause_frames + adj;
                            if (pf < 1) pf = 1;
                            px_total_frames[idx] = pf;
                        }
                    }
                }
            } else {
                /* PAUSING: stay dark */
                set_pixel(x, y, 0, 0, 0);

                px_timer[idx]++;
                if (px_timer[idx] >= px_total_frames[idx]) {
                    /* Pause complete → pick new color, start fade */
                    pick_new_color(idx);
                    px_phase[idx] = 0;
                    px_timer[idx] = 0;
                    px_total_frames[idx] = fade_frames;

                    /* Add some randomness to fade duration (±20%) */
                    int variation = fade_frames * 20 / 100;
                    if (variation > 0) {
                        int adj = rand_range(variation * 2 + 1) - variation;
                        int ff = fade_frames + adj;
                        if (ff < 1) ff = 1;
                        px_total_frames[idx] = ff;
                    }
                }
            }
        }
    }

    draw();
}
