#include "api.h"

/* ---- Metadata JSON ---- */
static const char META[] =
    "{\"name\":\"RGB Cycle\","
    "\"desc\":\"Cycles through pure R, G, B colors\","
    "\"params\":["
        "{\"id\":0,\"name\":\"Speed\",\"type\":\"int\","
         "\"min\":1,\"max\":200,\"default\":50,"
         "\"desc\":\"Cycle speed\"},"
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
    /* nothing to initialize */
}

EXPORT(update)
void update(int tick_ms) {
    int speed      = get_param_i32(0);
    int brightness = get_param_i32(1);
    int w = get_width();
    int h = get_height();

    /* phase 0=Red, 1=Green, 2=Blue */
    int phase = (tick_ms * speed / 1000) % 3;

    int r = (phase == 0) ? brightness : 0;
    int g = (phase == 1) ? brightness : 0;
    int b = (phase == 2) ? brightness : 0;

    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            set_pixel(x, y, r, g, b);

    draw();
}
