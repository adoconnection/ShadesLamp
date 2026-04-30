#include "api.h"

/* ---- Metadata JSON ---- */
static const char META[] =
    "{\"name\":\"Aurora\","
    "\"desc\":\"Flowing vertical curtains of northern lights that shimmer and drift\","
    "\"params\":["
        "{\"id\":0,\"name\":\"Speed\",\"type\":\"int\","
         "\"min\":1,\"max\":100,\"default\":50,"
         "\"desc\":\"Animation speed (higher = faster)\"},"
        "{\"id\":1,\"name\":\"Brightness\",\"type\":\"int\","
         "\"min\":1,\"max\":255,\"default\":200,"
         "\"desc\":\"Overall brightness\"},"
        "{\"id\":2,\"name\":\"Hue\",\"type\":\"int\","
         "\"min\":0,\"max\":255,\"default\":85,"
         "\"desc\":\"Base hue (85=green for classic aurora)\"}"
    "]}";

EXPORT(get_meta_ptr)
int get_meta_ptr(void) { return (int)META; }

EXPORT(get_meta_len)
int get_meta_len(void) { return sizeof(META) - 1; }

/* ---- PRNG (xorshift32) - used only at init for column phases ---- */
static uint32_t rng = 12345;

static uint32_t rng_next(void) {
    uint32_t x = rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng = x;
    return x;
}

/* ---- Sine approximation (Bhaskara I) ---- */
#define TWO_PI 6.28318530f
#define PI     3.14159265f

static float fsin(float x) {
    while (x < 0.0f)    x += TWO_PI;
    while (x >= TWO_PI) x -= TWO_PI;
    float sign = 1.0f;
    if (x > PI) { x -= PI; sign = -1.0f; }
    float num = 16.0f * x * (PI - x);
    float den = 5.0f * PI * PI - 4.0f * x * (PI - x);
    if (den == 0.0f) return 0.0f;
    return sign * num / den;
}

static float fcos(float x) { return fsin(x + 1.57079632f); }

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

/* ---- Per-column random phase offsets for variety ---- */
#define MAX_W 64
#define MAX_H 64
static float col_phase[MAX_W];

EXPORT(init)
void init(void) {
    rng = 48271;
    for (int i = 0; i < MAX_W; i++) {
        col_phase[i] = (float)(rng_next() % 1000) * TWO_PI / 1000.0f;
    }
}

EXPORT(update)
void update(int tick_ms) {
    int speed_param = get_param_i32(0);
    int brightness  = get_param_i32(1);
    int base_hue    = get_param_i32(2);
    int W = get_width();
    int H = get_height();

    if (W > MAX_W) W = MAX_W;
    if (H > MAX_H) H = MAX_H;
    if (W < 1) W = 1;
    if (H < 1) H = 1;

    /* Speed 1..100, higher = faster. tick_ms is total elapsed ms. */
    float t = (float)tick_ms * (float)speed_param * 0.00006f;

    /* Global breathing: entire aurora swells and fades smoothly */
    float global_pulse = fsin(t * 0.35f) * 0.25f + 0.75f; /* 0.50 .. 1.00 */

    for (int x = 0; x < W; x++) {
        float fx = (float)x;
        float phase = col_phase[x];

        /* Curtain shape: layered slow sines */
        float w1 = fsin(fx * 0.20f + t * 0.55f + phase);
        float w2 = fsin(fx * 0.42f - t * 0.80f + phase * 1.3f);
        float w3 = fsin(fx * 0.85f + t * 0.40f + phase * 0.7f);
        float w4 = fcos(fx * 0.10f + t * 0.22f);

        float curtain = (w1 * 1.0f + w2 * 0.6f + w3 * 0.3f + w4 * 0.8f);
        curtain = (curtain + 2.7f) / 5.4f;
        if (curtain < 0.0f) curtain = 0.0f;
        if (curtain > 1.0f) curtain = 1.0f;
        curtain = curtain * curtain;

        /* Smooth shimmer (replaces per-frame random noise) */
        float shimmer1 = fsin(fx * 1.7f + t * 1.9f + phase * 1.7f) * 0.10f;
        float shimmer2 = fsin(fx * 2.6f - t * 1.3f + phase * 2.3f) * 0.06f;
        float shimmer = shimmer1 + shimmer2;

        /* Per-column hue variation: subtle drift */
        int hue_offset = (int)(fsin(fx * 0.35f + t * 0.18f) * 18.0f);
        int col_hue = (base_hue + hue_offset) & 0xFF;

        for (int y = 0; y < H; y++) {
            float fy = (float)y;
            float fH = (float)H;

            /* Original aurora hangs from the "top" of its source frame.
             * y_norm 0 at y=0, 1 at y=H-1. Brightest where y_norm small.
             * (Preserves the established look regardless of physical orientation.) */
            float y_norm = fy / fH;
            float y_fade = 1.0f - y_norm;
            y_fade = y_fade * y_fade;

            /* Vertical wave structure (shimmering folds), smooth */
            float vert_wave = fsin(fy * 0.45f + t * 1.20f + phase) * 0.16f + 0.84f;
            if (vert_wave < 0.0f) vert_wave = 0.0f;

            /* Combine factors smoothly */
            float intensity = curtain * y_fade * vert_wave * global_pulse;
            intensity += shimmer * y_fade * 0.3f;
            if (intensity < 0.0f) intensity = 0.0f;
            if (intensity > 1.0f) intensity = 1.0f;

            int val = (int)(intensity * (float)brightness);
            if (val < 0) val = 0;
            if (val > 255) val = 255;

            if (val < 2) {
                set_pixel(x, y, 0, 0, 0);
                continue;
            }

            int sat = 220;
            if (intensity > 0.7f) {
                int desat = (int)((intensity - 0.7f) * 200.0f);
                sat -= desat;
                if (sat < 120) sat = 120;
            }

            int r, g, b;
            hsv2rgb(col_hue, sat, val, &r, &g, &b);
            set_pixel(x, y, r, g, b);
        }
    }

    draw();
}
