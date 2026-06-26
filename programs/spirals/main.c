#include "api.h"

/* ---- Metadata JSON ---- */
static const char META[] =
    "{\"name\":\"Spirals\","
    "\"desc\":\"Rotating spirograph pattern with multiple orbiting points\","
    "\"params\":["
        "{\"id\":0,\"name\":\"Count\",\"type\":\"int\","
         "\"min\":1,\"max\":6,\"default\":3,"
         "\"desc\":\"Number of spiral arms\"},"
        "{\"id\":1,\"name\":\"Speed\",\"type\":\"int\","
         "\"min\":1,\"max\":10,\"default\":5,"
         "\"desc\":\"Rotation speed\"},"
        "{\"id\":2,\"name\":\"Brightness\",\"type\":\"int\","
         "\"min\":1,\"max\":255,\"default\":200,"
         "\"desc\":\"Overall brightness\"},"
        "{\"id\":3,\"name\":\"Spread\",\"type\":\"int\","
         "\"min\":1,\"max\":10,\"default\":5,"
         "\"desc\":\"Spread of the spiral pattern\"},"
        "{\"id\":4,\"name\":\"Particle\",\"type\":\"int\","
         "\"min\":1,\"max\":5,\"default\":1,"
         "\"desc\":\"Size of each particle\"}"
    "]}";

EXPORT(get_meta_ptr)
int get_meta_ptr(void) { return (int)META; }

EXPORT(get_meta_len)
int get_meta_len(void) { return sizeof(META) - 1; }

static int isin(int angle) {
    return (int)(m_sin((float)(angle & 255) * (6.28318530f / 256.0f)) * 127.0f);
}

static int icos(int angle) {
    return (int)(m_cos((float)(angle & 255) * (6.28318530f / 256.0f)) * 127.0f);
}

/* ---- HSV to RGB ---- */
static void hsv_to_rgb(int h, int s, int v, int *r, int *g, int *b) {
    int c = m_hsv(h & 255, s, v);
    *r = (c >> 16) & 255;
    *g = (c >> 8) & 255;
    *b = c & 255;
}

/* ---- State ---- */
#define MAX_W 64
#define MAX_H 64

static uint8_t fb_r[MAX_W][MAX_H];
static uint8_t fb_g[MAX_W][MAX_H];
static uint8_t fb_b[MAX_W][MAX_H];

static uint32_t tick_acc;
static int theta1; /* primary orbit angle (accumulator) */
static int theta2; /* secondary orbit angle (accumulator) */
static int hue_offset;

EXPORT(init)
void init(void) {
    tick_acc = 0;
    theta1 = 0;
    theta2 = 0;
    hue_offset = 0;
    for (int x = 0; x < MAX_W; x++)
        for (int y = 0; y < MAX_H; y++) {
            fb_r[x][y] = 0;
            fb_g[x][y] = 0;
            fb_b[x][y] = 0;
        }
}

/* Add color to framebuffer pixel with saturation */
static void fb_add(int x, int y, int r, int g, int b, int W, int H) {
    /* Horizontal cylinder wrap */
    x = ((x % W) + W) % W;
    if (y < 0 || y >= H) return;
    int nr = fb_r[x][y] + r; if (nr > 255) nr = 255;
    int ng = fb_g[x][y] + g; if (ng > 255) ng = 255;
    int nb = fb_b[x][y] + b; if (nb > 255) nb = 255;
    fb_r[x][y] = (uint8_t)nr;
    fb_g[x][y] = (uint8_t)ng;
    fb_b[x][y] = (uint8_t)nb;
}

/* Draw a soft round particle of the given radius, full brightness at the
   center and fading toward the edge. psize=1 reproduces the original cross. */
static void draw_particle(int cx, int cy, int r, int g, int b,
                          int psize, int W, int H) {
    int rad2 = psize * psize;
    for (int dy = -psize; dy <= psize; dy++) {
        for (int dx = -psize; dx <= psize; dx++) {
            int d2 = dx * dx + dy * dy;
            if (d2 > rad2) continue;
            int fall = 255 - (d2 * 255) / (rad2 + 1);   /* center..edge */
            fb_add(cx + dx, cy + dy, r * fall / 255, g * fall / 255,
                   b * fall / 255, W, H);
        }
    }
}

EXPORT(update)
void update(int tick_ms) {
    int count  = get_param_i32(0);
    int speed  = get_param_i32(1);
    int bright = get_param_i32(2);
    int spread = get_param_i32(3);
    int psize  = get_param_i32(4);
    int W = get_width();
    int H = get_height();

    if (W > MAX_W) W = MAX_W;
    if (H > MAX_H) H = MAX_H;
    if (W < 1) W = 1;
    if (H < 1) H = 1;
    if (count < 1) count = 1;
    if (count > 6) count = 6;
    if (spread < 1) spread = 1;
    if (spread > 10) spread = 10;
    if (psize < 1) psize = 1;
    if (psize > 5) psize = 5;

    tick_acc += (uint32_t)tick_ms;

    /* Fade framebuffer — creates trailing effect */
    int fade = 8 + speed * 2;
    for (int x = 0; x < W; x++) {
        for (int y = 0; y < H; y++) {
            int r = fb_r[x][y] - fade;
            int g = fb_g[x][y] - fade;
            int b = fb_b[x][y] - fade;
            fb_r[x][y] = (uint8_t)(r < 0 ? 0 : r);
            fb_g[x][y] = (uint8_t)(g < 0 ? 0 : g);
            fb_b[x][y] = (uint8_t)(b < 0 ? 0 : b);
        }
    }

    /* Apply light blur: average with direct neighbors (simple 3x3 box, low weight) */
    /* This is expensive on MAX_W*MAX_H, so only do it for the actual matrix size */
    /* Skip blur for performance — the trail fading is sufficient for smooth look */

    /* Update angles */
    int angle_step1 = speed + 1;     /* primary orbit: slower */
    int angle_step2 = speed * 2 + 3; /* secondary orbit: faster */
    theta1 += angle_step1;
    theta2 += angle_step2;
    hue_offset += 1;

    /* Spirograph parameters */
    int cx = W / 2;
    int cy = H / 2;
    /* Spread scales the orbit radii (spread=5 == the original W/4 layout). */
    int radius_x1 = W * spread / 20;
    int radius_y1 = H * spread / 20;
    int radius_x2 = radius_x1 - 1;
    int radius_y2 = radius_y1 - 1;
    if (radius_x1 < 1) radius_x1 = 1;
    if (radius_y1 < 1) radius_y1 = 1;
    if (radius_x2 < 1) radius_x2 = 1;
    if (radius_y2 < 1) radius_y2 = 1;

    int spiro_offset = 256 / count;

    for (int i = 0; i < count; i++) {
        int a1 = (theta1 + i * spiro_offset) & 255;
        int a2 = (theta2 + i * spiro_offset) & 255;

        /* Primary orbit position */
        int px = cx + (isin(a1) * radius_x1 / 127);
        int py = cy + (icos(a1) * radius_y1 / 127);

        /* Secondary orbit around the primary point */
        int x2 = px + (isin(a2) * radius_x2 / 127);
        int y2 = py + (icos(a2) * radius_y2 / 127);

        /* Color: each arm gets a different hue */
        int h = (hue_offset + i * spiro_offset) & 255;
        int r, g, b;
        hsv_to_rgb(h, 255, bright, &r, &g, &b);

        /* Draw the particle at the requested size */
        draw_particle(x2, y2, r, g, b, psize, W, H);
    }

    /* Output framebuffer */
    for (int x = 0; x < W; x++)
        for (int y = 0; y < H; y++)
            set_pixel(x, y, fb_r[x][y], fb_g[x][y], fb_b[x][y]);

    draw();
}
