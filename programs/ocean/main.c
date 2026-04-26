#include "api.h"

/* ---- Metadata JSON ---- */
static const char META[] =
    "{\"name\":\"Ocean\","
    "\"desc\":\"Blue-green ocean wave patterns inspired by Pacifica\","
    "\"params\":["
        "{\"id\":0,\"name\":\"Speed\",\"type\":\"int\","
         "\"min\":1,\"max\":10,\"default\":3,"
         "\"desc\":\"Wave speed\"},"
        "{\"id\":1,\"name\":\"Scale\",\"type\":\"int\","
         "\"min\":1,\"max\":50,\"default\":20,"
         "\"desc\":\"Wave scale\"},"
        "{\"id\":2,\"name\":\"Brightness\",\"type\":\"int\","
         "\"min\":1,\"max\":255,\"default\":180,"
         "\"desc\":\"Overall brightness\"}"
    "]}";

EXPORT(get_meta_ptr)
int get_meta_ptr(void) { return (int)META; }

EXPORT(get_meta_len)
int get_meta_len(void) { return sizeof(META) - 1; }

/* ---- Integer sine lookup table ---- */
/* 64-entry quarter-wave sine table, values 0-255 */
static const uint8_t sin_lut[65] = {
    0,   6,  13,  19,  25,  31,  37,  44,
   50,  56,  62,  68,  74,  80,  86,  92,
   97, 103, 109, 114, 120, 125, 130, 136,
  141, 146, 151, 155, 160, 165, 169, 173,
  177, 181, 185, 189, 193, 196, 199, 202,
  205, 208, 211, 213, 216, 218, 220, 222,
  224, 226, 227, 229, 230, 231, 232, 233,
  234, 234, 235, 235, 235, 235, 235, 235,
  255
};

/* Integer sine: input 0-255 (full cycle), output 0-255 (centered at 128) */
static int isin8(int angle) {
    angle = angle & 0xFF;
    int quadrant = angle >> 6;
    int idx = angle & 0x3F;
    int val;

    switch (quadrant) {
        case 0: val = sin_lut[idx]; break;
        case 1: val = sin_lut[64 - idx]; break;
        case 2: val = -((int)sin_lut[idx]); break;
        case 3: val = -((int)sin_lut[64 - idx]); break;
        default: val = 0; break;
    }

    return (val + 255) >> 1;
}

/* ---- Hash-based value noise ---- */
static int hash_i(int x) {
    x = ((x >> 16) ^ x) * 0x45d9f3b;
    x = ((x >> 16) ^ x) * 0x45d9f3b;
    x = (x >> 16) ^ x;
    return x;
}

static int hash2d(int x, int y) {
    return (hash_i(x * 374761393 + y * 668265263 + 1274126177) & 0xFF);
}

static int lerp8(int a, int b, int t) {
    return a + ((b - a) * t >> 8);
}

static int smooth8(int t) {
    int t2 = (t * t) >> 8;
    int t3 = (t2 * t) >> 8;
    return (3 * t2 - 2 * t3);
}

static int value_noise2d(int fx, int fy) {
    int ix = fx >> 8;
    int iy = fy >> 8;
    int fracx = fx & 0xFF;
    int fracy = fy & 0xFF;

    fracx = smooth8(fracx);
    fracy = smooth8(fracy);

    int v00 = hash2d(ix, iy);
    int v10 = hash2d(ix + 1, iy);
    int v01 = hash2d(ix, iy + 1);
    int v11 = hash2d(ix + 1, iy + 1);

    int top = lerp8(v00, v10, fracx);
    int bot = lerp8(v01, v11, fracx);
    return lerp8(top, bot, fracy);
}

/* ---- Ocean color palettes ---- */
/* Three ocean palettes inspired by Pacifica, blended in layers */

/* Palette 1: Deep dark blue-green */
static void ocean_palette1(int idx, int *r, int *g, int *b) {
    /* Dark ocean: very dark blue transitioning to dark teal */
    if (idx < 200) {
        *r = 0;
        *g = idx / 20;
        *b = idx / 8 + 2;
    } else {
        int s = idx - 200;
        *r = s / 4;
        *g = 10 + s;
        *b = 27 + s / 2;
    }
}

/* Palette 2: Mid blue-green */
static void ocean_palette2(int idx, int *r, int *g, int *b) {
    if (idx < 180) {
        *r = 0;
        *g = idx / 12;
        *b = idx / 5 + 5;
    } else {
        int s = idx - 180;
        *r = s / 5;
        *g = 15 + s * 2 / 3;
        *b = 41 + s / 2;
    }
}

/* Palette 3: Brighter blue with hints of cyan */
static void ocean_palette3(int idx, int *r, int *g, int *b) {
    if (idx < 128) {
        *r = 0;
        *g = idx / 8;
        *b = idx / 3 + 8;
    } else {
        int s = idx - 128;
        *r = s / 8;
        *g = 16 + s / 3;
        *b = 50 + s * 3 / 4;
    }
}

/* Saturating add for uint8 values */
static int sat_add(int a, int b) {
    int s = a + b;
    return s > 255 ? 255 : s;
}

/* ---- Smoothing buffer ---- */
#define MAX_W 64
#define MAX_H 64
static uint8_t prev_r[MAX_W * MAX_H];
static uint8_t prev_g[MAX_W * MAX_H];
static uint8_t prev_b[MAX_W * MAX_H];

EXPORT(init)
void init(void) {
    for (int i = 0; i < MAX_W * MAX_H; i++) {
        prev_r[i] = 0;
        prev_g[i] = 2;
        prev_b[i] = 5;
    }
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

    /* Multiple time offsets for different wave layers */
    int t1 = (tick_ms * speed) / 12;
    int t2 = (tick_ms * speed) / 9;
    int t3 = (tick_ms * speed) / 15;
    int t4 = (tick_ms * speed) / 7;

    int noise_scale = 512 / (scale + 1) + 3;

    for (int x = 0; x < W; x++) {
        for (int y = 0; y < H; y++) {
            int idx = x * MAX_H + y;

            /* Start with dark blue-green background */
            int r = 1, g = 3, b = 6;

            /* Layer 1: slow, large-scale noise wave */
            int nx1 = x * noise_scale + t1;
            int ny1 = y * noise_scale + (t1 >> 1);
            int n1 = value_noise2d(nx1, ny1);
            /* Sine modulation on noise for wave-like motion */
            int wave1 = isin8((y * 4 + (t1 >> 2)) & 0xFF);
            int ci1 = (n1 + wave1) >> 1;
            int r1, g1, b1;
            ocean_palette1(ci1, &r1, &g1, &b1);
            /* Layer brightness variation */
            int bri1 = isin8((t2 >> 3) & 0xFF);
            bri1 = 70 + (bri1 * 60 >> 8);
            r = sat_add(r, r1 * bri1 >> 8);
            g = sat_add(g, g1 * bri1 >> 8);
            b = sat_add(b, b1 * bri1 >> 8);

            /* Layer 2: medium speed, different scale */
            int nx2 = x * (noise_scale + 2) - t2;
            int ny2 = y * (noise_scale + 2) + (t2 >> 2);
            int n2 = value_noise2d(nx2, ny2);
            int wave2 = isin8((x * 5 + y * 3 + (t3 >> 2)) & 0xFF);
            int ci2 = (n2 + wave2) >> 1;
            int r2, g2, b2;
            ocean_palette2(ci2, &r2, &g2, &b2);
            int bri2 = isin8(((t3 >> 3) + 100) & 0xFF);
            bri2 = 40 + (bri2 * 40 >> 8);
            r = sat_add(r, r2 * bri2 >> 8);
            g = sat_add(g, g2 * bri2 >> 8);
            b = sat_add(b, b2 * bri2 >> 8);

            /* Layer 3: faster ripples */
            int nx3 = x * (noise_scale - 1) + (t3 >> 1);
            int ny3 = y * (noise_scale - 1) - t4;
            int n3 = value_noise2d(nx3, ny3);
            int r3, g3, b3;
            ocean_palette3(n3, &r3, &g3, &b3);
            int bri3 = isin8(((t1 >> 4) + 50) & 0xFF);
            bri3 = 10 + (bri3 * 28 >> 8);
            r = sat_add(r, r3 * bri3 >> 8);
            g = sat_add(g, g3 * bri3 >> 8);
            b = sat_add(b, b3 * bri3 >> 8);

            /* Layer 4: subtle sine wave overlay for shimmer */
            int shimmer = isin8((x * 7 + y * 11 + (t4 >> 2)) & 0xFF);
            shimmer = (shimmer * shimmer) >> 8;  /* bias towards peaks */
            int r4, g4, b4;
            ocean_palette3(shimmer, &r4, &g4, &b4);
            r = sat_add(r, r4 * 15 >> 8);
            g = sat_add(g, g4 * 15 >> 8);
            b = sat_add(b, b4 * 15 >> 8);

            /* Deepen blues and greens (Pacifica-style) */
            b = b * 145 >> 7;  /* boost blue ~1.13x */
            if (b > 255) b = 255;
            g = g * 200 >> 8;  /* slightly attenuate green */
            /* Ensure minimum blue tint */
            if (b < 4) b = 4;
            if (g < 2) g = 2;

            /* Add whitecap highlights where layers combine brightly */
            int avg = (r + g + b) / 3;
            int threshold = 55 + (isin8((t2 >> 4) & 0xFF) * 10 >> 8);
            if (avg > threshold) {
                int overage = avg - threshold;
                int ov2 = overage * 2;
                if (ov2 > 255) ov2 = 255;
                int ov4 = overage * 4;
                if (ov4 > 255) ov4 = 255;
                r = sat_add(r, overage);
                g = sat_add(g, ov2);
                b = sat_add(b, ov4);
            }

            /* Apply brightness */
            r = r * brightness >> 8;
            g = g * brightness >> 8;
            b = b * brightness >> 8;

            /* Temporal smoothing for fluid look */
            int pr = prev_r[idx];
            int pg = prev_g[idx];
            int pb = prev_b[idx];
            r = (pr * 128 + r * 128) >> 8;
            g = (pg * 128 + g * 128) >> 8;
            b = (pb * 128 + b * 128) >> 8;
            prev_r[idx] = (uint8_t)r;
            prev_g[idx] = (uint8_t)g;
            prev_b[idx] = (uint8_t)b;

            set_pixel(x, y, r, g, b);
        }
    }

    draw();
}
