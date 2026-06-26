#include "api.h"

/* ---- Metadata JSON ---- */
static const char META[] =
    "{\"name\":\"Forest\","
    "\"desc\":\"Deep forest noise with green palette\","
    "\"params\":["
        "{\"id\":0,\"name\":\"Speed\",\"type\":\"int\","
         "\"min\":1,\"max\":10,\"default\":4,"
         "\"desc\":\"Animation speed\"},"
        "{\"id\":1,\"name\":\"Scale\",\"type\":\"int\","
         "\"min\":1,\"max\":50,\"default\":25,"
         "\"desc\":\"Noise zoom level\"},"
        "{\"id\":2,\"name\":\"Brightness\",\"type\":\"int\","
         "\"min\":1,\"max\":255,\"default\":200,"
         "\"desc\":\"Overall brightness\"}"
    "]}";

EXPORT(get_meta_ptr)
int get_meta_ptr(void) { return (int)META; }

EXPORT(get_meta_len)
int get_meta_len(void) { return sizeof(META) - 1; }

/* ---- Forest color palette ---- */
/* Mimics FastLED ForestColors_p: dark greens, olive, sea green, lime */
static void forest_color(int val, int brightness, int *r, int *g, int *b) {
    int r0, g0, b0;

    if (val < 51) {
        /* Dark green (0,100,0) -> Dark olive green (85,107,47) */
        int t = val * 5;  /* 0-255 */
        r0 = (0 * (255 - t) + 85 * t) >> 8;
        g0 = (100 * (255 - t) + 107 * t) >> 8;
        b0 = (0 * (255 - t) + 47 * t) >> 8;
    } else if (val < 102) {
        /* Dark olive green (85,107,47) -> Forest green (34,139,34) */
        int t = (val - 51) * 5;
        r0 = (85 * (255 - t) + 34 * t) >> 8;
        g0 = (107 * (255 - t) + 139 * t) >> 8;
        b0 = (47 * (255 - t) + 34 * t) >> 8;
    } else if (val < 153) {
        /* Forest green (34,139,34) -> Sea green (46,139,87) */
        int t = (val - 102) * 5;
        r0 = (34 * (255 - t) + 46 * t) >> 8;
        g0 = 139;
        b0 = (34 * (255 - t) + 87 * t) >> 8;
    } else if (val < 204) {
        /* Sea green (46,139,87) -> Lime green (50,205,50) */
        int t = (val - 153) * 5;
        r0 = (46 * (255 - t) + 50 * t) >> 8;
        g0 = (139 * (255 - t) + 205 * t) >> 8;
        b0 = (87 * (255 - t) + 50 * t) >> 8;
    } else {
        /* Lime green (50,205,50) -> Yellow green (154,205,50) */
        int t = (val - 204) * 5;
        r0 = (50 * (255 - t) + 154 * t) >> 8;
        g0 = 205;
        b0 = 50;
    }

    if (r0 > 255) r0 = 255;
    if (g0 > 255) g0 = 255;
    if (b0 > 255) b0 = 255;
    if (r0 < 0) r0 = 0;
    if (g0 < 0) g0 = 0;
    if (b0 < 0) b0 = 0;

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

    /* Animate: offsets are 8.8 pixel-space (pixel shift = offset/256). */
    int time_offset = (tick_ms * speed) / 8;
    int ox = time_offset >> 1;
    int oy = time_offset >> 2;

    m_noise_fill(NOISE, W, H, noise_scale, ox, oy, 2);

    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            int val = NOISE[y * W + x];
            /* fbm noise clusters near mid-grey; stretch contrast so the
               value spreads across the dark-green -> lime palette. */
            val = 128 + (val - 128) * 9 / 4;
            if (val < 0) val = 0;
            if (val > 255) val = 255;
            int r, g, b;
            forest_color(val, brightness, &r, &g, &b);
            set_pixel(x, y, r, g, b);
        }
    }

    draw();
}
