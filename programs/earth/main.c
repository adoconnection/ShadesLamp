#include "api.h"

/* ---- Metadata JSON ---- */
static const char META[] =
    "{\"name\":\"Earth\","
    "\"desc\":\"Detailed 32x32 world map slowly rotating across the display\","
    "\"params\":["
        "{\"id\":0,\"name\":\"Speed\",\"type\":\"int\","
         "\"min\":0,\"max\":100,\"default\":30,"
         "\"desc\":\"Rotation speed (0 stops the globe)\"},"
        "{\"id\":1,\"name\":\"Style\",\"type\":\"select\","
         "\"default\":0,"
         "\"options\":[\"Map\",\"Satellite\",\"Night\",\"Neon\",\"Land only\",\"Water only\"],"
         "\"desc\":\"Colour theme (land + sea together)\"},"
        "{\"id\":2,\"name\":\"LandBrightness\",\"type\":\"int\","
         "\"min\":0,\"max\":255,\"default\":200,"
         "\"desc\":\"Brightness of land pixels\"},"
        "{\"id\":3,\"name\":\"SeaBrightness\",\"type\":\"int\","
         "\"min\":0,\"max\":255,\"default\":80,"
         "\"desc\":\"Brightness of sea pixels\"},"
        "{\"id\":4,\"name\":\"Direction\",\"type\":\"select\","
         "\"default\":0,"
         "\"options\":[\"West to East\",\"East to West\"],"
         "\"desc\":\"Rotation direction\"},"
        "{\"id\":5,\"name\":\"Fit\",\"type\":\"select\","
         "\"default\":1,"
         "\"options\":[\"Stretch\",\"Native\"],"
         "\"desc\":\"1:1 native pixels (window over the map), or stretch whole map to display\"}"
    "]}";

EXPORT(get_meta_ptr)
int get_meta_ptr(void) { return (int)META; }

EXPORT(get_meta_len)
int get_meta_len(void) { return sizeof(META) - 1; }

/* ---- Earth bitmap, cylindrical wrap. ----
 * The art below is 32 wide x 25 tall; it is resampled at load to MAP_W x MAP_H.
 * MAP_H = 32 matches the matrix height (no vertical scaling on a 32-row lamp),
 * and MAP_W = 53 gives the classic world-map aspect ~1.65:1 (53/32 = 1.656).
 * Polar caps (Arctic ice, Antarctica) are omitted — only the inhabited band.
 * Columns: col 0 ≈ 180° (date line), increasing east. Authored north -> south. */

#define SRC_W 32
#define ART_H 28
#define MAP_W 53
#define MAP_H 32
static uint8_t earth_map[MAP_W][MAP_H];

/* Base land/sea shape. col 0 ≈ 180°W (left), col 16 ≈ 0° (centre), col 31 ≈ 180°E.
 * rows[0] is the northernmost art line. '#' = land. */
static const char *const rows[ART_H] = {
    "................................",  /* Arctic Ocean (open water)        */
    "......#.#...#.........#..#..#....",  /* Arctic islands / Greenland tip   */
    "..#.######.###......##########..",  /* N.Canada, Greenland, Siberia     */
    "..############...##############.",  /* Canada, Scandinavia, Russia      */
    "#############...###############.",  /* Canada, Europe, Siberia          */
    "#..#########...################.",  /* Alaska, Canada, Europe, Russia   */
    "....#######....###############..",  /* USA, Europe, C.Asia              */
    "....#######.....###.#########...",  /* USA, Europe, C.Asia/China        */
    ".....######....###...########...",  /* USA, Iberia, C.Asia/China/Japan  */
    ".....######....#......#######...",  /* USA, Iberia, China/Japan         */
    "......#####....###..####.###....",  /* USA, N.Africa, MidEast, China    */
    "......###......############.....",  /* Mexico, Sahara..China belt       */
    "......###......############.....",  /* Mexico, Sahara/Arabia/India belt */
    ".......###.....#########.##.#...",  /* C.America, Africa, India, SEA,PH */
    "........##....########.#.##.#...",  /* C.America, Africa, India, SEA,PH */
    ".........##....#######.#.#..#...",  /* Colombia, Africa, India, SEA,PH  */
    ".........####...#####.....#####.",  /* N.S.America, Africa, Indonesia   */
    "..........####..#####.....#####.",  /* Amazon, Congo, Indonesia         */
    "...........####.#####......###..",  /* Brazil, Congo, Indonesia         */
    "...........####..#####.....##...",  /* Brazil, S.Africa, Madagascar     */
    "...........###...###.#.....####.",  /* Brazil, S.Africa, Madag, Australia */
    "..........####...###......#####.",  /* Argentina, S.Africa, Australia   */
    "..........###.....##......#####.",  /* Argentina, S.Africa, Australia   */
    "..........###.....#........###.#",  /* Argentina, S.Afr tip, Australia, NZ */
    "..........##...............##..#",  /* Patagonia, Australia, NZ         */
    "..........##...................#",  /* Patagonia, NZ                    */
    "..........#.....................",  /* Patagonia tip                    */
    "................................",  /* Southern Ocean                   */
};

/* Desert overlay (same grid): '#' marks desert where the base has land. */
static const char *const desert[ART_H] = {
    "................................",
    "................................",
    "................................",
    "................................",
    "................................",
    "................................",
    "................................",
    "................................",
    "........................###.....",  /* Gobi / Central Asia              */
    "................................",
    "......#........###......###.....",  /* SW US, Sahara N, Gobi            */
    "......#........#######...##.....",  /* Mexico, Sahara, Arabia, Gobi     */
    "...............########.........",  /* Sahara, Arabia                   */
    "................###.............",  /* Sahel edge                       */
    "................................",
    "................................",
    "................................",
    "................................",
    "................................",
    "................................",
    "..................##.......##...",  /* Kalahari, Australia              */
    "..................#.......####..",  /* Kalahari, Australia outback      */
    "...........................##...",  /* Australia                        */
    "................................",
    "................................",
    "................................",
    "................................",
    "................................",
};

EXPORT(init)
void init(void) {
    /* Resample the 32x25 art to the MAP_W x MAP_H map (nearest neighbour).
     * MAP_H matches the matrix height; MAP_W gives the ~1.65:1 world aspect.
     * y = MAP_H-1 is north. */
    for (int y = 0; y < MAP_H; y++) {
        int t  = (MAP_H - 1) - y;            /* 0 = northernmost */
        int ai = (t * ART_H) / MAP_H;        /* nearest art row */
        if (ai >= ART_H) ai = ART_H - 1;
        const char *s = rows[ai];
        const char *d = desert[ai];
        for (int dx = 0; dx < MAP_W; dx++) {
            int sx = (dx * SRC_W) / MAP_W;   /* nearest art column 0..SRC_W-1 */
            int v = 0;                       /* 0 sea, 1 forest, 2 desert, 3 ice */
            if (s[sx] == '#') {
                v = 1;
                if (d[sx] == '#') v = 2;
                if (ai <= 4)      v = 3;     /* northern rows -> snow/ice */
            }
            earth_map[dx][y] = (uint8_t)v;
        }
    }
}

static uint32_t hashu(uint32_t a) {
    a ^= a >> 16; a *= 0x7feb352du; a ^= a >> 15; a *= 0x846ca68bu; a ^= a >> 16; return a;
}

/* ---- Colour themes: land + sea picked together ---- */
static void pick_theme(int style, int *lr, int *lg, int *lb, int *sr, int *sg, int *sb) {
    switch (style) {
        case 1: *lr=70;  *lg=115; *lb=50;  *sr=6;  *sg=18; *sb=48;  break; /* Satellite */
        case 2: *lr=255; *lg=185; *lb=80;  *sr=14; *sg=22; *sb=80;  break; /* Night (city lights) */
        case 3: *lr=60;  *lg=255; *lb=130; *sr=10; *sg=12; *sb=40;  break; /* Neon */
        case 4: *lr=80;  *lg=200; *lb=90;  *sr=0;  *sg=0;  *sb=0;   break; /* Land only */
        case 5: *lr=0;   *lg=0;   *lb=0;   *sr=40; *sg=120;*sb=220; break; /* Water only */
        default:*lr=60;  *lg=160; *lb=70;  *sr=40; *sg=90; *sb=200; break; /* Map */
    }
}

/* ---- Scroll state (Q24.8) ---- */
static int32_t scroll_q8 = 0;
static int32_t prev_tick = 0;

EXPORT(update)
void update(int tick_ms) {
    int speed       = get_param_i32(0);  /* 0..100 */
    int style       = get_param_i32(1);  /* colour theme */
    int land_bright = get_param_i32(2);  /* 0..255 */
    int sea_bright  = get_param_i32(3);  /* 0..255 */
    int direction   = get_param_i32(4);  /* 0=W→E, 1=E→W */
    int fit_mode    = get_param_i32(5);  /* 0=Stretch, 1=Native */

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
    pick_theme(style, &land_r, &land_g, &land_b, &sea_r, &sea_g, &sea_b);

    int lr = (land_r * land_bright) / 255;
    int lg = (land_g * land_bright) / 255;
    int lb = (land_b * land_bright) / 255;
    int sr = (sea_r  * sea_bright)  / 255;
    int sg = (sea_g  * sea_bright)  / 255;
    int sb = (sea_b  * sea_bright)  / 255;

    int night = (style == 2);            /* city-lights sparkle on land */
    int twk   = tick_ms / 320;           /* slow twinkle bucket */

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

            int tLo = earth_map[sx_lo][sy];   /* 0 sea, 1 forest, 2 desert, 3 ice */
            int tHi = earth_map[sx_hi][sy];
            int lLo = tLo ? 1 : 0;
            int lHi = tHi ? 1 : 0;
            int wL = lLo * (256 - frac_x) + lHi * frac_x;
            int wS = 256 - wL;

            int plr, plg, plb;
            if (style == 0) {
                /* Map: multicolour by terrain type */
                int tt = tLo ? tLo : tHi;
                switch (tt) {
                    case 2:  plr=(200*land_bright)/255; plg=(170*land_bright)/255; plb=(95 *land_bright)/255; break; /* desert */
                    case 3:  plr=(225*land_bright)/255; plg=(228*land_bright)/255; plb=(245*land_bright)/255; break; /* ice    */
                    default: plr=(60 *land_bright)/255; plg=(160*land_bright)/255; plb=(70 *land_bright)/255; break; /* forest */
                }
            } else {
                plr = lr; plg = lg; plb = lb;
                if (night && wL > 0) {
                    /* city lights: skewed per-cell brightness + slow twinkle */
                    uint32_t h = hashu((uint32_t)(sx_lo * 131 + sy * 977));
                    int s8 = (int)(h & 255);
                    int f  = 45 + (s8 * s8) / 300;      /* mostly dim, a few bright */
                    if (f > 255) f = 255;
                    uint32_t h2 = hashu(h ^ (uint32_t)twk);
                    int tw = 205 + (int)(h2 & 50);      /* 205..255 shimmer */
                    f = (f * tw) / 255;
                    plr = (lr * f) / 255;
                    plg = (lg * f) / 255;
                    plb = (lb * f) / 255;
                }
            }

            int r = (plr * wL + sr * wS) >> 8;
            int g = (plg * wL + sg * wS) >> 8;
            int b = (plb * wL + sb * wS) >> 8;
            if (r > 255) r = 255;
            if (g > 255) g = 255;
            if (b > 255) b = 255;
            set_pixel(x, y, r, g, b);
        }
    }

    draw();
}
