#include "api.h"

/* ---- Metadata JSON ---- */
static const char META[] =
    "{\"name\":\"Rainbow\","
    "\"desc\":\"Smooth HSV rainbow cycle\","
    "\"params\":["
        "{\"id\":0,\"name\":\"Speed\",\"type\":\"int\","
         "\"min\":1,\"max\":100,\"default\":30,"
         "\"desc\":\"Rotation speed\"},"
        "{\"id\":1,\"name\":\"Brightness\",\"type\":\"int\","
         "\"min\":1,\"max\":255,\"default\":128,"
         "\"desc\":\"Brightness\"},"
        "{\"id\":2,\"name\":\"Saturation\",\"type\":\"float\","
         "\"min\":0.0,\"max\":1.0,\"default\":1.0,"
         "\"desc\":\"Color saturation\"}"
    "]}";

EXPORT(get_meta_ptr)
int get_meta_ptr(void) {
    return (int)META;
}

EXPORT(get_meta_len)
int get_meta_len(void) {
    return sizeof(META) - 1;
}

/* ---- HSV to RGB (integer math, saturation as 0-255) ---- */
/*
 * hue: 0-359   (degrees)
 * sat: 0-255   (mapped from float 0.0-1.0)
 * val: 0-255   (brightness)
 *
 * Outputs r, g, b in 0-255 via pointers.
 */
static void hsv_to_rgb(int hue, int sat, int val, int *r, int *g, int *b) {
    if (sat == 0) {
        *r = val; *g = val; *b = val;
        return;
    }

    /* Ensure hue is in 0-359 */
    while (hue < 0)   hue += 360;
    while (hue >= 360) hue -= 360;

    int sector = hue / 60;          /* 0..5 */
    int frac   = hue - sector * 60; /* remainder 0..59 */

    /* Pre-compute common terms (all in 0-255 range) */
    int p = (val * (255 - sat)) / 255;
    int q = (val * (255 - (sat * frac) / 60)) / 255;
    int t = (val * (255 - (sat * (60 - frac)) / 60)) / 255;

    switch (sector) {
        case 0:  *r = val; *g = t;   *b = p;   break;
        case 1:  *r = q;   *g = val; *b = p;   break;
        case 2:  *r = p;   *g = val; *b = t;   break;
        case 3:  *r = p;   *g = q;   *b = val; break;
        case 4:  *r = t;   *g = p;   *b = val; break;
        default: *r = val; *g = p;   *b = q;   break; /* sector 5 */
    }
}

EXPORT(init)
void init(void) {
    /* nothing to initialize */
}

EXPORT(update)
void update(int tick_ms) {
    int   speed      = get_param_i32(0);
    int   brightness = get_param_i32(1);
    float sat_f      = get_param_f32(2);

    /* Convert saturation float (0.0-1.0) to integer (0-255) */
    int sat = (int)(sat_f * 255.0f);
    if (sat < 0)   sat = 0;
    if (sat > 255) sat = 255;

    int w = get_width();
    int h = get_height();
    int total = w * h;
    if (total < 1) total = 1;

    /* Base hue rotates over time */
    int base_hue = (tick_ms * speed / 100) % 360;

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            /* Offset hue by pixel position for rainbow spread */
            int pixel_index = x + y * w;
            int hue = (base_hue + pixel_index * 360 / total) % 360;

            int r, g, b;
            hsv_to_rgb(hue, sat, brightness, &r, &g, &b);
            set_pixel(x, y, r, g, b);
        }
    }

    draw();
}
