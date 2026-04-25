#include "api.h"

/* ---- Metadata JSON ---- */
static const char META[] =
    "{\"name\":\"RGB Cycle\","
    "\"desc\":\"Smooth rainbow color cycle\","
    "\"params\":["
        "{\"id\":0,\"name\":\"Speed\",\"type\":\"int\","
         "\"min\":1,\"max\":100,\"default\":20,"
         "\"desc\":\"Cycle speed (1=slow, 100=fast)\"},"
        "{\"id\":1,\"name\":\"Brightness\",\"type\":\"int\","
         "\"min\":1,\"max\":255,\"default\":255,"
         "\"desc\":\"LED brightness\"}"
    "]}";

EXPORT(get_meta_ptr)
int get_meta_ptr(void) {
    return (int)META;
}

EXPORT(get_meta_len)
int get_meta_len(void) {
    return sizeof(META) - 1;
}

EXPORT(init)
void init(void) {
}

EXPORT(update)
void update(int tick_ms) {
    int speed      = get_param_i32(0);
    int brightness = get_param_i32(1);
    int w = get_width();
    int h = get_height();

    if (speed < 1) speed = 1;
    if (speed > 100) speed = 100;
    if (brightness < 1) brightness = 1;
    if (brightness > 255) brightness = 255;

    /* Period: speed=1 → 30s cycle, speed=100 → 1s cycle */
    /* period_ms = 30000 / speed */
    int period_ms = 30000 / speed;
    if (period_ms < 100) period_ms = 100;

    /* Position in color wheel: 0..767 (3 transitions of 256 steps) */
    /* Use modular arithmetic to avoid overflow on large tick_ms values */
    int pos = (int)(((uint32_t)tick_ms % (uint32_t)period_ms) * 768 / period_ms);

    int r, g, b;

    if (pos < 256) {
        /* Red → Green */
        r = 255 - pos;
        g = pos;
        b = 0;
    } else if (pos < 512) {
        /* Green → Blue */
        int p = pos - 256;
        r = 0;
        g = 255 - p;
        b = p;
    } else {
        /* Blue → Red */
        int p = pos - 512;
        r = p;
        g = 0;
        b = 255 - p;
    }

    /* Apply brightness */
    r = r * brightness / 255;
    g = g * brightness / 255;
    b = b * brightness / 255;

    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            set_pixel(x, y, r, g, b);

    draw();
}
