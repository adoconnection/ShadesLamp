#include "api.h"

/*
 * Electrons — fast particles orbiting a nucleus.
 * All particles orbit in the same direction with slight orbital tilts,
 * clustered around the equator. Adjustable trail length.
 */

static const char META[] =
    "{\"name\":\"Electrons\","
    "\"desc\":\"Fast electrons orbiting around a nucleus\","
    "\"params\":["
        "{\"id\":0,\"name\":\"Hue\",\"type\":\"int\","
         "\"min\":0,\"max\":255,\"default\":25,"
         "\"desc\":\"Base hue (0=red, 25=fire-orange)\"},"
        "{\"id\":1,\"name\":\"Electrons\",\"type\":\"int\","
         "\"min\":1,\"max\":40,\"default\":20,"
         "\"desc\":\"Number of electrons\"},"
        "{\"id\":2,\"name\":\"Speed\",\"type\":\"int\","
         "\"min\":1,\"max\":600,\"default\":150,"
         "\"desc\":\"Orbital speed\"},"
        "{\"id\":3,\"name\":\"Trail\",\"type\":\"int\","
         "\"min\":1,\"max\":50,\"default\":20,"
         "\"desc\":\"Trail length (less=shorter)\"},"
        "{\"id\":4,\"name\":\"Brightness\",\"type\":\"int\","
         "\"min\":1,\"max\":255,\"default\":230,"
         "\"desc\":\"Overall brightness\"},"
        "{\"id\":5,\"name\":\"White Head\",\"type\":\"int\","
         "\"min\":0,\"max\":1,\"default\":1,"
         "\"options\":[\"Off\",\"On\"],"
         "\"desc\":\"White electron head\"},"
        "{\"id\":6,\"name\":\"Spread\",\"type\":\"int\","
         "\"min\":1,\"max\":100,\"default\":25,"
         "\"desc\":\"Vertical spread (% of height)\"}"
    "]}";

EXPORT(get_meta_ptr)
int get_meta_ptr(void) { return (int)META; }
EXPORT(get_meta_len)
int get_meta_len(void) { return sizeof(META) - 1; }

/* ---- PRNG ---- */
static uint32_t rng = 77317;
static uint32_t rng_next(void) {
    uint32_t x = rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    rng = x; return x;
}

/* ---- HSV -> RGB ---- */
static void hsv2rgb(int hue, int sat, int val, int *r, int *g, int *b) {
    if (val == 0) { *r = *g = *b = 0; return; }
    if (sat == 0) { *r = *g = *b = val; return; }
    int h = hue & 0xFF;
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

/* ---- Sine (Bhaskara I) ---- */
#define TWO_PI  6.28318530f
#define PI      3.14159265f

static float fsin(float x) {
    while (x < 0.0f) x += TWO_PI;
    while (x >= TWO_PI) x -= TWO_PI;
    float sign = 1.0f;
    if (x > PI) { x -= PI; sign = -1.0f; }
    float num = 16.0f * x * (PI - x);
    float den = 5.0f * PI * PI - 4.0f * x * (PI - x);
    if (den == 0.0f) return 0.0f;
    return sign * num / den;
}

/* ---- Particles ---- */
#define MAX_P 40

static float p_angle[MAX_P];   /* orbital angle */
static float p_incl[MAX_P];    /* vertical amplitude fraction: -1.0 to +1.0 */
static float p_speed[MAX_P];   /* individual speed mult */
static uint8_t p_hue[MAX_P];   /* hue offset */

/* ---- Framebuffer ---- */
#define MAX_W 64
#define MAX_H 64
static uint8_t fb_hue[MAX_W * MAX_H];
static uint8_t fb_val[MAX_W * MAX_H];

/* Head positions for white overlay */
static int head_x[MAX_P];
static int head_y[MAX_P];

static int cur_w, cur_h;
#define FB(x,y) ((x) * cur_h + (y))

/* ---- Draw single pixel with max-V blend ---- */
static void plot(int px, int py, uint8_t bright, uint8_t hue_val) {
    while (px < 0) px += cur_w;
    while (px >= cur_w) px -= cur_w;
    if (py < 0 || py >= cur_h) return;
    int fi = FB(px, py);
    if (bright > fb_val[fi]) {
        fb_hue[fi] = hue_val;
        fb_val[fi] = bright;
    }
}

EXPORT(init)
void init(void) {
    for (int i = 0; i < MAX_W * MAX_H; i++) {
        fb_hue[i] = 0; fb_val[i] = 0;
    }
    rng = 77317;

    for (int i = 0; i < MAX_P; i++) {
        /* Spread starting angles evenly + jitter */
        p_angle[i] = (float)i / (float)MAX_P * TWO_PI
                   + ((float)(rng_next() % 1000) / 1000.0f - 0.5f) * 0.5f;

        /* Vertical amplitude fraction: evenly distributed -1.0 to +1.0 */
        p_incl[i] = ((float)(rng_next() % 10000) / 10000.0f) * 2.0f - 1.0f;

        /* Speed: 0.6x to 1.5x of base — all positive (same direction) */
        p_speed[i] = 0.6f + (float)(rng_next() % 10000) / 11111.0f;

        /* Hue offset: 0-20 */
        p_hue[i] = (uint8_t)(rng_next() % 20);
    }
}

EXPORT(update)
void update(int tick_ms) {
    int base_hue   = get_param_i32(0);
    int num_p      = get_param_i32(1);
    int speed      = get_param_i32(2);
    int trail      = get_param_i32(3);
    int bright     = get_param_i32(4);
    int white_head = get_param_i32(5);
    int spread     = get_param_i32(6);

    cur_w = get_width();
    cur_h = get_height();
    if (cur_w > MAX_W) cur_w = MAX_W;
    if (cur_h > MAX_H) cur_h = MAX_H;
    if (cur_w < 1) cur_w = 1;
    if (cur_h < 1) cur_h = 1;
    if (num_p > MAX_P) num_p = MAX_P;

    float equator = (float)cur_h / 2.0f;
    /* Spread controls max vertical amplitude: 1%=tight, 100%=full height */
    float max_amp = (equator - 1.0f) * (float)spread / 100.0f;
    if (max_amp < 0.5f) max_amp = 0.5f;

    float base_speed = (float)speed * 0.0007f;

    /* Trail: trail=1 -> very short (fast fade), trail=50 -> very long (slow fade) */
    /* fade = 256 - (51-trail)*4: trail=1->56/256, trail=50->252/256 */
    int fade = 256 - (51 - trail) * 4;
    if (fade < 40) fade = 40;
    if (fade > 253) fade = 253;

    rng ^= (uint32_t)tick_ms;

    /* Fade framebuffer */
    for (int i = 0; i < cur_w * cur_h; i++)
        fb_val[FB(i / cur_h, i % cur_h)] = (uint8_t)((int)fb_val[FB(i / cur_h, i % cur_h)] * fade >> 8);

    /* Update and draw electrons */
    for (int i = 0; i < num_p; i++) {
        p_angle[i] += base_speed * p_speed[i];
        if (p_angle[i] >= TWO_PI) p_angle[i] -= TWO_PI;

        /* X: linear around cylinder */
        float fx = p_angle[i] / TWO_PI * (float)cur_w;
        /* Y: oscillation around equator, amplitude = max_amp * incl fraction */
        float fy = equator + fsin(p_angle[i]) * max_amp * p_incl[i];

        if (fy < 0.0f) fy = 0.0f;
        if (fy >= (float)cur_h - 0.01f) fy = (float)cur_h - 0.01f;

        int px = (int)(fx + 0.5f);
        int py = (int)(fy + 0.5f);
        if (py >= cur_h) py = cur_h - 1;

        uint8_t hue = (uint8_t)(base_hue + p_hue[i]);
        plot(px, py, 255, hue);

        /* Remember head position for white overlay */
        head_x[i] = px;
        head_y[i] = py;
    }

    /* Render trails (colored) */
    for (int x = 0; x < cur_w; x++) {
        for (int y = 0; y < cur_h; y++) {
            int fi = FB(x, y);
            int v = (int)fb_val[fi] * bright / 255;
            if (v < 1) {
                set_pixel(x, y, 0, 0, 0);
            } else {
                int r, g, b;
                hsv2rgb(fb_hue[fi], 255, v, &r, &g, &b);
                set_pixel(x, y, r, g, b);
            }
        }
    }

    /* White electron heads on top (if enabled) */
    if (white_head) {
        for (int i = 0; i < num_p; i++) {
            int px = head_x[i];
            int py = head_y[i];
            while (px < 0) px += cur_w;
            while (px >= cur_w) px -= cur_w;
            if (py >= 0 && py < cur_h) {
                set_pixel(px, py, bright, bright, bright);
            }
        }
    }

    draw();
}
