#include "api.h"

/*
 * Flame Particle — порт эффекта "Пламя" из GyverLamp (SottNick)
 * + слой искр (embers) — яркие быстрые частицы, вылетающие вверх из пламени
 */

/* ---- Metadata ---- */
static const char META[] =
    "{\"name\":\"Flame Particle\","
    "\"desc\":\"Particle flame with sparks and Wu antialiasing\","
    "\"params\":["
        "{\"id\":0,\"name\":\"Hue\",\"type\":\"int\","
         "\"min\":0,\"max\":255,\"default\":30,"
         "\"desc\":\"Base flame hue (0=red, 30=orange, 60=yellow)\"},"
        "{\"id\":1,\"name\":\"Intensity\",\"type\":\"int\","
         "\"min\":1,\"max\":100,\"default\":50,"
         "\"desc\":\"Flame tongue density (%)\"},"
        "{\"id\":2,\"name\":\"Sparks\",\"type\":\"int\","
         "\"min\":0,\"max\":30,\"default\":8,"
         "\"desc\":\"Number of flying sparks\"},"
        "{\"id\":3,\"name\":\"Brightness\",\"type\":\"int\","
         "\"min\":1,\"max\":255,\"default\":220,"
         "\"desc\":\"Overall brightness\"}"
    "]}";

EXPORT(get_meta_ptr)
int get_meta_ptr(void) { return (int)META; }
EXPORT(get_meta_len)
int get_meta_len(void) { return sizeof(META) - 1; }

/* ---- PRNG (xorshift32) ---- */
static uint32_t rng = 91735;
static uint32_t rng_next(void) {
    uint32_t x = rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng = x;
    return x;
}
static int rand8(void) { return (int)(rng_next() & 0xFF); }
static int rand_range(int lo, int hi) {
    if (lo >= hi) return lo;
    return lo + (int)(rng_next() % (uint32_t)(hi - lo));
}

/* ---- HSV → RGB ---- */
static void hsv2rgb(int hue, int sat, int val, int *r, int *g, int *b) {
    if (val == 0) { *r = *g = *b = 0; return; }
    if (sat == 0) { *r = *g = *b = val; return; }

    int h = hue;
    while (h < 0) h += 256;
    h = h & 0xFF;

    int region = h / 43;
    int frac = (h - region * 43) * 6;

    int p = (val * (255 - sat)) >> 8;
    int q = (val * (255 - ((sat * frac) >> 8))) >> 8;
    int t = (val * (255 - ((sat * (255 - frac)) >> 8))) >> 8;

    switch (region) {
        case 0:  *r = val; *g = t;   *b = p;   break;
        case 1:  *r = q;   *g = val; *b = p;   break;
        case 2:  *r = p;   *g = val; *b = t;   break;
        case 3:  *r = p;   *g = q;   *b = val; break;
        case 4:  *r = t;   *g = p;   *b = val; break;
        default: *r = val; *g = p;   *b = q;   break;
    }
}

/* ---- Flame tongue constants ---- */
#define MAX_PARTICLES 100
#define FLAME_MIN_DY  128
#define FLAME_MAX_DY  256
#define FLAME_MAX_DX   32
#define FLAME_MIN_DX  (-FLAME_MAX_DX)
#define FLAME_MIN_VALUE 176
#define FLAME_MAX_VALUE 255

/* ---- Flame tongue state ---- */
static float   posX[MAX_PARTICLES];
static float   posY[MAX_PARTICLES];
static float   spdX[MAX_PARTICLES];
static float   spdY[MAX_PARTICLES];
static uint8_t bri[MAX_PARTICLES];
static uint8_t ttl[MAX_PARTICLES];
static uint8_t hueShift[MAX_PARTICLES];

/* ---- Spark (ember) state ---- */
#define MAX_SPARKS 30
static float   spkX[MAX_SPARKS];
static float   spkY[MAX_SPARKS];
static float   spkDX[MAX_SPARKS];
static float   spkDY[MAX_SPARKS];
static uint8_t spkBri[MAX_SPARKS];
static uint8_t spkTTL[MAX_SPARKS];
static uint8_t spkHue[MAX_SPARKS];

/* ---- Frame buffers ---- */
#define MAX_W 64
#define MAX_H 64
static uint8_t fb_hue[MAX_W * MAX_H];
static uint8_t fb_sat[MAX_W * MAX_H];
static uint8_t fb_val[MAX_W * MAX_H];

#define FB(x,y) ((x) * cur_h + (y))

static int cur_w, cur_h;

/* ---- Wu antialiased draw (maxV strategy) ---- */
static void wu_draw(float fx, float fy, uint8_t bright, uint8_t hue_val) {
    int ix0 = (int)fx;
    int iy0 = (int)fy;
    if (fx < 0.0f) ix0--;
    if (fy < 0.0f) iy0--;

    int xx = (int)((fx - ix0) * 255.0f);
    int yy = (int)((fy - iy0) * 255.0f);
    int ixx = 255 - xx;
    int iyy = 255 - yy;

    int wu[4] = {
        (ixx * iyy + ixx + iyy) >> 8,
        (xx  * iyy + xx  + iyy) >> 8,
        (ixx * yy  + ixx + yy)  >> 8,
        (xx  * yy  + xx  + yy)  >> 8
    };

    for (int i = 0; i < 4; i++) {
        int px = ix0 + (i & 1);
        int py = iy0 + ((i >> 1) & 1);

        while (px < 0) px += cur_w;
        while (px >= cur_w) px -= cur_w;
        if (py < 0 || py >= cur_h) continue;

        int weighted = (int)bright * wu[i] >> 8;
        int fi = FB(px, py);

        if (weighted >= fb_val[fi]) {
            fb_hue[fi] = hue_val;
            fb_sat[fi] = 255;
            fb_val[fi] = (uint8_t)weighted;
        }
    }
}

/* ---- Draw spark as additive bright point ---- */
static void spark_draw(float fx, float fy, uint8_t bright, uint8_t hue_val, int global_bri) {
    int ix0 = (int)fx;
    int iy0 = (int)fy;
    if (fx < 0.0f) ix0--;
    if (fy < 0.0f) iy0--;

    int xx = (int)((fx - ix0) * 255.0f);
    int yy = (int)((fy - iy0) * 255.0f);
    int ixx = 255 - xx;
    int iyy = 255 - yy;

    int wu[4] = {
        (ixx * iyy + ixx + iyy) >> 8,
        (xx  * iyy + xx  + iyy) >> 8,
        (ixx * yy  + ixx + yy)  >> 8,
        (xx  * yy  + xx  + yy)  >> 8
    };

    for (int i = 0; i < 4; i++) {
        int px = ix0 + (i & 1);
        int py = iy0 + ((i >> 1) & 1);

        while (px < 0) px += cur_w;
        while (px >= cur_w) px -= cur_w;
        if (py < 0 || py >= cur_h) continue;

        int weighted = (int)bright * wu[i] >> 8;
        int v = weighted * global_bri / 255;
        if (v < 1) continue;

        /* Sparks rendered directly — additive over the flame */
        int r, g, b;
        /* Sparks are mostly yellow-white: low saturation, high value */
        int sat = 180 - weighted / 2;
        if (sat < 50) sat = 50;
        hsv2rgb(hue_val, sat, v, &r, &g, &b);

        /* Read current pixel, add spark on top */
        int fi = FB(px, py);
        int cv = (int)fb_val[fi] * global_bri / 255;
        if (cv > 0) {
            int cr, cg, cb;
            hsv2rgb(fb_hue[fi], fb_sat[fi], cv, &cr, &cg, &cb);
            r += cr; if (r > 255) r = 255;
            g += cg; if (g > 255) g = 255;
            b += cb; if (b > 255) b = 255;
        }

        set_pixel(px, py, r, g, b);
    }
}

EXPORT(init)
void init(void) {
    for (int i = 0; i < MAX_W * MAX_H; i++) {
        fb_hue[i] = 0; fb_sat[i] = 0; fb_val[i] = 0;
    }
    for (int i = 0; i < MAX_PARTICLES; i++) {
        ttl[i] = 0; bri[i] = 0;
    }
    for (int i = 0; i < MAX_SPARKS; i++) {
        spkTTL[i] = 0; spkBri[i] = 0;
    }
    rng = 48271;
}

EXPORT(update)
void update(int tick_ms) {
    int base_hue   = get_param_i32(0);
    int intensity  = get_param_i32(1);
    int num_sparks = get_param_i32(2);
    int bright     = get_param_i32(3);

    cur_w = get_width();
    cur_h = get_height();
    if (cur_w > MAX_W) cur_w = MAX_W;
    if (cur_h > MAX_H) cur_h = MAX_H;
    if (cur_w < 1) cur_w = 1;
    if (cur_h < 1) cur_h = 1;

    rng ^= (uint32_t)tick_ms;

    int num_particles = (int)((float)cur_w * 2.4f * intensity / 100.0f);
    if (num_particles < 2) num_particles = 2;
    if (num_particles > MAX_PARTICLES) num_particles = MAX_PARTICLES;

    if (num_sparks > MAX_SPARKS) num_sparks = MAX_SPARKS;

    int min_ttl = cur_h / 4;
    if (min_ttl < 3) min_ttl = 3;
    int max_ttl = cur_h;
    if (max_ttl < min_ttl + 2) max_ttl = min_ttl + 2;
    if (max_ttl > 40) max_ttl = 40;

    /* ======== STEP 1: Fade previous frame (×237/256 ≈ 93%) ======== */
    for (int x = 0; x < cur_w; x++)
        for (int y = 0; y < cur_h; y++)
            fb_val[FB(x, y)] = (uint8_t)((int)fb_val[FB(x, y)] * 237 >> 8);

    /* ======== STEP 2: Update flame tongues ======== */
    for (int i = 0; i < num_particles; i++) {
        if (ttl[i] > 0) {
            wu_draw(posX[i], posY[i], bri[i], hueShift[i]);

            uint8_t prev = ttl[i];
            ttl[i]--;

            posX[i] += spdX[i];
            posY[i] += spdY[i];

            bri[i] = (uint8_t)((int)bri[i] * ttl[i] / prev);

            if (posY[i] >= cur_h || bri[i] < 2)
                ttl[i] = 0;

            if (posX[i] < 0.0f) posX[i] += (float)cur_w;
            else if (posX[i] >= (float)cur_w) posX[i] -= (float)cur_w;
        } else {
            ttl[i] = (uint8_t)rand_range(min_ttl, max_ttl);
            hueShift[i] = (uint8_t)(254 + base_hue + rand8() % 20);
            posX[i] = (float)(rng_next() % (uint32_t)(cur_w * 255)) / 255.0f;
            posY[i] = -0.9f;
            spdX[i] = (float)(FLAME_MIN_DX + (int)(rng_next() % (uint32_t)(FLAME_MAX_DX - FLAME_MIN_DX))) / 256.0f;
            spdY[i] = (float)(FLAME_MIN_DY + (int)(rng_next() % (uint32_t)(FLAME_MAX_DY - FLAME_MIN_DY))) / 256.0f;
            bri[i] = (uint8_t)rand_range(FLAME_MIN_VALUE, FLAME_MAX_VALUE + 1);
        }
    }

    /* ======== STEP 3: Update sparks (embers) ======== */
    for (int i = 0; i < num_sparks; i++) {
        if (spkTTL[i] > 0) {
            spkTTL[i]--;

            spkX[i] += spkDX[i];
            spkY[i] += spkDY[i];

            /* Gravity: sparks decelerate then fall */
            spkDY[i] -= 0.02f;

            /* Fade */
            int newbri = (int)spkBri[i] - 6;
            if (newbri < 0) newbri = 0;
            spkBri[i] = (uint8_t)newbri;

            if (spkY[i] < 0 || spkY[i] >= cur_h || spkBri[i] < 3)
                spkTTL[i] = 0;

            /* Horizontal wrap */
            if (spkX[i] < 0.0f) spkX[i] += (float)cur_w;
            else if (spkX[i] >= (float)cur_w) spkX[i] -= (float)cur_w;
        } else {
            /* Spawn new spark from lower third of matrix */
            spkTTL[i] = (uint8_t)rand_range(15, 45);
            spkX[i] = (float)rand_range(0, cur_w * 256) / 256.0f;
            spkY[i] = (float)rand_range(cur_h / 6, cur_h / 2);
            /* Fast upward, slight horizontal drift */
            spkDX[i] = ((float)rand_range(-40, 41)) / 256.0f;
            spkDY[i] = ((float)rand_range(200, 500)) / 256.0f;
            spkBri[i] = (uint8_t)rand_range(200, 255);
            spkHue[i] = (uint8_t)(base_hue + 15 + rand8() % 20); /* slightly yellower than flame */
        }
    }

    /* ======== STEP 4: Render flame from framebuffer ======== */
    for (int x = 0; x < cur_w; x++) {
        for (int y = 0; y < cur_h; y++) {
            int fi = FB(x, y);
            int v = (int)fb_val[fi] * bright / 255;
            if (v < 1) {
                set_pixel(x, y, 0, 0, 0);
            } else {
                int r, g, b;
                hsv2rgb(fb_hue[fi], fb_sat[fi], v, &r, &g, &b);
                set_pixel(x, y, r, g, b);
            }
        }
    }

    /* ======== STEP 5: Render sparks on top (additive) ======== */
    for (int i = 0; i < num_sparks; i++) {
        if (spkTTL[i] > 0 && spkBri[i] > 2) {
            spark_draw(spkX[i], spkY[i], spkBri[i], spkHue[i], bright);
        }
    }

    draw();
}
