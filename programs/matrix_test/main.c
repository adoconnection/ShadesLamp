#include "api.h"

/* ---- Metadata JSON ---- */
static const char META[] =
    "{\"name\":\"Matrix Test\","
    "\"desc\":\"Sweeps G along X, R along Y, B diagonally to verify matrix mapping\","
    "\"params\":["
        "{\"id\":0,\"name\":\"Speed\",\"type\":\"int\","
         "\"min\":1,\"max\":100,\"default\":20,"
         "\"desc\":\"Animation speed\"},"
        "{\"id\":1,\"name\":\"Brightness\",\"type\":\"int\","
         "\"min\":1,\"max\":255,\"default\":64,"
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
    int speed = get_param_i32(0);
    int bright = get_param_i32(1);
    int w = get_width();
    int h = get_height();

    if (w < 1) w = 1;
    if (h < 1) h = 1;

    /* Time-based position: pixels per second = speed */
    int t = (tick_ms * speed) / 1000;

    /* Green dot sweeps along X axis (row 0) */
    int gx = t % w;

    /* Red dot sweeps along Y axis (column 0) */
    int ry = t % h;

    /* Blue dot sweeps diagonally from (0,0) to (w-1, h-1) */
    /* Use total diagonal steps = w + h - 1, ping-pong */
    int diag_len = w + h;
    int diag_pos = t % diag_len;
    int bx, by;
    if (w >= h) {
        bx = diag_pos * (w - 1) / (diag_len - 1);
        by = diag_pos * (h - 1) / (diag_len - 1);
    } else {
        bx = diag_pos * (w - 1) / (diag_len - 1);
        by = diag_pos * (h - 1) / (diag_len - 1);
    }

    /* Clear all pixels */
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            set_pixel(x, y, 0, 0, 0);

    /* Draw green horizontal line at row 0, highlight current pos */
    for (int x = 0; x < w; x++) {
        int g = (x == gx) ? bright : bright / 10;
        set_pixel(x, 0, 0, g, 0);
    }

    /* Draw red vertical line at column 0, highlight current pos */
    for (int y = 0; y < h; y++) {
        int r = (y == ry) ? bright : bright / 10;
        set_pixel(0, y, r, 0, 0);
    }

    /* Draw blue dot at diagonal position */
    set_pixel(bx, by, 0, 0, bright);

    /* Corner markers (dim white) to show matrix boundaries */
    int dim = bright / 8;
    if (dim < 1) dim = 1;
    set_pixel(0,     0,     dim, dim, dim);  /* top-left */
    set_pixel(w - 1, 0,     dim, dim, dim);  /* top-right */
    set_pixel(0,     h - 1, dim, dim, dim);  /* bottom-left */
    set_pixel(w - 1, h - 1, dim, dim, dim);  /* bottom-right */

    draw();
}
