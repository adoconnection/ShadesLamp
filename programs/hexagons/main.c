#include "api.h"

/*
 * Hexagons - Hexagonal grid pattern with color waves pulsing outward
 * from the center. Uses axial hex coordinates to map each pixel to
 * a hex cell, then colors based on cell distance from center + time.
 * Designed for a cylindrical LED matrix (Y=0 is bottom).
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

static float fabs_f(float x) { return x < 0.0f ? -x : x; }

static float fsqrt(float x) {
    if (x <= 0.0f) return 0.0f;
    float guess = x * 0.5f;
    guess = 0.5f * (guess + x / guess);
    guess = 0.5f * (guess + x / guess);
    guess = 0.5f * (guess + x / guess);
    return guess;
}

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

EXPORT(init)
void init(void) {
    /* Nothing to initialize - purely per-frame computation */
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

    /* Hex cell size in pixels */
    float hex_size = (float)scale * 0.5f + 1.0f;

    /* Time phase for wave animation */
    float t = (float)tick_ms * (float)speed * 0.00003f;

    /* Center of the display in pixel coordinates */
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

            /* Convert pixel to fractional axial hex coordinates */
            float fq = (2.0f / 3.0f * px) / hex_size;
            float fr = (-1.0f / 3.0f * px + SQRT3 / 3.0f * py) / hex_size;

            /* Round to nearest hex cell */
            int hq, hr;
            hex_round(fq, fr, &hq, &hr);

            /* Distance of this hex cell from the center hex cell */
            int dq = hq - center_q;
            int dr = hr - center_r;
            int dist = hex_distance(dq, dr);

            /* Compute distance from pixel to hex cell center for edge detection */
            /* Convert hex cell center back to pixel coordinates */
            float cell_px = hex_size * (3.0f / 2.0f * (float)hq);
            float cell_py = hex_size * (SQRT3_2 * (float)hq + SQRT3 * (float)hr);
            float dx = px - cell_px;
            float dy = py - cell_py;
            float pixel_dist = fsqrt(dx * dx + dy * dy);

            /* Normalized distance within cell (0 = center, 1 = edge) */
            float inner_dist = pixel_dist / (hex_size * 0.9f);
            if (inner_dist > 1.0f) inner_dist = 1.0f;

            /* Wave value based on hex distance from center + time */
            float wave = fsin(t - (float)dist * 0.6f);
            /* Secondary wave for complexity */
            float wave2 = fsin(t * 1.3f + (float)dist * 0.4f + (float)(hq & 3) * 0.5f);
            /* Combine waves: 0.0 to 1.0 */
            float combined = (wave + wave2) * 0.25f + 0.5f;
            if (combined < 0.0f) combined = 0.0f;
            if (combined > 1.0f) combined = 1.0f;

            /* Add hex cell identity for color variation */
            float cell_id = (float)((hq * 7 + hr * 13) & 0xFF) / 255.0f;
            float color_val = combined * 0.7f + cell_id * 0.3f;
            /* Shift by time for global color rotation */
            color_val = color_val + t * 0.05f;
            /* Wrap to 0-1 */
            color_val = color_val - floor_f(color_val);

            /* Get palette color */
            int hue, sat;
            palette_color(palette, color_val, &hue, &sat);

            /* Brightness: pulse based on wave, dim at cell edges for hex outline */
            float pulse = 0.6f + 0.4f * combined;
            /* Edge darkening: dim pixels near hexagon boundaries */
            float edge_factor;
            if (inner_dist > 0.55f) {
                /* Strong darkening near edges for visible hex grid */
                edge_factor = 1.0f - (inner_dist - 0.55f) * 2.2f;
                if (edge_factor < 0.0f) edge_factor = 0.0f;
            } else {
                edge_factor = 1.0f;
            }

            int val = (int)((float)bright * pulse * edge_factor);
            if (val < 0) val = 0;
            if (val > 255) val = 255;

            int r, g, b;
            hsv2rgb(hue, sat, val, &r, &g, &b);
            set_pixel(x, y, r, g, b);
        }
    }

    draw();
}
