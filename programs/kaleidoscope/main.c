#include "api.h"

/*
 * Kaleidoscope - Symmetric rotating patterns with color transitions.
 * Pixels are colored based on angle and distance from center,
 * creating rotational symmetry on a cylindrical LED matrix.
 */

static const char META[] =
    "{\"name\":\"Kaleidoscope\","
    "\"desc\":\"Symmetric rotating patterns with beautiful color transitions\","
    "\"params\":["
        "{\"id\":0,\"name\":\"Speed\",\"type\":\"int\","
         "\"min\":1,\"max\":100,\"default\":30,"
         "\"desc\":\"Rotation speed\"},"
        "{\"id\":1,\"name\":\"Segments\",\"type\":\"int\","
         "\"min\":2,\"max\":8,\"default\":6,"
         "\"desc\":\"Number of symmetry segments\"},"
        "{\"id\":2,\"name\":\"Brightness\",\"type\":\"int\","
         "\"min\":1,\"max\":255,\"default\":220,"
         "\"desc\":\"Overall brightness\"},"
        "{\"id\":3,\"name\":\"Palette\",\"type\":\"int\","
         "\"min\":0,\"max\":3,\"default\":0,"
         "\"options\":[\"Rainbow\",\"Fire\",\"Ocean\",\"Forest\"],"
         "\"desc\":\"Color palette\"}"
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

static float fatan2(float y, float x) {
    if (x == 0.0f && y == 0.0f) return 0.0f;
    float abs_x = x < 0 ? -x : x;
    float abs_y = y < 0 ? -y : y;
    float a = abs_x < abs_y ? abs_x / abs_y : abs_y / abs_x;
    float s = a * a;
    float r = ((-0.0464964749f * s + 0.15931422f) * s - 0.327622764f) * s * a + a;
    if (abs_y > abs_x) r = HALF_PI - r;
    if (x < 0.0f) r = PI - r;
    if (y < 0.0f) r = -r;
    return r;
}

static float fabs_f(float x) { return x < 0.0f ? -x : x; }

static float fmod_f(float x, float m) {
    if (m == 0.0f) return 0.0f;
    while (x < 0.0f) x += m;
    while (x >= m) x -= m;
    return x;
}

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

/* ---- State ---- */
#define MAX_W 64
#define MAX_H 64

static float time_offset;
static int cur_w, cur_h;
static int32_t prev_tick;

EXPORT(init)
void init(void) {
    time_offset = 0.0f;
    prev_tick = 0;
    cur_w = get_width();
    cur_h = get_height();
    if (cur_w > MAX_W) cur_w = MAX_W;
    if (cur_h > MAX_H) cur_h = MAX_H;
}

/*
 * Palette functions: given a normalized value 0.0-1.0, return hue and saturation.
 * Rainbow:  full hue range, full saturation
 * Fire:     hue 0-40 (red-orange-yellow), high saturation
 * Ocean:    hue 130-180 (cyan-blue), high saturation
 * Forest:   hue 60-120 (green-teal), moderate saturation
 */
static void palette_color(int palette, float val, int *hue, int *sat) {
    /* val is 0.0 to 1.0 */
    int v = (int)(val * 255.0f);
    if (v < 0) v = 0;
    if (v > 255) v = 255;

    switch (palette) {
        case 0: /* Rainbow */
            *hue = v;
            *sat = 255;
            break;
        case 1: /* Fire */
            *hue = (v * 40) >> 8; /* 0-40 */
            *sat = 255;
            break;
        case 2: /* Ocean */
            *hue = 130 + ((v * 50) >> 8); /* 130-180 */
            *sat = 240;
            break;
        case 3: /* Forest */
            *hue = 60 + ((v * 60) >> 8); /* 60-120 */
            *sat = 220;
            break;
        default:
            *hue = v;
            *sat = 255;
            break;
    }
}

EXPORT(update)
void update(int tick_ms) {
    int speed    = get_param_i32(0);
    int segments = get_param_i32(1);
    int bright   = get_param_i32(2);
    int palette  = get_param_i32(3);

    cur_w = get_width();
    cur_h = get_height();
    if (cur_w > MAX_W) cur_w = MAX_W;
    if (cur_h > MAX_H) cur_h = MAX_H;
    if (cur_w < 1) cur_w = 1;
    if (cur_h < 1) cur_h = 1;
    if (segments < 2) segments = 2;
    if (segments > 8) segments = 8;
    if (palette < 0) palette = 0;
    if (palette > 3) palette = 3;

    /* Advance time */
    int32_t delta_ms = tick_ms - prev_tick;
    if (delta_ms <= 0 || delta_ms > 200) delta_ms = 33;
    prev_tick = tick_ms;
    float dt = (float)delta_ms / 1000.0f;
    time_offset += dt * (float)speed * 0.04f;
    /* Keep time_offset from growing unbounded */
    if (time_offset > 1000.0f) time_offset -= 1000.0f;

    float cx = (float)cur_w / 2.0f;
    float cy = (float)cur_h / 2.0f;
    float max_dist = fsqrt(cx * cx + cy * cy);
    if (max_dist < 1.0f) max_dist = 1.0f;

    /* Angular size of one segment */
    float seg_angle = TWO_PI / (float)segments;

    for (int x = 0; x < cur_w; x++) {
        for (int y = 0; y < cur_h; y++) {
            float dx = (float)x - cx;
            float dy = (float)y - cy;

            /* Distance from center, normalized 0-1 */
            float dist = fsqrt(dx * dx + dy * dy);
            float norm_dist = dist / max_dist;

            /* Angle from center: -PI to PI -> 0 to TWO_PI */
            float angle = fatan2(dy, dx);
            if (angle < 0.0f) angle += TWO_PI;

            /* Fold angle into a single segment for mirror symmetry */
            float seg_pos = fmod_f(angle, seg_angle);
            /* Mirror within segment: fold back after halfway */
            float half_seg = seg_angle / 2.0f;
            if (seg_pos > half_seg) {
                seg_pos = seg_angle - seg_pos;
            }
            /* Normalize folded angle: 0 to 1 */
            float folded = seg_pos / half_seg;

            /* Generate color value from folded angle, distance, and time */
            /* Multiple overlapping wave functions for complexity */
            float wave1 = fsin(folded * PI * 3.0f + time_offset * 2.0f + norm_dist * 8.0f);
            float wave2 = fcos(norm_dist * PI * 5.0f - time_offset * 1.5f + folded * 4.0f);
            float wave3 = fsin((folded + norm_dist) * PI * 2.0f + time_offset * 3.0f);

            /* Combine waves: result in -3..3, normalize to 0..1 */
            float combined = (wave1 + wave2 + wave3) / 6.0f + 0.5f;
            if (combined < 0.0f) combined = 0.0f;
            if (combined > 1.0f) combined = 1.0f;

            /* Add a slow global rotation component */
            float color_val = fmod_f(combined + time_offset * 0.1f, 1.0f);

            /* Look up palette color */
            int hue, sat;
            palette_color(palette, color_val, &hue, &sat);

            /* Modulate brightness by distance: slightly brighter near center */
            int val = bright - (int)(norm_dist * 40.0f);
            if (val < 1) val = 1;
            if (val > 255) val = 255;

            int r, g, b;
            hsv2rgb(hue, sat, val, &r, &g, &b);
            set_pixel(x, y, r, g, b);
        }
    }

    draw();
}
