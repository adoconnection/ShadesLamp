#include "api.h"

/* ---- Metadata JSON ---- */
static const char META[] =
    "{\"name\":\"Earth\","
    "\"desc\":\"Stylized world map slowly rotating across the display\","
    "\"params\":["
        "{\"id\":0,\"name\":\"Speed\",\"type\":\"int\","
         "\"min\":0,\"max\":100,\"default\":30,"
         "\"desc\":\"Rotation speed (0 stops the globe)\"},"
        "{\"id\":1,\"name\":\"LandColor\",\"type\":\"select\","
         "\"default\":0,"
         "\"options\":[\"Forest\",\"Desert\",\"Sand\",\"Snow\",\"Neon\"],"
         "\"desc\":\"Continents color palette\"},"
        "{\"id\":2,\"name\":\"SeaColor\",\"type\":\"select\","
         "\"default\":0,"
         "\"options\":[\"Ocean\",\"Deep\",\"Teal\",\"Cyan\",\"Dark\"],"
         "\"desc\":\"Oceans color palette\"},"
        "{\"id\":3,\"name\":\"LandBrightness\",\"type\":\"int\","
         "\"min\":0,\"max\":255,\"default\":200,"
         "\"desc\":\"Brightness of land pixels\"},"
        "{\"id\":4,\"name\":\"SeaBrightness\",\"type\":\"int\","
         "\"min\":0,\"max\":255,\"default\":80,"
         "\"desc\":\"Brightness of sea pixels\"},"
        "{\"id\":5,\"name\":\"Direction\",\"type\":\"select\","
         "\"default\":0,"
         "\"options\":[\"West to East\",\"East to West\"],"
         "\"desc\":\"Rotation direction\"},"
        "{\"id\":6,\"name\":\"Fit\",\"type\":\"select\","
         "\"default\":0,"
         "\"options\":[\"Stretch\",\"Native\"],"
         "\"desc\":\"Stretch map to display, or 1:1 native pixels (window over the map)\"}"
    "]}";

EXPORT(get_meta_ptr)
int get_meta_ptr(void) { return (int)META; }

EXPORT(get_meta_len)
int get_meta_len(void) { return sizeof(META) - 1; }

/* ---- Earth bitmap (64 cols × 16 rows). Y=15 = Arctic, Y=0 = Antarctic. ----
 * Coordinates: col 0 ≈ 180° (date line), col 16 ≈ 90°W, col 32 ≈ 0° (Greenwich),
 * col 48 ≈ 90°E, col 63 ≈ 174°E. Scrolling wraps the map cylindrically.
 *
 * The map is built procedurally from a list of land rectangles in init() so
 * shapes are easy to tweak without redoing ASCII art. Each rect contributes
 * a filled block; overlapping rects merge naturally. */

#define MAP_W 64
#define MAP_H 16
static uint8_t earth_map[MAP_W][MAP_H];

typedef struct { int x0, y0, x1, y1; } Rect;

static const Rect lands[] = {
    /* Antarctic ice cap (full strip + slight bulge) */
    {0,  0, 63, 0},
    {0,  1, 63, 1},
    {6,  2, 50, 2},
    /* Eastern tip of Russia / Kamchatka (wraps to left edge) */
    {0, 13,  3, 14},
    /* Alaska */
    {3, 12,  6, 14},
    /* Canada (broad northern strip) */
    {6, 12, 22, 14},
    /* Hudson Bay notch */
    {12, 13, 14, 14},  /* placeholder; we'll overwrite with sea below */
    /* Greenland */
    {23, 14, 26, 15},
    /* Arctic islands */
    {18, 15, 21, 15},
    {30, 15, 31, 15},
    {52, 15, 54, 15},
    /* USA */
    {9, 11, 22, 12},
    /* Mexico / Central America */
    {13,  9, 18, 11},
    {17,  8, 20,  9},
    /* South America - Northern (Venezuela, Colombia) */
    {19,  8, 25,  9},
    /* South America - Brazil / Amazon */
    {21,  5, 27,  8},
    /* South America - Argentina, Chile */
    {21,  3, 24,  5},
    {22,  2, 23,  3},
    /* Western Europe */
    {29, 12, 33, 14},
    /* Iberia */
    {30, 11, 32, 12},
    /* Scandinavia */
    {32, 14, 34, 15},
    {31, 14, 32, 15},
    /* British Isles */
    {28, 13, 29, 14},
    /* North Africa / Sahara */
    {31,  9, 39, 11},
    /* Central Africa */
    {33,  6, 38,  9},
    /* Southern Africa */
    {34,  3, 37,  6},
    /* Madagascar */
    {39,  4, 40,  6},
    /* Middle East / Arabia */
    {37,  9, 41, 11},
    /* Turkey / Caucasus bridge */
    {35, 12, 39, 12},
    /* Russia / Siberia (long northern strip) */
    {33, 13, 60, 14},
    /* Iran / Central Asia */
    {40, 11, 44, 12},
    /* India */
    {44,  9, 47, 11},
    {45,  8, 46,  9},
    /* Himalayas / Tibet */
    {44, 12, 52, 12},
    /* China */
    {50, 11, 58, 12},
    /* Southeast Asia (Indochina) */
    {50,  9, 53, 11},
    /* Korea / Japan */
    {57, 12, 59, 13},
    {59, 11, 60, 12},
    /* Indonesia / Philippines */
    {50,  7, 56,  8},
    {57,  8, 60,  9},
    /* Papua New Guinea */
    {57,  6, 60,  7},
    /* Australia */
    {53,  4, 60,  6},
    {54,  3, 59,  4},
    /* New Zealand */
    {61,  3, 62,  4},
    /* Tasmania */
    {57,  3, 58,  3},
    /* Iceland (small dot) */
    {28, 14, 29, 14},
    /* Sri Lanka */
    {47,  8, 47,  8},
    /* Cuba / Caribbean smudge */
    {18,  9, 21,  9},
};

static const int LANDS_COUNT = sizeof(lands) / sizeof(lands[0]);

/* "Sea cuts" — rectangles to clear AFTER land is drawn, for inland seas/bays. */
static const Rect sea_cuts[] = {
    /* Hudson Bay */
    {13, 13, 14, 13},
    /* Mediterranean */
    {31, 12, 35, 12},
    /* Black + Caspian Sea */
    {36, 12, 37, 12},
    /* Persian Gulf */
    {39, 11, 40, 11},
    /* Bay of Bengal indent */
    {47,  9, 47, 10},
    /* Sea of Japan */
    {59, 13, 59, 13},
    /* Great Australian Bight */
    {55,  3, 56,  3},
    /* Baltic */
    {32, 13, 33, 13},
};

static const int SEA_CUTS_COUNT = sizeof(sea_cuts) / sizeof(sea_cuts[0]);

static void fill_rect(const Rect *r, uint8_t value) {
    int x0 = r->x0, y0 = r->y0, x1 = r->x1, y1 = r->y1;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= MAP_W) x1 = MAP_W - 1;
    if (y1 >= MAP_H) y1 = MAP_H - 1;
    for (int x = x0; x <= x1; x++) {
        for (int y = y0; y <= y1; y++) {
            earth_map[x][y] = value;
        }
    }
}

EXPORT(init)
void init(void) {
    /* Clear to sea */
    for (int x = 0; x < MAP_W; x++) {
        for (int y = 0; y < MAP_H; y++) {
            earth_map[x][y] = 0;
        }
    }
    /* Paint continents */
    for (int i = 0; i < LANDS_COUNT; i++) {
        fill_rect(&lands[i], 1);
    }
    /* Cut out inland seas */
    for (int i = 0; i < SEA_CUTS_COUNT; i++) {
        fill_rect(&sea_cuts[i], 0);
    }
}

/* ---- Color palettes ---- */
static void pick_land_color(int palette, int *r, int *g, int *b) {
    switch (palette) {
        case 1: *r = 180; *g = 120; *b = 60;  break;  /* Desert */
        case 2: *r = 220; *g = 200; *b = 120; break;  /* Sand */
        case 3: *r = 220; *g = 220; *b = 240; break;  /* Snow */
        case 4: *r = 80;  *g = 255; *b = 100; break;  /* Neon */
        default: *r = 60; *g = 160; *b = 70;  break;  /* Forest */
    }
}

static void pick_sea_color(int palette, int *r, int *g, int *b) {
    switch (palette) {
        case 1: *r = 20;  *g = 40;  *b = 120; break;  /* Deep */
        case 2: *r = 30;  *g = 130; *b = 130; break;  /* Teal */
        case 3: *r = 60;  *g = 180; *b = 220; break;  /* Cyan */
        case 4: *r = 8;   *g = 8;   *b = 24;  break;  /* Dark */
        default: *r = 40; *g = 90;  *b = 200; break;  /* Ocean */
    }
}

/* ---- Scroll state (Q24.8) ---- */
static int32_t scroll_q8 = 0;
static int32_t prev_tick = 0;

EXPORT(update)
void update(int tick_ms) {
    int speed       = get_param_i32(0);  /* 0..100 */
    int land_pal    = get_param_i32(1);
    int sea_pal     = get_param_i32(2);
    int land_bright = get_param_i32(3);  /* 0..255 */
    int sea_bright  = get_param_i32(4);  /* 0..255 */
    int direction   = get_param_i32(5);  /* 0=W→E, 1=E→W */
    int fit_mode    = get_param_i32(6);  /* 0=Stretch, 1=Native (1:1, window over map) */

    int W = get_width();
    int H = get_height();
    if (W < 1) W = 1;
    if (H < 1) H = 1;

    /* dt-based scroll: speed=100 ≈ 4 columns per second across the map. */
    int32_t dt_ms = tick_ms - prev_tick;
    prev_tick = tick_ms;
    if (dt_ms < 0 || dt_ms > 500) dt_ms = 33;

    /* px/sec on the source map ≈ speed * 0.04. In Q8: scroll_step_q8 = speed * dt_ms * 256 * 0.04 / 1000
     *                                                                = speed * dt_ms * 10.24 / 1000  ≈ speed * dt_ms / 98 */
    int32_t scroll_step = (speed * dt_ms * 10) / 1000;
    if (direction == 1) scroll_step = -scroll_step;
    scroll_q8 += scroll_step;
    int32_t map_w_q8 = MAP_W << 8;
    while (scroll_q8 >= map_w_q8) scroll_q8 -= map_w_q8;
    while (scroll_q8 < 0) scroll_q8 += map_w_q8;

    int land_r, land_g, land_b;
    int sea_r,  sea_g,  sea_b;
    pick_land_color(land_pal, &land_r, &land_g, &land_b);
    pick_sea_color(sea_pal,   &sea_r,  &sea_g,  &sea_b);

    /* Pre-scale palette colors by their respective brightness so the per-pixel
     * blend is a single multiply. Final values are 0..255. */
    int lr = (land_r * land_bright) / 255;
    int lg = (land_g * land_bright) / 255;
    int lb = (land_b * land_bright) / 255;
    int sr = (sea_r  * sea_bright)  / 255;
    int sg = (sea_g  * sea_bright)  / 255;
    int sb = (sea_b  * sea_bright)  / 255;

    for (int x = 0; x < W; x++) {
        /* Map screen X → source X with sub-pixel precision so scrolling looks
         * smooth even when the scroll step is fractional.
         *  - Stretch (mode 0): fit MAP_W into W → entire planet visible at once.
         *  - Native  (mode 1): 1 screen px = 1 map px, screen is a window onto
         *    a cylindrical map. With W < MAP_W the window crosses col 63→0 as
         *    it scrolls (the seam runs through the Pacific date line). */
        int32_t src_x_q8;
        if (fit_mode == 1) {
            src_x_q8 = ((int32_t)x << 8) + scroll_q8;
        } else {
            src_x_q8 = ((int32_t)x * map_w_q8) / W + scroll_q8;
        }
        while (src_x_q8 >= map_w_q8) src_x_q8 -= map_w_q8;
        while (src_x_q8 < 0) src_x_q8 += map_w_q8;
        int sx_lo = (src_x_q8 >> 8) % MAP_W;
        int sx_hi = (sx_lo + 1) % MAP_W;
        int frac_x = src_x_q8 & 0xFF;       /* 0..255 weight toward sx_hi */

        for (int y = 0; y < H; y++) {
            /* Y mapping: stretch / shrink as needed (no anti-alias on Y). */
            int sy = (y * MAP_H) / H;
            if (sy < 0) sy = 0;
            if (sy >= MAP_H) sy = MAP_H - 1;

            /* land_low / land_high are 0 (sea) or 1 (land). Blend by frac_x to
             * get a soft horizontal anti-alias along coastlines. */
            int lLo = earth_map[sx_lo][sy];
            int lHi = earth_map[sx_hi][sy];
            /* land weight scaled to 0..256 */
            int wL = lLo * (256 - frac_x) + lHi * frac_x;
            int wS = 256 - wL;

            int r = (lr * wL + sr * wS) >> 8;
            int g = (lg * wL + sg * wS) >> 8;
            int b = (lb * wL + sb * wS) >> 8;
            if (r > 255) r = 255;
            if (g > 255) g = 255;
            if (b > 255) b = 255;
            set_pixel(x, y, r, g, b);
        }
    }

    draw();
}
