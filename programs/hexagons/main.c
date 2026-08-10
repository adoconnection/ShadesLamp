#include "api.h"

/*
 * Hexagons - Hexagonal grid pattern with color waves pulsing outward
 * from the center. Uses axial hex coordinates to map each pixel to
 * a hex cell, then colors based on cell distance from center + time.
 * Designed for a cylindrical LED matrix (Y=0 is bottom).
 *
 * Optimized: the hex geometry (which cell a pixel belongs to, its distance
 * from the centre cell, the in-cell edge factor and cell identity) is static
 * per canvas size / scale, so it's baked into per-pixel byte LUTs rebuilt only
 * when W/H/scale change. The two per-pixel sines go through a 256-entry sine
 * table, and m_hsv is replaced by a 256-entry palette LUT scaled by the
 * per-pixel brightness. Per-pixel host calls drop from ~4 to 0.
 */

static const char META[] =
    "{\"name\":\"Hexagons\","
    "\"desc\":\"Hexagonal grid with color waves rippling from center\","
    "\"params\":["
        "{\"id\":0,\"name\":\"Speed\",\"type\":\"int\","
         "\"min\":1,\"max\":100,\"default\":30,"
         "\"desc\":\"Wave propagation speed\"},"
        "{\"id\":1,\"name\":\"Brightness\",\"type\":\"int\","
         "\"min\":1,\"max\":255,\"default\":220,"
         "\"desc\":\"Overall brightness\"},"
        "{\"id\":2,\"name\":\"Palette\",\"type\":\"int\","
         "\"min\":0,\"max\":3,\"default\":0,"
         "\"options\":[\"Rainbow\",\"Fire\",\"Ocean\",\"Forest\"],"
         "\"desc\":\"Color palette\"},"
        "{\"id\":3,\"name\":\"Scale\",\"type\":\"int\","
         "\"min\":1,\"max\":20,\"default\":8,"
         "\"desc\":\"Hexagon cell size\"}"
    "]}";

EXPORT(get_meta_ptr)
int get_meta_ptr(void) { return (int)META; }
EXPORT(get_meta_len)
int get_meta_len(void) { return sizeof(META) - 1; }

/* ---- Math helpers ---- */
#define TWO_PI   6.28318530f
#define PI       3.14159265f
#define HALF_PI  1.57079632f
#define SQRT3    1.73205080f
#define SQRT3_2  0.86602540f  /* sqrt(3)/2 */

/* 256-entry sine table (built once in init via native m_sin). Linearly
 * interpolated: the sine feeds the hue index (which wraps at the palette seam),
 * so a raw 256-step lookup would flip a few boundary pixels; interpolation keeps
 * the error negligible. SINT[256] duplicates SINT[0] as the interpolation guard. */
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

static float fabs_f(float x) { return x < 0.0f ? -x : x; }

/* Round float to nearest int */
static int round_f(float x) {
    if (x >= 0.0f) return (int)(x + 0.5f);
    return -(int)(-x + 0.5f);
}

/* Floor float */
static float floor_f(float x) {
    int i = (int)x;
    if ((float)i > x) i--;
    return (float)i;
}

/* ---- Palette functions ---- */
static void palette_color(int palette, float val, int *hue, int *sat) {
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
static uint8_t LR[256], LG[256], LB[256];
static int pal_cached = -1;
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

/* ---- Hex coordinate helpers ---- */

/*
 * Axial hex coordinates (q, r) with flat-top hexagons.
 *
 * To convert pixel (px, py) to fractional axial coords:
 *   q = (2/3 * px) / size
 *   r = (-1/3 * px + sqrt(3)/3 * py) / size
 *
 * Then round to nearest hex cell using cube-coordinate rounding.
 */

/* Cube round: convert fractional axial (fq, fr) to integer axial (q, r) */
static void hex_round(float fq, float fr, int *oq, int *or_) {
    /* Convert axial to cube: x=q, z=r, y=-x-z */
    float fx = fq;
    float fz = fr;
    float fy = -fx - fz;

    int rx = round_f(fx);
    int ry = round_f(fy);
    int rz = round_f(fz);

    float dx = fabs_f((float)rx - fx);
    float dy = fabs_f((float)ry - fy);
    float dz = fabs_f((float)rz - fz);

    /* Fix the coordinate with the largest rounding error */
    if (dx > dy && dx > dz) {
        rx = -ry - rz;
    } else if (dy > dz) {
        ry = -rx - rz;
    } else {
        rz = -rx - ry;
    }

    *oq = rx;
    *or_ = rz;
}

/* Hex distance from origin in cube coords: (|q| + |q+r| + |r|) / 2 */
static int hex_distance(int q, int r) {
    int aq = q < 0 ? -q : q;
    int ar = r < 0 ? -r : r;
    int as = (q + r) < 0 ? -(q + r) : (q + r);
    return (aq + ar + as) / 2;
}

/* ---- State ---- */
#define MAX_W 64
#define MAX_H 64

/* Per-pixel static geometry LUTs, rebuilt when W/H/scale change. */
static uint8_t HDIST[MAX_W * MAX_H];   /* hex distance from centre cell */
static uint8_t HQMOD[MAX_W * MAX_H];   /* hq & 3 */
static uint8_t CELLID[MAX_W * MAX_H];  /* (hq*7 + hr*13) & 0xFF */
static uint8_t EDGE[MAX_W * MAX_H];    /* edge_factor * 255 */
static int geo_w = -1, geo_h = -1, geo_scale = -1;

static uint8_t FB[MAX_W * MAX_H * 3];
EXPORT(get_framebuffer) int get_framebuffer(void) { return (int)FB; }

EXPORT(init)
void init(void) {
    init_sin();
}

/* Rebuild the hex-geometry LUTs (one-off m_hypot + hex rounding per pixel
 * instead of every frame). */
static void build_geo(int W, int H, int scale) {
    if (W == geo_w && H == geo_h && scale == geo_scale) return;
    geo_w = W; geo_h = H; geo_scale = scale;

    float hex_size = (float)scale * 0.5f + 1.0f;
    float cx = (float)W * 0.5f;
    float cy = (float)H * 0.5f;

    /* Find center hex cell for reference */
    float center_fq = (2.0f / 3.0f * cx) / hex_size;
    float center_fr = (-1.0f / 3.0f * cx + SQRT3 / 3.0f * cy) / hex_size;
    int center_q, center_r;
    hex_round(center_fq, center_fr, &center_q, &center_r);

    for (int x = 0; x < W; x++) {
        for (int y = 0; y < H; y++) {
            float px = (float)x;
            float py = (float)y;

            /* Convert pixel to fractional axial hex coordinates, round to cell */
            float fq = (2.0f / 3.0f * px) / hex_size;
            float fr = (-1.0f / 3.0f * px + SQRT3 / 3.0f * py) / hex_size;
            int hq, hr;
            hex_round(fq, fr, &hq, &hr);

            /* Distance of this hex cell from the center hex cell */
            int dist = hex_distance(hq - center_q, hr - center_r);
            if (dist > 255) dist = 255;

            /* Distance from pixel to hex cell center for edge detection */
            float cell_px = hex_size * (3.0f / 2.0f * (float)hq);
            float cell_py = hex_size * (SQRT3_2 * (float)hq + SQRT3 * (float)hr);
            float dx = px - cell_px;
            float dy = py - cell_py;
            float pixel_dist = m_hypot(dx, dy);

            float inner_dist = pixel_dist / (hex_size * 0.9f);
            if (inner_dist > 1.0f) inner_dist = 1.0f;

            /* Edge darkening: dim pixels near hexagon boundaries */
            float edge_factor;
            if (inner_dist > 0.55f) {
                edge_factor = 1.0f - (inner_dist - 0.55f) * 2.2f;
                if (edge_factor < 0.0f) edge_factor = 0.0f;
            } else {
                edge_factor = 1.0f;
            }

            int p = y * W + x;
            HDIST[p]  = (uint8_t)dist;
            HQMOD[p]  = (uint8_t)(hq & 3);
            CELLID[p] = (uint8_t)((hq * 7 + hr * 13) & 0xFF);
            EDGE[p]   = (uint8_t)(edge_factor * 255.0f + 0.5f);
        }
    }
}

EXPORT(update)
void update(int tick_ms) {
    int speed   = get_param_i32(0);
    int bright  = get_param_i32(1);
    int palette = get_param_i32(2);
    int scale   = get_param_i32(3);

    int W = get_width();
    int H = get_height();
    if (W > MAX_W) W = MAX_W;
    if (H > MAX_H) H = MAX_H;
    if (W < 1) W = 1;
    if (H < 1) H = 1;
    if (palette < 0) palette = 0;
    if (palette > 3) palette = 3;
    if (scale < 1) scale = 1;
    if (scale > 20) scale = 20;

    build_geo(W, H, scale);
    build_pal(palette);

    /* Time phase for wave animation */
    float t = (float)tick_ms * (float)speed * 0.00003f;
    float t13 = t * 1.3f;
    /* Global color rotation, reduced to its fractional part once per frame. */
    float trot = t * 0.05f;
    trot = trot - floor_f(trot);

    for (int x = 0; x < W; x++) {
        for (int y = 0; y < H; y++) {
            int p = y * W + x;
            int dist = HDIST[p];

            /* Wave value based on hex distance from center + time */
            float wave = fsin(t - (float)dist * 0.6f);
            /* Secondary wave for complexity */
            float wave2 = fsin(t13 + (float)dist * 0.4f + (float)HQMOD[p] * 0.5f);
            /* Combine waves: 0.0 to 1.0 */
            float combined = (wave + wave2) * 0.25f + 0.5f;
            if (combined < 0.0f) combined = 0.0f;
            if (combined > 1.0f) combined = 1.0f;

            /* Add hex cell identity for color variation, then global rotation */
            float cell_id = (float)CELLID[p] / 255.0f;
            float color_val = combined * 0.7f + cell_id * 0.3f + trot;
            if (color_val >= 1.0f) color_val -= 1.0f;

            int idx = (int)(color_val * 255.0f);
            if (idx < 0) idx = 0;
            if (idx > 255) idx = 255;

            /* Brightness: pulse based on wave, dim at cell edges for hex outline */
            float pulse = 0.6f + 0.4f * combined;
            float edge_factor = (float)EDGE[p] / 255.0f;

            int val = (int)((float)bright * pulse * edge_factor);
            if (val < 0) val = 0;
            if (val > 255) val = 255;

            int o = p * 3;
            FB[o]     = (uint8_t)(LR[idx] * val / 255);
            FB[o + 1] = (uint8_t)(LG[idx] * val / 255);
            FB[o + 2] = (uint8_t)(LB[idx] * val / 255);
        }
    }

    draw();
}
