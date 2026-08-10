#include "api.h"

/*
 * Kaleidoscope - Symmetric rotating patterns with color transitions.
 * Pixels are colored based on angle and distance from center,
 * creating rotational symmetry on a cylindrical LED matrix.
 *
 * Optimized: the pixel->polar mapping (norm_dist, angle) is static per canvas
 * size, so it's baked into per-pixel LUTs rebuilt only when W/H change. The
 * three per-pixel sines/cosines go through a 256-entry sine table, and m_hsv is
 * replaced by a 256-entry palette LUT (built once per palette change) scaled by
 * the per-pixel brightness. Per-pixel host calls drop from ~7 to 0.
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

/* 256-entry sine table (built once in init via native m_sin), then inline
 * lookups replace per-pixel m_sin/m_cos. Linearly interpolated: here the sine
 * feeds the hue index, which wraps at the palette seam, so a raw 256-step lookup
 * would flip a few boundary pixels red<->magenta; interpolation keeps the error
 * negligible. SINT[256] duplicates SINT[0] as the interpolation guard. */
#define SIN_N 1024
static float SINT[SIN_N + 1];
static void init_sin(void) {
    for (int i = 0; i < SIN_N; i++) SINT[i] = m_sin((float)i * TWO_PI / (float)SIN_N);
    SINT[SIN_N] = SINT[0];
}
static inline float fsin(float a) {
    float idx = a * 162.9746617f + 16384.0f;  /* 1024/TWO_PI; 16384 = 16*1024, masks cleanly */
    int i = (int)idx;
    float f = idx - (float)i;
    i &= (SIN_N - 1);
    return SINT[i] + (SINT[i + 1] - SINT[i]) * f;
}
static inline float fcos(float a) { return fsin(a + HALF_PI); }

/* ---- State ---- */
#define MAX_W 64
#define MAX_H 64

static float time_offset;
static int cur_w, cur_h;
static int32_t prev_tick;

/* Per-pixel polar LUTs (norm_dist 0..1, angle 0..TWO_PI), static per canvas. */
static float NDIST[MAX_W * MAX_H];
static float ANG[MAX_W * MAX_H];
static int geo_w = -1, geo_h = -1;

/* Palette LUT at full value, indexed by color value 0..255. */
static uint8_t LR[256], LG[256], LB[256];
static int pal_cached = -1;

static uint8_t FB[MAX_W * MAX_H * 3];
EXPORT(get_framebuffer) int get_framebuffer(void) { return (int)FB; }

EXPORT(init)
void init(void) {
    init_sin();
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

/* Bake the palette into an RGB LUT at full value; per-pixel we scale by the
 * pixel's brightness. m_hsv channels are linear in value, so LUT*val/255
 * reproduces m_hsv(hue,sat,val) to within a couple of counts. */
static void build_pal(int palette) {
    if (palette == pal_cached) return;
    pal_cached = palette;
    for (int i = 0; i < 256; i++) {
        int hue, sat;
        palette_color(palette, (float)i / 255.0f, &hue, &sat);
        int c = m_hsv(hue & 0xFF, sat, 255);
        LR[i] = (c >> 16) & 255;
        LG[i] = (c >> 8) & 255;
        LB[i] = c & 255;
    }
}

/* Rebuild the polar LUTs when the canvas size changes (one-off m_hypot/m_atan2
 * per pixel instead of every frame). */
static void build_geo(int W, int H) {
    if (W == geo_w && H == geo_h) return;
    geo_w = W; geo_h = H;
    float cx = (float)W / 2.0f;
    float cy = (float)H / 2.0f;
    float max_dist = m_hypot(cx, cy);
    if (max_dist < 1.0f) max_dist = 1.0f;
    for (int x = 0; x < W; x++) {
        for (int y = 0; y < H; y++) {
            float dx = (float)x - cx;
            float dy = (float)y - cy;
            int p = y * W + x;
            NDIST[p] = m_hypot(dx, dy) / max_dist;
            float a = m_atan2(dy, dx);
            if (a < 0.0f) a += TWO_PI;
            ANG[p] = a;
        }
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

    build_geo(cur_w, cur_h);
    build_pal(palette);

    /* Angular size of one segment */
    float seg_angle = TWO_PI / (float)segments;
    float half_seg = seg_angle / 2.0f;

    /* Frame-constant time terms, hoisted out of the pixel loop. */
    float t2 = time_offset * 2.0f;
    float t15 = time_offset * 1.5f;
    float t3 = time_offset * 3.0f;
    /* Global color rotation, reduced to its fractional part once per frame. */
    float trot = time_offset * 0.1f;
    trot = trot - (float)(int)trot;
    if (trot < 0.0f) trot += 1.0f;

    int W = cur_w, H = cur_h;
    for (int x = 0; x < W; x++) {
        for (int y = 0; y < H; y++) {
            int p = y * W + x;
            float norm_dist = NDIST[p];
            float angle = ANG[p];

            /* Fold angle into a single segment for mirror symmetry */
            int k = (int)(angle / seg_angle);
            float seg_pos = angle - (float)k * seg_angle;
            if (seg_pos > half_seg) seg_pos = seg_angle - seg_pos;
            float folded = seg_pos / half_seg;

            /* Multiple overlapping wave functions for complexity */
            float wave1 = fsin(folded * PI * 3.0f + t2 + norm_dist * 8.0f);
            float wave2 = fcos(norm_dist * PI * 5.0f - t15 + folded * 4.0f);
            float wave3 = fsin((folded + norm_dist) * PI * 2.0f + t3);

            /* Combine waves: result in -3..3, normalize to 0..1 */
            float combined = (wave1 + wave2 + wave3) / 6.0f + 0.5f;
            if (combined < 0.0f) combined = 0.0f;
            if (combined > 1.0f) combined = 1.0f;

            /* Add a slow global rotation component (fmod by 1) */
            float color_val = combined + trot;
            if (color_val >= 1.0f) color_val -= 1.0f;

            int idx = (int)(color_val * 255.0f);
            if (idx < 0) idx = 0;
            if (idx > 255) idx = 255;

            /* Modulate brightness by distance: slightly brighter near center */
            int val = bright - (int)(norm_dist * 40.0f);
            if (val < 1) val = 1;
            if (val > 255) val = 255;

            int o = p * 3;
            FB[o]     = (uint8_t)(LR[idx] * val / 255);
            FB[o + 1] = (uint8_t)(LG[idx] * val / 255);
            FB[o + 2] = (uint8_t)(LB[idx] * val / 255);
        }
    }

    draw();
}
