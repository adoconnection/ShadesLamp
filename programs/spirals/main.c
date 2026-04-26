#include "api.h"

/* ---- Metadata JSON ---- */
static const char META[] =
    "{\"name\":\"Spirals\","
    "\"desc\":\"Rotating spirograph pattern with multiple orbiting points\","
    "\"params\":["
        "{\"id\":0,\"name\":\"Count\",\"type\":\"int\","
         "\"min\":1,\"max\":6,\"default\":3,"
         "\"desc\":\"Number of spiral arms\"},"
        "{\"id\":1,\"name\":\"Speed\",\"type\":\"int\","
         "\"min\":1,\"max\":10,\"default\":5,"
         "\"desc\":\"Rotation speed\"},"
        "{\"id\":2,\"name\":\"Brightness\",\"type\":\"int\","
         "\"min\":1,\"max\":255,\"default\":200,"
         "\"desc\":\"Overall brightness\"}"
    "]}";

EXPORT(get_meta_ptr)
int get_meta_ptr(void) { return (int)META; }

EXPORT(get_meta_len)
int get_meta_len(void) { return sizeof(META) - 1; }

/* ---- Sin/Cos lookup table (256 entries, values -127..+127) ---- */
static const signed char sin_table[256] = {
      0,   3,   6,   9,  12,  16,  19,  22,  25,  28,  31,  34,  37,  40,  43,  46,
     49,  51,  54,  57,  60,  63,  65,  68,  71,  73,  76,  78,  81,  83,  85,  88,
     90,  92,  94,  96,  98, 100, 102, 104, 106, 107, 109, 111, 112, 113, 115, 116,
    117, 118, 120, 121, 122, 122, 123, 124, 125, 125, 126, 126, 126, 127, 127, 127,
    127, 127, 127, 127, 126, 126, 126, 125, 125, 124, 123, 122, 122, 121, 120, 118,
    117, 116, 115, 113, 112, 111, 109, 107, 106, 104, 102, 100,  98,  96,  94,  92,
     90,  88,  85,  83,  81,  78,  76,  73,  71,  68,  65,  63,  60,  57,  54,  51,
     49,  46,  43,  40,  37,  34,  31,  28,  25,  22,  19,  16,  12,   9,   6,   3,
      0,  -3,  -6,  -9, -12, -16, -19, -22, -25, -28, -31, -34, -37, -40, -43, -46,
    -49, -51, -54, -57, -60, -63, -65, -68, -71, -73, -76, -78, -81, -83, -85, -88,
    -90, -92, -94, -96, -98,-100,-102,-104,-106,-107,-109,-111,-112,-113,-115,-116,
   -117,-118,-120,-121,-122,-122,-123,-124,-125,-125,-126,-126,-126,-127,-127,-127,
   -127,-127,-127,-127,-126,-126,-126,-125,-125,-124,-123,-122,-122,-121,-120,-118,
   -117,-116,-115,-113,-112,-111,-109,-107,-106,-104,-102,-100, -98, -96, -94, -92,
    -90, -88, -85, -83, -81, -78, -76, -73, -71, -68, -65, -63, -60, -57, -54, -51,
    -49, -46, -43, -40, -37, -34, -31, -28, -25, -22, -19, -16, -12,  -9,  -6,  -3
};

static int isin(int angle) {
    return sin_table[angle & 255];
}

static int icos(int angle) {
    return sin_table[(angle + 64) & 255];
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

static uint8_t fb_r[MAX_W][MAX_H];
static uint8_t fb_g[MAX_W][MAX_H];
static uint8_t fb_b[MAX_W][MAX_H];

static uint32_t tick_acc;
static int theta1; /* primary orbit angle (accumulator) */
static int theta2; /* secondary orbit angle (accumulator) */
static int hue_offset;

EXPORT(init)
void init(void) {
    tick_acc = 0;
    theta1 = 0;
    theta2 = 0;
    hue_offset = 0;
    for (int x = 0; x < MAX_W; x++)
        for (int y = 0; y < MAX_H; y++) {
            fb_r[x][y] = 0;
            fb_g[x][y] = 0;
            fb_b[x][y] = 0;
        }
}

/* Add color to framebuffer pixel with saturation */
static void fb_add(int x, int y, int r, int g, int b, int W, int H) {
    /* Horizontal cylinder wrap */
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
    int count  = get_param_i32(0);
    int speed  = get_param_i32(1);
    int bright = get_param_i32(2);
    int W = get_width();
    int H = get_height();

    if (W > MAX_W) W = MAX_W;
    if (H > MAX_H) H = MAX_H;
    if (W < 1) W = 1;
    if (H < 1) H = 1;
    if (count < 1) count = 1;
    if (count > 6) count = 6;

    tick_acc += (uint32_t)tick_ms;

    /* Fade framebuffer — creates trailing effect */
    int fade = 8 + speed * 2;
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

    /* Apply light blur: average with direct neighbors (simple 3x3 box, low weight) */
    /* This is expensive on MAX_W*MAX_H, so only do it for the actual matrix size */
    /* Skip blur for performance — the trail fading is sufficient for smooth look */

    /* Update angles */
    int angle_step1 = speed + 1;     /* primary orbit: slower */
    int angle_step2 = speed * 2 + 3; /* secondary orbit: faster */
    theta1 += angle_step1;
    theta2 += angle_step2;
    hue_offset += 1;

    /* Spirograph parameters */
    int cx = W / 2;
    int cy = H / 2;
    int radius_x1 = W / 4;
    int radius_y1 = H / 4;
    int radius_x2 = W / 4 - 1;
    int radius_y2 = H / 4 - 1;
    if (radius_x1 < 1) radius_x1 = 1;
    if (radius_y1 < 1) radius_y1 = 1;
    if (radius_x2 < 1) radius_x2 = 1;
    if (radius_y2 < 1) radius_y2 = 1;

    int spiro_offset = 256 / count;

    for (int i = 0; i < count; i++) {
        int a1 = (theta1 + i * spiro_offset) & 255;
        int a2 = (theta2 + i * spiro_offset) & 255;

        /* Primary orbit position */
        int px = cx + (isin(a1) * radius_x1 / 127);
        int py = cy + (icos(a1) * radius_y1 / 127);

        /* Secondary orbit around the primary point */
        int x2 = px + (isin(a2) * radius_x2 / 127);
        int y2 = py + (icos(a2) * radius_y2 / 127);

        /* Color: each arm gets a different hue */
        int h = (hue_offset + i * spiro_offset) & 255;
        int r, g, b;
        hsv_to_rgb(h, 255, bright, &r, &g, &b);

        /* Draw the point with a small cross for visibility */
        fb_add(x2, y2, r, g, b, W, H);
        fb_add(x2 + 1, y2, r / 2, g / 2, b / 2, W, H);
        fb_add(x2 - 1, y2, r / 2, g / 2, b / 2, W, H);
        fb_add(x2, y2 + 1, r / 2, g / 2, b / 2, W, H);
        fb_add(x2, y2 - 1, r / 2, g / 2, b / 2, W, H);
    }

    /* Output framebuffer */
    for (int x = 0; x < W; x++)
        for (int y = 0; y < H; y++)
            set_pixel(x, y, fb_r[x][y], fb_g[x][y], fb_b[x][y]);

    draw();
}
