#include "api.h"

/* ---- Metadata JSON ---- */
static const char META[] =
    "{\"name\":\"Clouds\","
    "\"desc\":\"Smooth cloud-like noise patterns\","
    "\"params\":["
        "{\"id\":0,\"name\":\"Speed\",\"type\":\"int\","
         "\"min\":1,\"max\":10,\"default\":5,"
         "\"desc\":\"Animation speed\"},"
        "{\"id\":1,\"name\":\"Scale\",\"type\":\"int\","
         "\"min\":1,\"max\":50,\"default\":20,"
         "\"desc\":\"Noise zoom level\"},"
        "{\"id\":2,\"name\":\"Brightness\",\"type\":\"int\","
         "\"min\":1,\"max\":255,\"default\":200,"
         "\"desc\":\"Overall brightness\"}"
    "]}";

EXPORT(get_meta_ptr)
int get_meta_ptr(void) { return (int)META; }

EXPORT(get_meta_len)
int get_meta_len(void) { return sizeof(META) - 1; }

/* ---- Cloud color palette ---- */
/* Maps a noise value (0-255) to soft blue-white cloud colors */
static void cloud_color(int val, int brightness, int *r, int *g, int *b) {
    int r0, g0, b0;

    if (val < 85) {
        /* Deep sky blue to lighter blue */
        r0 = 30 + val;
        g0 = 50 + val;
        b0 = 120 + val;
    } else if (val < 170) {
        /* Lighter blue to near-white */
        int s = val - 85;
        r0 = 115 + s * 140 / 85;
        g0 = 135 + s * 120 / 85;
        b0 = 205 + s * 50 / 85;
    } else {
        /* Near-white to bright white */
        int s = val - 170;
        r0 = 255;
        g0 = 255;
        b0 = 255 - s / 4;
    }

    if (r0 > 255) r0 = 255;
    if (g0 > 255) g0 = 255;
    if (b0 > 255) b0 = 255;

    *r = r0 * brightness / 255;
    *g = g0 * brightness / 255;
    *b = b0 * brightness / 255;
}

/* ---- Native noise field ---- */
#define MAX_W 64
#define MAX_H 64
static uint8_t NOISE[MAX_W * MAX_H];

EXPORT(init)
void init(void) {
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

    /* Scale factor (8.8 fixed, 256 = one noise cell): higher Scale param =
       smaller noise_scale = more zoomed in (larger features). */
    int noise_scale = 512 / (scale + 1) + 3;

    /* Animate: offsets are 8.8 pixel-space (pixel shift = offset/256). */
    int time_offset = (tick_ms * speed) / 8;
    int ox = time_offset >> 1;   /* drift sideways */
    int oy = time_offset >> 2;   /* slower vertical drift */

    m_noise_fill(NOISE, W, H, noise_scale, ox, oy, 2);

    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            int val = NOISE[y * W + x];
            int r, g, b;
            cloud_color(val, brightness, &r, &g, &b);
            set_pixel(x, y, r, g, b);
        }
    }

    draw();
}
