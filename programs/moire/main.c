#include "api.h"

/*
 * Moire - Two overlapping geometric patterns that create beautiful
 * moire interference as they slowly shift. Each pixel's color is
 * computed from the combination of two wave functions emanating
 * from slightly offset centers. No framebuffer needed.
 * Designed for a cylindrical LED matrix (Y=0 is bottom).
 */

static const char META[] =
    "{\"name\":\"Moire\","
    "\"desc\":\"Interference patterns from two overlapping wave sources\","
    "\"params\":["
        "{\"id\":0,\"name\":\"Speed\",\"type\":\"int\","
         "\"min\":1,\"max\":100,\"default\":25,"
         "\"desc\":\"Pattern shift speed\"},"
        "{\"id\":1,\"name\":\"Brightness\",\"type\":\"int\","
         "\"min\":1,\"max\":255,\"default\":220,"
         "\"desc\":\"Overall brightness\"},"
        "{\"id\":2,\"name\":\"Pattern\",\"type\":\"int\","
         "\"min\":0,\"max\":2,\"default\":0,"
         "\"options\":[\"Circles\",\"Lines\",\"Grid\"],"
         "\"desc\":\"Base pattern type\"},"
        "{\"id\":3,\"name\":\"Separation\",\"type\":\"int\","
         "\"min\":1,\"max\":30,\"default\":10,"
         "\"desc\":\"Distance between pattern centers\"}"
    "]}";

EXPORT(get_meta_ptr)
int get_meta_ptr(void) { return (int)META; }
EXPORT(get_meta_len)
int get_meta_len(void) { return sizeof(META) - 1; }

/* ---- Math helpers ---- */
#define TWO_PI   6.28318530f
#define PI       3.14159265f
#define HALF_PI  1.57079632f

static float fsin(float x) {
    while (x < 0.0f) x += TWO_PI;
    while (x >= TWO_PI) x -= TWO_PI;
    float sign = 1.0f;
    if (x > PI) { x -= PI; sign = -1.0f; }
    float num = 16.0f * x * (PI - x);
    float den = 5.0f * PI * PI - 4.0f * x * (PI - x);
    if (den == 0.0f) return 0.0f;
    return sign * num / den;
}

static float fcos(float x) { return fsin(x + HALF_PI); }

static float fsqrt(float x) {
    if (x <= 0.0f) return 0.0f;
    float guess = x * 0.5f;
    guess = 0.5f * (guess + x / guess);
    guess = 0.5f * (guess + x / guess);
    guess = 0.5f * (guess + x / guess);
    return guess;
}

static float fabs_f(float x) { return x < 0.0f ? -x : x; }

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

#define MAX_W 64
#define MAX_H 64

EXPORT(init)
void init(void) {
    /* Purely per-frame computation, nothing to initialize */
}

/*
 * Pattern functions: given a point (px, py) relative to a center,
 * return a wave value in the range -1.0 to 1.0.
 *
 * - Circles: concentric rings based on distance from center
 * - Lines:   parallel stripes based on a rotated axis
 * - Grid:    combination of horizontal and vertical stripes
 */

static float pattern_circles(float dx, float dy, float freq) {
    float dist = fsqrt(dx * dx + dy * dy);
    return fsin(dist * freq);
}

static float pattern_lines(float dx, float dy, float freq, float angle) {
    /* Project point onto the axis defined by angle */
    float proj = dx * fcos(angle) + dy * fsin(angle);
    return fsin(proj * freq);
}

static float pattern_grid(float dx, float dy, float freq) {
    /* Superposition of horizontal and vertical waves */
    float h = fsin(dx * freq);
    float v = fsin(dy * freq);
    return (h + v) * 0.5f;
}

EXPORT(update)
void update(int tick_ms) {
    int speed      = get_param_i32(0);
    int bright     = get_param_i32(1);
    int pattern    = get_param_i32(2);
    int separation = get_param_i32(3);

    int W = get_width();
    int H = get_height();
    if (W > MAX_W) W = MAX_W;
    if (H > MAX_H) H = MAX_H;
    if (W < 1) W = 1;
    if (H < 1) H = 1;
    if (pattern < 0) pattern = 0;
    if (pattern > 2) pattern = 2;
    if (separation < 1) separation = 1;
    if (separation > 30) separation = 30;

    /* Time phase */
    float t = (float)tick_ms * (float)speed * 0.00003f;

    /* Display center */
    float cx = (float)W * 0.5f;
    float cy = (float)H * 0.5f;

    /* Two pattern centers orbit slowly around the display center */
    float sep = (float)separation * 0.5f;
    /* Center A orbits clockwise */
    float ax = cx + fcos(t * 0.7f) * sep;
    float ay = cy + fsin(t * 0.7f) * sep;
    /* Center B orbits counter-clockwise at a different rate */
    float bx = cx + fcos(-t * 0.5f + PI) * sep;
    float by = cy + fsin(-t * 0.5f + PI) * sep;

    /* Wave spatial frequency: controls how tight the pattern rings/lines are */
    float freq = 1.2f;

    /* Slow rotation angle for line-based patterns */
    float angle_a = t * 0.3f;
    float angle_b = -t * 0.2f + PI * 0.5f;

    for (int x = 0; x < W; x++) {
        for (int y = 0; y < H; y++) {
            float px = (float)x;
            float py = (float)y;

            /* Displacement from each center */
            float da_x = px - ax;
            float da_y = py - ay;
            float db_x = px - bx;
            float db_y = py - by;

            /* Evaluate both patterns */
            float val_a, val_b;

            switch (pattern) {
                case 0: /* Circles */
                    val_a = pattern_circles(da_x, da_y, freq);
                    val_b = pattern_circles(db_x, db_y, freq * 1.05f);
                    break;
                case 1: /* Lines */
                    val_a = pattern_lines(da_x, da_y, freq, angle_a);
                    val_b = pattern_lines(db_x, db_y, freq, angle_b);
                    break;
                case 2: /* Grid */
                    val_a = pattern_grid(da_x, da_y, freq);
                    val_b = pattern_grid(db_x, db_y, freq * 0.95f);
                    break;
                default:
                    val_a = 0.0f;
                    val_b = 0.0f;
                    break;
            }

            /* Moire interference: multiply the two patterns */
            /* Product of two sine waves creates sum/difference frequencies */
            float interference = val_a * val_b;
            /* interference is in -1..1 range, normalize to 0..1 */
            float norm = interference * 0.5f + 0.5f;

            /* Add the absolute difference for extra detail */
            float diff = fabs_f(val_a - val_b) * 0.5f;

            /* Combine interference and difference for rich pattern */
            float combined = norm * 0.6f + diff * 0.4f;
            if (combined < 0.0f) combined = 0.0f;
            if (combined > 1.0f) combined = 1.0f;

            /* Color: map combined value to hue with time-based shift */
            /* The interference creates natural zones of constructive/destructive overlap */
            int hue = (int)(combined * 180.0f + t * 15.0f) & 0xFF;
            int sat = 200 + (int)(combined * 55.0f);
            if (sat > 255) sat = 255;

            /* Brightness modulation: brighter where patterns constructively interfere */
            float bright_mod = 0.3f + 0.7f * combined;
            int val = (int)((float)bright * bright_mod);
            if (val < 0) val = 0;
            if (val > 255) val = 255;

            int r, g, b;
            hsv2rgb(hue, sat, val, &r, &g, &b);
            set_pixel(x, y, r, g, b);
        }
    }

    draw();
}
