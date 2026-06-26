#include "api.h"

/* ---- Metadata JSON ---- */
static const char META[] =
    "{\"name\":\"Earth\","
    "\"desc\":\"Detailed 32x32 world map slowly rotating across the display\","
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
         "\"default\":1,"
         "\"options\":[\"Stretch\",\"Native\"],"
         "\"desc\":\"1:1 native pixels (window over the map), or stretch whole map to display\"}"
    "]}";

EXPORT(get_meta_ptr)
int get_meta_ptr(void) { return (int)META; }

EXPORT(get_meta_len)
int get_meta_len(void) { return sizeof(META) - 1; }

/* ---- Earth bitmap, equirectangular, cylindrical wrap. ----
 * The art below is 32 wide; it is expanded to 64 wide at load so the map keeps
 * the true 2:1 world proportions (360° x 180°) instead of looking squished.
 * Columns: col 0 ≈ 180° (date line), col 32 ≈ 0° (Greenwich), increasing east.
 * Rows in earth_map: y=31 = Arctic (north), y=0 = Antarctic (south).
 * Authored as ASCII art, north (top) -> south (bottom); '#' = land. */

#define SRC_W 32
#define MAP_W 64
#define MAP_H 32
static uint8_t earth_map[MAP_W][MAP_H];

/* rows[0] is the northernmost line (maps to y=31). */
static const char *const rows[MAP_H] = {
    "......##.....##.......######....",  /* Arctic ice / N. islands       */
    ".########...###..##.###########.",  /* N.Canada, Greenland, Scand, Siberia */
    "..#######...#######.##########..",  /* Canada, Europe, Russia        */
    "..#.#####...#.####..#########...",  /* Alaska, Canada, Europe, C.Asia */
    "....#####......###..##########..",  /* USA, Europe, C.Asia/China     */
    "....#####......##....########...",  /* USA, Iberia, China            */
    ".....####......#....########....",  /* USA, Iberia, China/Japan      */
    ".....####......#############....",  /* USA, N.Africa..China belt     */
    "......###......############.#...",  /* Mexico, Sahara..China, Japan  */
    ".......##.....########.####.#...",  /* Mexico, Sahara/Arabia, India, Japan */
    "........##....#######..#.#..#...",  /* C.America, Africa, India, SEAsia, PH */
    ".........####...#####....####...",  /* N.S.America, Africa, Indonesia */
    "..........####..#####..######...",  /* Amazon, Africa, India/Indonesia */
    "...........####.#####...#####...",  /* Brazil, Congo, Indonesia      */
    "...........####..####....###....",  /* Brazil, Africa, Indonesia     */
    "............###..####....##.....",  /* Brazil, Africa, Indonesia     */
    "............###...#####.........",  /* Brazil, S.Africa, Madagascar  */
    "...........###....####...#####..",  /* Argentina, S.Africa, Australia */
    "...........###....##.....#####..",  /* Argentina, S.Africa, Australia */
    "...........##.....##.....#####..",  /* Argentina, S.Africa, Australia */
    "..........###.....#.......###...",  /* Argentina, S.Afr tip, Australia */
    "..........##..............##..##",  /* Patagonia, S.Australia, NZ    */
    "..........##...............#..##",  /* Patagonia, Tasmania, NZ       */
    "..........#...................#.",  /* Patagonia tip, NZ             */
    "..........#.....................",  /* Patagonia tip                 */
    "................................",  /* Southern Ocean                */
    "................................",  /* Southern Ocean                */
    "................................",  /* Southern Ocean                */
    "#..#..#..#..#..#..#..#..#..#..#.",  /* Antarctic coast               */
    "################################",  /* Antarctica                    */
    "################################",  /* Antarctica                    */
    "################################",  /* Antarctica                    */
};

EXPORT(init)
void init(void) {
    for (int x = 0; x < MAP_W; x++)
        for (int y = 0; y < MAP_H; y++)
            earth_map[x][y] = 0;

    /* rows[i] is north->south; map row i to y = (MAP_H-1 - i).
     * Each source column is written to two map columns so the 32-wide art
     * fills the 64-wide map at true 2:1 world proportions. */
    for (int i = 0; i < MAP_H; i++) {
        const char *s = rows[i];
        int y = MAP_H - 1 - i;
        for (int x = 0; s[x] && x < SRC_W; x++) {
            if (s[x] == '#') {
                int dx = x * 2;
                earth_map[dx][y] = 1;
                if (dx + 1 < MAP_W) earth_map[dx + 1][y] = 1;
            }
        }
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
    int fit_mode    = get_param_i32(6);  /* 0=Stretch, 1=Native */

    int W = get_width();
    int H = get_height();
    if (W < 1) W = 1;
    if (H < 1) H = 1;

    int32_t dt_ms = tick_ms - prev_tick;
    prev_tick = tick_ms;
    if (dt_ms < 0 || dt_ms > 500) dt_ms = 33;

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

    int lr = (land_r * land_bright) / 255;
    int lg = (land_g * land_bright) / 255;
    int lb = (land_b * land_bright) / 255;
    int sr = (sea_r  * sea_bright)  / 255;
    int sg = (sea_g  * sea_bright)  / 255;
    int sb = (sea_b  * sea_bright)  / 255;

    for (int x = 0; x < W; x++) {
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
        int frac_x = src_x_q8 & 0xFF;

        for (int y = 0; y < H; y++) {
            int sy = (y * MAP_H) / H;
            if (sy < 0) sy = 0;
            if (sy >= MAP_H) sy = MAP_H - 1;

            int lLo = earth_map[sx_lo][sy];
            int lHi = earth_map[sx_hi][sy];
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
