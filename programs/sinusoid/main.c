#include "api.h"

/* ---- Metadata JSON ---- */
static const char META[] =
    "{\"name\":\"Sinusoid\","
    "\"desc\":\"Three overlapping sine waves creating colorful interference\","
    "\"params\":["
        "{\"id\":0,\"name\":\"Speed\",\"type\":\"int\","
         "\"min\":1,\"max\":10,\"default\":5,"
         "\"desc\":\"Animation speed\"},"
        "{\"id\":1,\"name\":\"Scale\",\"type\":\"int\","
         "\"min\":1,\"max\":50,\"default\":20,"
         "\"desc\":\"Pattern scale/frequency\"},"
        "{\"id\":2,\"name\":\"Brightness\",\"type\":\"int\","
         "\"min\":1,\"max\":255,\"default\":200,"
         "\"desc\":\"Overall brightness\"}"
    "]}";

EXPORT(get_meta_ptr)
int get_meta_ptr(void) { return (int)META; }

EXPORT(get_meta_len)
int get_meta_len(void) { return sizeof(META) - 1; }

/* ---- Sine lookup table (256 entries, values -127..127) ---- */
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

/* Returns -127..127 */
static int isin(int angle) {
    return (int)sin_table[angle & 255];
}

/* ---- HSV to RGB ---- */
static void hsv_to_rgb(int h, int s, int v, int *r, int *g, int *b) {
    h = h & 255;
    if (s == 0) { *r = *g = *b = v; return; }
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
static uint32_t time_acc;

EXPORT(init)
void init(void) {
    time_acc = 0;
}

EXPORT(update)
void update(int tick_ms) {
    int speed  = get_param_i32(0);
    int scale  = get_param_i32(1);
    int bright = get_param_i32(2);

    int W = get_width();
    int H = get_height();
    if (W < 1) W = 1;
    if (H < 1) H = 1;

    time_acc += (uint32_t)(tick_ms * speed);

    /* Time phase (8-bit wrapping counters at different speeds) */
    int t1 = (int)(time_acc / 80) & 255;
    int t2 = (int)(time_acc / 110) & 255;
    int t3 = (int)(time_acc / 150) & 255;

    /* Three sine waves with different frequencies and phases */
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            /*
             * Wave 1: radial rings from center, moves outward
             * Wave 2: diagonal stripes
             * Wave 3: horizontal bands that shift
             */

            /* Distance from center (approximate, no sqrt needed) */
            int cx = x * 256 / W - 128;  /* -128..127 */
            int cy = y * 256 / H - 128;

            /* Wave 1: concentric rings from center */
            int dist = cx * cx + cy * cy;
            /* Scale distance into angle range */
            int angle1 = (dist * scale / 64 + t1 * 4) & 255;
            int v1 = isin(angle1);  /* -127..127 */

            /* Wave 2: diagonal (x+y) with different frequency */
            int angle2 = ((x + y) * scale / 4 + t2 * 3) & 255;
            int v2 = isin(angle2);

            /* Wave 3: horizontal with vertical modulation */
            int angle3 = (y * scale / 3 - x * scale / 8 + t3 * 5) & 255;
            int v3 = isin(angle3);

            /* Map each wave to a color channel (shifted to 0..255) */
            int r_val = (v1 + 127) * bright / 255;
            int g_val = (v2 + 127) * bright / 255;
            int b_val = (v3 + 127) * bright / 255;

            /* Add interference: boost where waves overlap */
            int sum = v1 + v2 + v3;  /* -381..381 */
            /* Use sum to create a hue shift for more colorful result */
            int hue = ((sum + 381) * 255 / 762);  /* 0..255 */
            int intensity = (r_val + g_val + b_val) / 3;

            /* Blend between raw RGB and HSV-mapped color */
            int r, g, b;
            int rh, gh, bh;
            hsv_to_rgb(hue, 220, intensity, &rh, &gh, &bh);

            /* 50/50 blend of direct waves and hue-mapped */
            r = (r_val + rh) / 2;
            g = (g_val + gh) / 2;
            b = (b_val + bh) / 2;

            if (r > 255) r = 255;
            if (g > 255) g = 255;
            if (b > 255) b = 255;

            set_pixel(x, y, r, g, b);
        }
    }

    draw();
}
