#include "api.h"

/* ---- Metadata JSON ---- */
static const char META[] =
    "{\"name\":\"Plasma\","
    "\"desc\":\"Classic plasma effect with overlapping sine waves\","
    "\"params\":["
        "{\"id\":0,\"name\":\"Speed\",\"type\":\"int\","
         "\"min\":1,\"max\":10,\"default\":5,"
         "\"desc\":\"Animation speed\"},"
        "{\"id\":1,\"name\":\"Scale\",\"type\":\"int\","
         "\"min\":1,\"max\":50,\"default\":30,"
         "\"desc\":\"Pattern scale\"},"
        "{\"id\":2,\"name\":\"Brightness\",\"type\":\"int\","
         "\"min\":1,\"max\":255,\"default\":200,"
         "\"desc\":\"Overall brightness\"}"
    "]}";

EXPORT(get_meta_ptr)
int get_meta_ptr(void) { return (int)META; }

EXPORT(get_meta_len)
int get_meta_len(void) { return sizeof(META) - 1; }

/* Integer sine: input 0-255 (full cycle), output 0-255 (centered at 128) */
static int isin8(int angle) {
    float s = m_sin((float)(angle & 0xFF) * (6.28318530f / 256.0f));
    int val = (int)(s * 255.0f);     /* -255..255 */
    /* Shift from -255..255 to 0..255 */
    return (val + 255) >> 1;
}

/* Integer cosine */
static int icos8(int angle) {
    return isin8(angle + 64);
}

/* ---- HSV to RGB conversion ---- */
/* h: 0-255, s: 0-255, v: 0-255 */
static void hsv_to_rgb(int h, int s, int v, int *r, int *g, int *b) {
    int c = m_hsv(h & 0xFF, s, v);
    *r = (c >> 16) & 255;
    *g = (c >> 8) & 255;
    *b = c & 255;
}

#define MAX_W 64
#define MAX_H 64

EXPORT(init)
void init(void) {
    /* Nothing to initialize */
}

EXPORT(update)
void update(int tick_ms) {
    int speed      = get_param_i32(0);
    int scale      = get_param_i32(1);
    int brightness = get_param_i32(2);
    int W = get_width();
    int H = get_height();

    if (W > MAX_W) W = MAX_W;
    if (H > MAX_H) H = MAX_H;
    if (W < 1) W = 1;
    if (H < 1) H = 1;

    /* Time value that drives animation */
    int t = (tick_ms * speed) / 4;

    /* Scale controls spatial frequency: higher scale = tighter patterns */
    /* Map scale 1-50 to a multiplier */
    int smul = scale + 5;

    /* Center of the matrix for radial component */
    int cx = W * 128;  /* in 8.8 fixed point of pixel coords, so W/2 * 256 */
    int cy = H * 128;

    for (int x = 0; x < W; x++) {
        for (int y = 0; y < H; y++) {
            /* Plasma is sum of several sine-based functions */

            /* Component 1: horizontal wave */
            int v1 = isin8((x * smul / 4 + (t >> 4)) & 0xFF);

            /* Component 2: vertical wave */
            int v2 = isin8((y * smul / 3 + (t >> 5)) & 0xFF);

            /* Component 3: diagonal wave */
            int v3 = isin8(((x + y) * smul / 5 + (t >> 3)) & 0xFF);

            /* Component 4: radial wave from center */
            int dx = x * 256 - cx;
            int dy = y * 256 - cy;
            /* Approximate distance (avoid full sqrt for speed) */
            int adx = dx < 0 ? -dx : dx;
            int ady = dy < 0 ? -dy : dy;
            /* Fast distance approximation: max(|dx|,|dy|) + 0.41*min(|dx|,|dy|) */
            int dmin = adx < ady ? adx : ady;
            int dmax = adx > ady ? adx : ady;
            int dist = dmax + (dmin * 105 >> 8);
            int v4 = isin8((dist * smul / 512 + (t >> 4)) & 0xFF);

            /* Combine all components */
            int combined = (v1 + v2 + v3 + v4) / 4;

            /* Map to hue for rainbow-like plasma colors */
            int hue = (combined + (t >> 6)) & 0xFF;

            int r, g, b;
            hsv_to_rgb(hue, 220, brightness, &r, &g, &b);
            set_pixel(x, y, r, g, b);
        }
    }

    draw();
}
