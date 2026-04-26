#include "api.h"

/* ---- Metadata JSON ---- */
static const char META[] =
    "{\"name\":\"Pulse\","
    "\"desc\":\"Expanding concentric rings pulsing outward from center with fading glow\","
    "\"params\":["
        "{\"id\":0,\"name\":\"Speed\",\"type\":\"int\","
         "\"min\":1,\"max\":10,\"default\":5,"
         "\"desc\":\"Expansion speed and spawn rate\"},"
        "{\"id\":1,\"name\":\"Hue\",\"type\":\"int\","
         "\"min\":0,\"max\":255,\"default\":0,"
         "\"desc\":\"Base hue (0=rainbow cycle)\"},"
        "{\"id\":2,\"name\":\"Brightness\",\"type\":\"int\","
         "\"min\":1,\"max\":255,\"default\":220,"
         "\"desc\":\"Overall brightness\"}"
    "]}";

EXPORT(get_meta_ptr)
int get_meta_ptr(void) { return (int)META; }

EXPORT(get_meta_len)
int get_meta_len(void) { return sizeof(META) - 1; }

/* ---- PRNG (xorshift32) ---- */
static uint32_t rng_state = 77777;

static uint32_t rng_next(void) {
    uint32_t x = rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng_state = x;
    return x;
}

static int random_range(int lo, int hi) {
    if (lo >= hi) return lo;
    return lo + (int)(rng_next() % (uint32_t)(hi - lo));
}

/* ---- HSV to RGB ---- */
static void hsv_to_rgb(int h, int s, int v, int *r, int *g, int *b) {
    h = h & 255;
    if (s == 0) { *r = v; *g = v; *b = v; return; }
    int region = h / 43;
    int remainder = (h - region * 43) * 6;
    int p = (v * (255 - s)) >> 8;
    int q = (v * (255 - ((s * remainder) >> 8))) >> 8;
    int t = (v * (255 - ((s * (255 - remainder)) >> 8))) >> 8;
    switch (region) {
        case 0:  *r = v; *g = t; *b = p; break;
        case 1:  *r = q; *g = v; *b = p; break;
        case 2:  *r = p; *g = v; *b = t; break;
        case 3:  *r = p; *g = q; *b = v; break;
        case 4:  *r = t; *g = p; *b = v; break;
        default: *r = v; *g = p; *b = q; break;
    }
}

/* ---- Cos lookup (for brightness modulation like GyverLamp) ---- */
static const signed char sin_table[256] = {
      0,   3,   6,   9,  12,  16,  19,  22,  25,  28,  31,  34,  37,  40,  43,  46,
     49,  51,  54,  57,  60,  63,  65,  68,  71,  73,  76,  78,  81,  83,  85,  88,
     90,  92,  94,  96,  98, 100, 102, 104, 106, 107, 109, 111, 112, 113, 115, 116,
    117, 118, 120, 121, 122, 122, 123, 124, 125, 125, 126, 126, 126, 127, 127, 127,
    127, 127, 127, 127, 126, 126, 126, 125, 125, 124, 123, 122, 122, 121, 120, 118,
    117, 116, 115, 113, 112, 111, 109, 107, 106, 104, 102, 100,  98,  96,  94,  92,
     90,  88,  85,  83,  81,  78,  76,  73,  71,  68,  65,  63,  60,  57,  54,  51,
     49,  46,  43,  40,  37,  34,  31,  28,  25,  22,  19,  16,  12,   9,   6,   3,
      0,  -3,  -6,  -9, -12, -16, -19, -22, -25, -28, -31, -34, -37, -40, -43, -46,
    -49, -51, -54, -57, -60, -63, -65, -68, -71, -73, -76, -78, -81, -83, -85, -88,
    -90, -92, -94, -96, -98,-100,-102,-104,-106,-107,-109,-111,-112,-113,-115,-116,
   -117,-118,-120,-121,-122,-122,-123,-124,-125,-125,-126,-126,-126,-127,-127,-127,
   -127,-127,-127,-127,-126,-126,-126,-125,-125,-124,-123,-122,-122,-121,-120,-118,
   -117,-116,-115,-113,-112,-111,-109,-107,-106,-104,-102,-100, -98, -96, -94, -92,
    -90, -88, -85, -83, -81, -78, -76, -73, -71, -68, -65, -63, -60, -57, -54, -51,
    -49, -46, -43, -40, -37, -34, -31, -28, -25, -22, -19, -16, -12,  -9,  -6,  -3
};

/* cos8: returns 0..255 (unsigned), matching FastLED cos8 behavior:
   cos8(0) = 255 (max), cos8(128) = 0 (min) */
static int cos8_unsigned(int angle) {
    /* sin_table gives -127..127. cos(x) = sin(x + 64).
       Map to 0..254 range, similar to FastLED cos8. */
    int val = sin_table[(angle + 64) & 255]; /* -127..127 */
    return (val + 127); /* 0..254 */
}

/* ---- Pulse ring state ---- */
#define MAX_PULSES 8

struct Pulse {
    int center_x;   /* emit position (in pixels) */
    int center_y;
    int radius;      /* current radius (in 8.8 fixed point) */
    int max_radius;  /* max radius before pulse dies */
    int hue;         /* color hue */
    int active;      /* 1 = alive */
};

static struct Pulse pulses[MAX_PULSES];

#define MAX_W 64
#define MAX_H 64

static uint8_t fb_r[MAX_W][MAX_H];
static uint8_t fb_g[MAX_W][MAX_H];
static uint8_t fb_b[MAX_W][MAX_H];

static uint32_t tick_acc;
static int spawn_timer; /* counts down to spawn a new pulse */
static int hue_cycle;   /* for rainbow mode */

EXPORT(init)
void init(void) {
    tick_acc = 0;
    spawn_timer = 0;
    hue_cycle = 0;
    rng_state = 31415;

    for (int i = 0; i < MAX_PULSES; i++)
        pulses[i].active = 0;

    for (int x = 0; x < MAX_W; x++)
        for (int y = 0; y < MAX_H; y++) {
            fb_r[x][y] = 0;
            fb_g[x][y] = 0;
            fb_b[x][y] = 0;
        }
}

/* Draw a circle using Bresenham's midpoint algorithm (adapted from GyverLamp) */
static void draw_circle(int x0, int y0, int radius, int r, int g, int b, int W, int H) {
    if (radius == 0) {
        /* Single pixel */
        int px = ((x0 % W) + W) % W;
        if (y0 >= 0 && y0 < H) {
            int nr = fb_r[px][y0] + r; if (nr > 255) nr = 255;
            int ng = fb_g[px][y0] + g; if (ng > 255) ng = 255;
            int nb = fb_b[px][y0] + b; if (nb > 255) nb = 255;
            fb_r[px][y0] = (uint8_t)nr;
            fb_g[px][y0] = (uint8_t)ng;
            fb_b[px][y0] = (uint8_t)nb;
        }
        return;
    }

    int a = radius, bv = 0;
    int err = 1 - a;

    while (a >= bv) {
        /* 8 octant points */
        int pts[8][2] = {
            { x0 + a, y0 + bv }, { x0 + bv, y0 + a },
            { x0 - a, y0 + bv }, { x0 - bv, y0 + a },
            { x0 - a, y0 - bv }, { x0 - bv, y0 - a },
            { x0 + a, y0 - bv }, { x0 + bv, y0 - a }
        };
        for (int p = 0; p < 8; p++) {
            int px = ((pts[p][0] % W) + W) % W; /* cylinder wrap */
            int py = pts[p][1];
            if (py >= 0 && py < H) {
                int nr = fb_r[px][py] + r; if (nr > 255) nr = 255;
                int ng = fb_g[px][py] + g; if (ng > 255) ng = 255;
                int nb = fb_b[px][py] + b; if (nb > 255) nb = 255;
                fb_r[px][py] = (uint8_t)nr;
                fb_g[px][py] = (uint8_t)ng;
                fb_b[px][py] = (uint8_t)nb;
            }
        }
        bv++;
        if (err < 0) {
            err += 2 * bv + 1;
        } else {
            a--;
            err += 2 * (bv - a + 1);
        }
    }
}

EXPORT(update)
void update(int tick_ms) {
    int speed  = get_param_i32(0);
    int hue    = get_param_i32(1);
    int bright = get_param_i32(2);
    int W = get_width();
    int H = get_height();

    if (W > MAX_W) W = MAX_W;
    if (H > MAX_H) H = MAX_H;
    if (W < 1) W = 1;
    if (H < 1) H = 1;

    rng_state ^= (uint32_t)tick_ms;
    tick_acc += (uint32_t)tick_ms;
    hue_cycle = (int)(tick_acc / 37) & 255;

    /* Fade framebuffer */
    int fade = 6 + speed * 3;
    for (int x = 0; x < W; x++) {
        for (int y = 0; y < H; y++) {
            int rv = fb_r[x][y] - fade;
            int gv = fb_g[x][y] - fade;
            int bv = fb_b[x][y] - fade;
            fb_r[x][y] = (uint8_t)(rv < 0 ? 0 : rv);
            fb_g[x][y] = (uint8_t)(gv < 0 ? 0 : gv);
            fb_b[x][y] = (uint8_t)(bv < 0 ? 0 : bv);
        }
    }

    /* Spawn new pulses */
    spawn_timer -= tick_ms;
    if (spawn_timer <= 0) {
        /* Find an inactive slot */
        for (int i = 0; i < MAX_PULSES; i++) {
            if (!pulses[i].active) {
                pulses[i].active = 1;
                pulses[i].center_x = random_range(W / 4, W * 3 / 4);
                pulses[i].center_y = random_range(H / 4, H * 3 / 4);
                pulses[i].radius = 0;
                int max_dim = W > H ? W : H;
                pulses[i].max_radius = random_range(max_dim / 3, max_dim / 2 + 2);
                if (hue == 0) {
                    pulses[i].hue = hue_cycle + random_range(0, 60);
                } else {
                    pulses[i].hue = hue;
                }
                break;
            }
        }
        /* Spawn interval: faster speed = more frequent */
        spawn_timer = 800 - speed * 65;
        if (spawn_timer < 120) spawn_timer = 120;
    }

    /* Update and draw each active pulse */
    for (int i = 0; i < MAX_PULSES; i++) {
        if (!pulses[i].active) continue;

        /* Expand radius */
        pulses[i].radius += speed * 32 + 20; /* 8.8 fixed point increment */
        int rad = pulses[i].radius >> 8;

        if (rad >= pulses[i].max_radius) {
            pulses[i].active = 0;
            continue;
        }

        /* Brightness falls off with radius using cos8-like curve */
        /* At radius=0, full brightness. At max_radius, zero. */
        int progress = rad * 128 / pulses[i].max_radius; /* 0..128 */
        int dark = cos8_unsigned(progress); /* 254..0 as progress 0..128 */
        dark = dark * bright / 254;
        if (dark < 2) {
            pulses[i].active = 0;
            continue;
        }

        /* Draw the ring at current radius */
        int ph = pulses[i].hue & 255;
        int r, g, b;
        hsv_to_rgb(ph, 255, dark, &r, &g, &b);

        draw_circle(pulses[i].center_x, pulses[i].center_y, rad, r, g, b, W, H);

        /* Draw a slightly dimmer inner ring for thickness */
        if (rad > 0) {
            int inner_dark = dark * 2 / 3;
            hsv_to_rgb(ph, 255, inner_dark, &r, &g, &b);
            draw_circle(pulses[i].center_x, pulses[i].center_y, rad - 1, r, g, b, W, H);
        }
    }

    /* Output framebuffer */
    for (int x = 0; x < W; x++)
        for (int y = 0; y < H; y++)
            set_pixel(x, y, fb_r[x][y], fb_g[x][y], fb_b[x][y]);

    draw();
}
