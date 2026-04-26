#include "api.h"

/* ---- Metadata JSON ---- */
static const char META[] =
    "{\"name\":\"Waves\","
    "\"desc\":\"Ocean-like undulating waves of color\","
    "\"params\":["
        "{\"id\":0,\"name\":\"Speed\",\"type\":\"int\","
         "\"min\":1,\"max\":10,\"default\":4,"
         "\"desc\":\"Wave animation speed\"},"
        "{\"id\":1,\"name\":\"Hue\",\"type\":\"int\","
         "\"min\":0,\"max\":255,\"default\":160,"
         "\"desc\":\"Base color hue (160=blue)\"},"
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
    int hue    = get_param_i32(1);
    int bright = get_param_i32(2);

    int W = get_width();
    int H = get_height();
    if (W < 1) W = 1;
    if (H < 1) H = 1;

    time_acc += (uint32_t)(tick_ms * speed);

    /* Multiple time phases at different rates */
    int t1 = (int)(time_acc / 100) & 255;
    int t2 = (int)(time_acc / 137) & 255;
    int t3 = (int)(time_acc / 200) & 255;
    int t4 = (int)(time_acc / 170) & 255;

    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            /*
             * Layer 1: Primary wave - horizontal bands shifting vertically
             * y-based sine with time offset
             */
            int angle1 = (y * 32 + x * 4 + t1 * 3) & 255;
            int wave1 = isin(angle1);  /* -127..127 */

            /*
             * Layer 2: Secondary wave - slightly different frequency
             * Creates depth through interference
             */
            int angle2 = (y * 24 - x * 6 + t2 * 2) & 255;
            int wave2 = isin(angle2);

            /*
             * Layer 3: Slow undulating vertical displacement
             * Simulates wave crests rising and falling
             */
            int angle3 = (y * 16 + t3 * 4) & 255;
            int wave3 = isin(angle3);

            /*
             * Layer 4: Shimmer - fast small-scale variation
             * Adds sparkle like light on water surface
             */
            int angle4 = (x * 40 + y * 20 + t4 * 6) & 255;
            int shimmer = isin(angle4);

            /* Combine waves: primary + secondary create main pattern,
               wave3 modulates intensity, shimmer adds detail */
            int combined = wave1 + wave2 / 2;  /* -190..190 */

            /* Intensity based on wave height */
            int intensity = (combined + 190) * 255 / 380;  /* 0..255 */

            /* Add wave3 as vertical brightness modulation */
            int vert_mod = (wave3 + 127) * 3 / 4 + 64;  /* 64..254 */
            intensity = intensity * vert_mod / 255;

            /* Add shimmer for surface sparkle (small contribution) */
            int shimmer_boost = shimmer / 8;  /* -15..15 */
            intensity += shimmer_boost;
            if (intensity < 0) intensity = 0;
            if (intensity > 255) intensity = 255;

            /* Hue variation: slight shifts around base hue based on wave height */
            int hue_shift = combined / 12;  /* approx -15..15 */
            int pixel_hue = (hue + hue_shift) & 255;

            /* Saturation: deeper water = more saturated, crests = slightly whiter */
            int sat = 255 - (intensity / 8);  /* 223..255 */
            if (sat < 180) sat = 180;

            /* Value with brightness control */
            int val = intensity * bright / 255;
            if (val > 255) val = 255;

            int r, g, b;
            hsv_to_rgb(pixel_hue, sat, val, &r, &g, &b);

            /* Add foam/whitecap at wave peaks */
            if (intensity > 220) {
                int foam = (intensity - 220) * 7;  /* 0..245 */
                foam = foam * bright / 255;
                r = r + (255 - r) * foam / 512;
                g = g + (255 - g) * foam / 512;
                b = b + (255 - b) * foam / 512;
                if (r > 255) r = 255;
                if (g > 255) g = 255;
                if (b > 255) b = 255;
            }

            set_pixel(x, y, r, g, b);
        }
    }

    draw();
}
