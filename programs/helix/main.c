#include "api.h"

/* ---- Metadata JSON ---- */
static const char META[] =
    "{\"name\":\"Helix\","
    "\"desc\":\"Double helix (DNA-style) rotating around the cylindrical display\","
    "\"params\":["
        "{\"id\":0,\"name\":\"Colours\",\"type\":\"select\","
         "\"options\":[\"Rainbow\",\"2 Colours\",\"3 Colours\"],\"default\":1,"
         "\"desc\":\"Colour scheme\"},"
        "{\"id\":1,\"name\":\"Harmony\",\"type\":\"select\","
         "\"options\":[\"Opposite\",\"Adjacent\"],\"default\":0,"
         "\"desc\":\"Opposite (complementary) or adjacent (analogous) hues\"},"
        "{\"id\":2,\"name\":\"Speed\",\"type\":\"int\","
         "\"min\":1,\"max\":100,\"default\":35,"
         "\"desc\":\"Vertical scroll speed\"},"
        "{\"id\":3,\"name\":\"Brightness\",\"type\":\"int\","
         "\"min\":1,\"max\":255,\"default\":220,"
         "\"desc\":\"Overall brightness\"},"
        "{\"id\":4,\"name\":\"Hue\",\"type\":\"int\","
         "\"min\":0,\"max\":255,\"default\":85,"
         "\"desc\":\"Base colour hue\"},"
        "{\"id\":5,\"name\":\"Twist\",\"type\":\"int\","
         "\"min\":1,\"max\":20,\"default\":1,"
         "\"desc\":\"How tightly wound the helix is\"}"
    "]}";

EXPORT(get_meta_ptr)
int get_meta_ptr(void) { return (int)META; }

EXPORT(get_meta_len)
int get_meta_len(void) { return sizeof(META) - 1; }

/* ---- Math helpers ---- */

#define PI 3.14159265f
#define TWO_PI 6.28318530f

/* Bhaskara I sine approximation */
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

static float fcos(float x) {
    return fsin(x + PI * 0.5f);
}

static float fabsf_(float x) {
    return x < 0.0f ? -x : x;
}

/* ---- HSV to RGB ---- */
static void hsv_to_rgb(int h, int s, int v, int *r, int *g, int *b) {
    h = h & 255;
    if (s == 0) { *r = v; *g = v; *b = v; return; }
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

/* ---- State ---- */
#define MAX_W 64
#define MAX_H 64

/* Framebuffer for glow/trail effect */
static uint8_t fb_r[MAX_W][MAX_H];
static uint8_t fb_g[MAX_W][MAX_H];
static uint8_t fb_b[MAX_W][MAX_H];

EXPORT(init)
void init(void) {
    for (int x = 0; x < MAX_W; x++)
        for (int y = 0; y < MAX_H; y++) {
            fb_r[x][y] = 0;
            fb_g[x][y] = 0;
            fb_b[x][y] = 0;
        }
}

/* Saturating add into framebuffer */
static void fb_add(int x, int y, int r, int g, int b, int W, int H) {
    /* Horizontal wrap for cylindrical display */
    x = ((x % W) + W) % W;
    if (y < 0 || y >= H) return;
    int nr = fb_r[x][y] + r; if (nr > 255) nr = 255;
    int ng = fb_g[x][y] + g; if (ng > 255) ng = 255;
    int nb = fb_b[x][y] + b; if (nb > 255) nb = 255;
    fb_r[x][y] = (uint8_t)nr;
    fb_g[x][y] = (uint8_t)ng;
    fb_b[x][y] = (uint8_t)nb;
}

EXPORT(update)
void update(int tick_ms) {
    int colors   = get_param_i32(0);   /* 0 rainbow, 1 two, 2 three */
    int harmony  = get_param_i32(1);   /* 0 opposite, 1 adjacent */
    int speed    = get_param_i32(2);
    int bright   = get_param_i32(3);
    int hue      = get_param_i32(4);
    int twist    = get_param_i32(5);
    int W = get_width();
    int H = get_height();

    if (W > MAX_W) W = MAX_W;
    if (H > MAX_H) H = MAX_H;
    if (W < 1) W = 1;
    if (H < 1) H = 1;
    if (twist < 1) twist = 1;

    /* hue spacing between strands/colours: opposite = far on the wheel,
     * adjacent = close. Two colours can spread to full complementary (128);
     * three colours sit closer so all three fit. */
    int spacing;
    if (colors == 1) spacing = (harmony == 0) ? 128 : 30;   /* 2 colours */
    else             spacing = (harmony == 0) ? 85  : 24;   /* 3 colours / rainbow */
    int rainbow = (colors == 0);

    /* Fade framebuffer for glow trail */
    int fade = 40 + speed / 3;
    for (int x = 0; x < W; x++) {
        for (int y = 0; y < H; y++) {
            int r = fb_r[x][y] - fade;
            int g = fb_g[x][y] - fade;
            int b = fb_b[x][y] - fade;
            fb_r[x][y] = (uint8_t)(r < 0 ? 0 : r);
            fb_g[x][y] = (uint8_t)(g < 0 ? 0 : g);
            fb_b[x][y] = (uint8_t)(b < 0 ? 0 : b);
        }
    }

    /* Time phase: tick_ms is total elapsed time, use directly */
    float t = (float)tick_ms * (float)speed * 0.00003f;

    /* Twist frequency: how many full turns of the helix across the height.
       twist param (1-20) maps to spatial frequency. */
    float twist_freq = (float)twist * TWO_PI / (float)H;

    /* rainbow hue drift over time (only used in Rainbow mode) */
    int htime = (int)(t * 30.0f);

    /* Rung spacing: one rung every N rows.
       More twist = can afford more rungs. Roughly every 3-5 rows. */
    int rung_spacing = 4;
    if (H > 16) rung_spacing = 3;

    /* Half-width for sine amplitude */
    float half_w = (float)W * 0.5f;
    /* Slight amplitude reduction so strands don't overlap edges of glow */
    float amplitude = half_w * 0.85f;

    for (int y = 0; y < H; y++) {
        float fy = (float)y;

        /* per-row hues: in Rainbow mode the colour cycles up the helix and
         * drifts over time; otherwise the strands are fixed hues */
        int rshift = rainbow ? ((y * 256) / H + htime) : 0;
        int hue1 = (hue + rshift) & 255;
        int hue2 = (hue + spacing + rshift) & 255;

        /* Strand 1: phase = 0, Strand 2: phase = PI (180 degrees apart) */
        float angle1 = fy * twist_freq + t;
        float angle2 = angle1 + PI;

        float sin1 = fsin(angle1);
        float sin2 = fsin(angle2);

        /* X positions of each strand (float, then round to int) */
        float fx1 = half_w + amplitude * sin1;
        float fx2 = half_w + amplitude * sin2;
        int x1 = (int)(fx1 + 0.5f);
        int x2 = (int)(fx2 + 0.5f);

        /* Depth effect: cosine gives z-depth (front vs back of cylinder).
           cos > 0 means strand is in front, cos < 0 means behind. */
        float cos1 = fcos(angle1);
        float cos2 = fcos(angle2);

        /* Brightness modulated by depth: front = full bright, back = dim */
        float depth_scale1 = 0.35f + 0.65f * (cos1 * 0.5f + 0.5f);
        float depth_scale2 = 0.35f + 0.65f * (cos2 * 0.5f + 0.5f);

        int v1 = (int)((float)bright * depth_scale1);
        int v2 = (int)((float)bright * depth_scale2);
        if (v1 < 15) v1 = 15;
        if (v2 < 15) v2 = 15;
        if (v1 > 255) v1 = 255;
        if (v2 > 255) v2 = 255;

        /* Draw strand 1 (core pixel + 1-pixel glow on each side) */
        int r, g, b;
        hsv_to_rgb(hue1, 255, v1, &r, &g, &b);
        fb_add(x1, y, r, g, b, W, H);
        /* Glow pixels at reduced brightness */
        int gr1 = r / 3, gg1 = g / 3, gb1 = b / 3;
        fb_add(x1 - 1, y, gr1, gg1, gb1, W, H);
        fb_add(x1 + 1, y, gr1, gg1, gb1, W, H);

        /* Draw strand 2 */
        hsv_to_rgb(hue2, 255, v2, &r, &g, &b);
        fb_add(x2, y, r, g, b, W, H);
        int gr2 = r / 3, gg2 = g / 3, gb2 = b / 3;
        fb_add(x2 - 1, y, gr2, gg2, gb2, W, H);
        fb_add(x2 + 1, y, gr2, gg2, gb2, W, H);

        /* Draw connecting rungs between strands at regular intervals */
        if ((y % rung_spacing) == 0) {
            /* Determine rung endpoints (handle wrapping) */
            int rx_start, rx_end;

            /* Find the shorter path around the cylinder between x1 and x2 */
            int dx_direct = x2 - x1;
            int dx_wrap;
            if (dx_direct > 0)
                dx_wrap = dx_direct - W;
            else
                dx_wrap = dx_direct + W;

            int use_direct;
            int abs_direct = dx_direct < 0 ? -dx_direct : dx_direct;
            int abs_wrap = dx_wrap < 0 ? -dx_wrap : dx_wrap;

            if (abs_direct <= abs_wrap) {
                use_direct = 1;
                rx_start = x1;
                rx_end = x2;
            } else {
                use_direct = 0;
                /* For the wrapping case, swap so we go the short way */
                if (dx_wrap > 0) {
                    rx_start = x1;
                    rx_end = x1 + dx_wrap;
                } else {
                    rx_start = x2;
                    rx_end = x2 - dx_wrap;
                }
            }

            /* Ensure rx_start <= rx_end */
            if (rx_start > rx_end) {
                int tmp = rx_start;
                rx_start = rx_end;
                rx_end = tmp;
            }

            /* Rung brightness: average the depth of both strands, desaturated */
            float rung_depth = (depth_scale1 + depth_scale2) * 0.5f;
            float rung_mul = (colors == 2) ? 0.8f : 0.4f;   /* 3rd colour brighter */
            int rung_v = (int)((float)bright * rung_depth * rung_mul);
            if (rung_v < 10) rung_v = 10;
            if (rung_v > 255) rung_v = 255;

            /* Rung colour: in 3-colour mode it's a distinct third hue;
             * otherwise a desaturated white-ish tint */
            int rung_r, rung_g, rung_b;
            if (colors == 2) {
                int hue3 = (hue + 2 * spacing + rshift) & 255;
                hsv_to_rgb(hue3, 230, rung_v, &rung_r, &rung_g, &rung_b);
            } else {
                hsv_to_rgb((hue + rshift) & 255, 60, rung_v, &rung_r, &rung_g, &rung_b);
            }

            /* Draw rung pixels (skip the strand pixels themselves) */
            for (int rx = rx_start + 1; rx < rx_end; rx++) {
                fb_add(rx, y, rung_r, rung_g, rung_b, W, H);
            }
        }
    }

    /* Output framebuffer to display */
    for (int x = 0; x < W; x++)
        for (int y = 0; y < H; y++)
            set_pixel(x, y, fb_r[x][y], fb_g[x][y], fb_b[x][y]);

    draw();
}
