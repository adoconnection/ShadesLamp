#include "api.h"

/* ---- Metadata JSON ---- */
static const char META[] =
    "{\"name\":\"Aurora\","
    "\"desc\":\"Flowing vertical curtains of northern lights that shimmer and drift\","
    "\"params\":["
        "{\"id\":0,\"name\":\"Speed\",\"type\":\"int\","
         "\"min\":1,\"max\":100,\"default\":30,"
         "\"desc\":\"Animation speed\"},"
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

/* ---- PRNG (xorshift32) ---- */
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

/* ---- Noise buffer for flicker ---- */
#define MAX_W 64
#define MAX_H 64

/* Per-column random phase offsets for variety */
static float col_phase[MAX_W];

EXPORT(init)
void init(void) {
    rng = 48271;
    /* Assign random phase offsets to each column */
    for (int i = 0; i < MAX_W; i++) {
        col_phase[i] = (float)(rng_next() % 1000) * TWO_PI / 1000.0f;
    }
}

EXPORT(update)
void update(int tick_ms) {
    int speed      = get_param_i32(0);
    int brightness = get_param_i32(1);
    int base_hue   = get_param_i32(2);
    int W = get_width();
    int H = get_height();

    if (W > MAX_W) W = MAX_W;
    if (H > MAX_H) H = MAX_H;
    if (W < 1) W = 1;
    if (H < 1) H = 1;

    /* Time variable: speed 1-100 mapped to a reasonable animation rate */
    float t = (float)tick_ms * (float)speed * 0.00004f;

    for (int x = 0; x < W; x++) {
        float fx = (float)x;
        float phase = col_phase[x];

        /* --- Column brightness from layered sine waves (curtain shape) --- */
        /* Wave 1: broad, slow-drifting curtain */
        float w1 = fsin(fx * 0.25f + t * 0.7f + phase);
        /* Wave 2: medium frequency, different speed */
        float w2 = fsin(fx * 0.45f - t * 1.1f + phase * 1.3f);
        /* Wave 3: higher frequency detail */
        float w3 = fsin(fx * 0.8f + t * 0.5f + phase * 0.7f);
        /* Wave 4: very slow broad envelope */
        float w4 = fcos(fx * 0.12f + t * 0.3f);

        /* Combine: w1 and w4 are dominant, w2 and w3 add detail */
        /* Range of each: -1..1, combined: roughly -4..4, normalize to 0..1 */
        float curtain = (w1 * 1.0f + w2 * 0.6f + w3 * 0.3f + w4 * 0.8f);
        /* Normalize from [-2.7, 2.7] to [0, 1] */
        curtain = (curtain + 2.7f) / 5.4f;
        if (curtain < 0.0f) curtain = 0.0f;
        if (curtain > 1.0f) curtain = 1.0f;

        /* Boost contrast: push values toward 0 or 1 */
        curtain = curtain * curtain;

        /* Slight flicker noise per column (changes each frame) */
        int noise = (int)(rng_next() % 20) - 10; /* -10..+9 */

        /* Per-column hue variation: subtle shift around base hue */
        int hue_offset = (int)(fsin(fx * 0.35f + t * 0.2f) * 18.0f);
        int col_hue = (base_hue + hue_offset) & 0xFF;

        for (int y = 0; y < H; y++) {
            float fy = (float)y;
            float fH = (float)H;

            /* --- Vertical profile: aurora hangs from the top --- */
            /* Brightest at top (y=0), fading toward bottom (y=H-1) */
            /* Use a power curve for natural-looking falloff */
            float y_norm = fy / fH;           /* 0 at top, 1 at bottom */
            float y_fade = 1.0f - y_norm;     /* 1 at top, 0 at bottom */
            y_fade = y_fade * y_fade;          /* Quadratic falloff */

            /* Add some vertical wave structure (shimmering curtain folds) */
            float vert_wave = fsin(fy * 0.5f + t * 1.5f + phase) * 0.15f + 0.85f;
            if (vert_wave < 0.0f) vert_wave = 0.0f;

            /* Combine all factors */
            float intensity = curtain * y_fade * vert_wave;
            int val = (int)(intensity * (float)brightness);

            /* Add flicker noise */
            val += noise;

            if (val < 0) val = 0;
            if (val > 255) val = 255;

            if (val < 2) {
                set_pixel(x, y, 0, 0, 0);
                continue;
            }

            /* Slightly less saturation near the bright peaks for ethereal look */
            int sat = 220;
            if (intensity > 0.7f) {
                /* Desaturate bright peaks a bit */
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
