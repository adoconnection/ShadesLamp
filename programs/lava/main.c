#include "api.h"

/* ---- Metadata JSON ---- */
static const char META[] =
    "{\"name\":\"Lava\","
    "\"desc\":\"Hot lava flowing patterns with warm colors\","
    "\"params\":["
        "{\"id\":0,\"name\":\"Speed\",\"type\":\"int\","
         "\"min\":1,\"max\":10,\"default\":4,"
         "\"desc\":\"Flow speed\"},"
        "{\"id\":1,\"name\":\"Scale\",\"type\":\"int\","
         "\"min\":1,\"max\":50,\"default\":25,"
         "\"desc\":\"Noise zoom level\"},"
        "{\"id\":2,\"name\":\"Brightness\",\"type\":\"int\","
         "\"min\":1,\"max\":255,\"default\":220,"
         "\"desc\":\"Overall brightness\"}"
    "]}";

EXPORT(get_meta_ptr)
int get_meta_ptr(void) { return (int)META; }

EXPORT(get_meta_len)
int get_meta_len(void) { return sizeof(META) - 1; }

/* ---- Lava color palette ---- */
/* Maps noise value (0-255) to warm lava colors: black -> deep red -> red -> orange -> yellow -> white */
static void lava_color(int val, int brightness, int *r, int *g, int *b) {
    int r0, g0, b0;

    if (val < 64) {
        /* Black to deep red */
        r0 = val * 3;
        g0 = 0;
        b0 = 0;
    } else if (val < 128) {
        /* Deep red to bright red-orange */
        int s = val - 64;
        r0 = 192 + s;
        g0 = s * 50 / 64;
        b0 = 0;
    } else if (val < 192) {
        /* Red-orange to orange-yellow */
        int s = val - 128;
        r0 = 255;
        g0 = 50 + s * 3;
        b0 = s / 4;
    } else {
        /* Orange-yellow to yellow-white (hot spots) */
        int s = val - 192;
        r0 = 255;
        g0 = 242 + s / 5;
        b0 = 16 + s * 2;
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

    /* Scale factor (8.8 fixed, 256 = one noise cell). */
    int noise_scale = 512 / (scale + 1) + 3;

    /* Lava flows upward: vertical offset advances (negative) over time,
       with slow horizontal drift. Offsets are 8.8 pixel-space. */
    int time_offset = (tick_ms * speed) / 10;
    int ox = time_offset >> 3;
    int oy = -time_offset;

    /* Three octaves for a richer, more organic molten look. */
    m_noise_fill(NOISE, W, H, noise_scale, ox, oy, 3);

    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            int val = NOISE[y * W + x];

            /* Upward heat bias: bottom of the display is hotter. */
            int heat_bias = (H - 1 - y) * 40 / H;
            val += heat_bias;
            if (val > 255) val = 255;

            int r, g, b;
            lava_color(val, brightness, &r, &g, &b);
            set_pixel(x, y, r, g, b);
        }
    }

    draw();
}
