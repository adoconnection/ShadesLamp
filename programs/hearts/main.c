#include "api.h"

/* ---- Metadata JSON ---- */
static const char META[] =
    "{\"name\":\"Hearts\","
    "\"desc\":\"White and pink hearts gently floating and spinning\","
    "\"params\":["
        "{\"id\":0,\"name\":\"Count\",\"type\":\"int\","
         "\"min\":1,\"max\":10,\"default\":5,"
         "\"desc\":\"Number of hearts\"},"
        "{\"id\":1,\"name\":\"Size\",\"type\":\"int\","
         "\"min\":2,\"max\":10,\"default\":5,"
         "\"desc\":\"Heart size in pixels\"},"
        "{\"id\":2,\"name\":\"Speed\",\"type\":\"int\","
         "\"min\":1,\"max\":100,\"default\":30,"
         "\"desc\":\"How fast they float\"},"
        "{\"id\":3,\"name\":\"Spin\",\"type\":\"int\","
         "\"min\":0,\"max\":100,\"default\":40,"
         "\"desc\":\"Rotation speed\"},"
        "{\"id\":4,\"name\":\"Brightness\",\"type\":\"int\","
         "\"min\":1,\"max\":255,\"default\":220,"
         "\"desc\":\"Overall brightness\"},"
        "{\"id\":5,\"name\":\"Pink\",\"type\":\"int\","
         "\"min\":0,\"max\":100,\"default\":60,"
         "\"desc\":\"White (0) to pink (100)\"}"
    "]}";

EXPORT(get_meta_ptr)
int get_meta_ptr(void) { return (int)META; }

EXPORT(get_meta_len)
int get_meta_len(void) { return sizeof(META) - 1; }

/* ---- PRNG (xorshift32) ---- */
static uint32_t rng = 2463534242u;
static uint32_t rng_next(void) {
    uint32_t x = rng; x ^= x << 13; x ^= x >> 17; x ^= x << 5; rng = x; return x;
}
static float rnd(void) { return (float)(rng_next() & 0xFFFF) / 65536.0f; }      /* 0..1 */
static float rnds(void) { return rnd() * 2.0f - 1.0f; }                          /* -1..1 */

/* ---- Sine/cosine (native, table index 0..255 == one full turn) ---- */
static float fsin(int a) { return m_sin((float)a * (6.28318530f / 256.0f)); }
static float fcos(int a) { return m_cos((float)a * (6.28318530f / 256.0f)); }

/* ---- Heart state ---- */
#define MAX_HEARTS 10

static float hx[MAX_HEARTS];      /* position x */
static float hy[MAX_HEARTS];      /* position y (0 = bottom) */
static float hvx[MAX_HEARTS];     /* horizontal drift */
static float hvy[MAX_HEARTS];     /* rise speed */
static float hsize[MAX_HEARTS];   /* per-heart size multiplier */
static float htheta[MAX_HEARTS];  /* rotation angle (table units 0..256) */
static float homega[MAX_HEARTS];  /* rotation speed */
static float hpink[MAX_HEARTS];   /* 0 = white, 1 = pink */
static float hsway[MAX_HEARTS];   /* sway phase */

/* ---- Framebuffer (glow + gentle trail) ---- */
#define MAX_W 64
#define MAX_H 64
static uint8_t fb_r[MAX_W * MAX_H];
static uint8_t fb_g[MAX_W * MAX_H];
static uint8_t fb_b[MAX_W * MAX_H];

static int cur_w, cur_h;
static int32_t prev_tick;

static void spawn(int i, int W, int H, int fresh_bottom) {
    hx[i] = rnd() * (float)W;
    hy[i] = fresh_bottom ? (-2.0f - rnd() * 6.0f) : (rnd() * (float)H);
    hvx[i] = rnds() * 0.5f;
    hvy[i] = 0.5f + rnd() * 0.7f;
    hsize[i] = 0.7f + rnd() * 0.6f;
    htheta[i] = rnd() * 256.0f;
    homega[i] = rnds();
    hpink[i] = 0.35f + 0.65f * rnd();   /* spread of white..pink, scaled by Pink param */
    hsway[i] = rnd() * 256.0f;
}

EXPORT(init)
void init(void) {
    prev_tick = 0;
    cur_w = get_width();
    cur_h = get_height();
    if (cur_w < 1) cur_w = 1;
    if (cur_h < 1) cur_h = 1;
    for (int i = 0; i < MAX_W * MAX_H; i++) { fb_r[i] = fb_g[i] = fb_b[i] = 0; }
    for (int i = 0; i < MAX_HEARTS; i++) spawn(i, cur_w, cur_h, 0);
}

/* inside test for the classic heart curve, unit space */
static int heart_inside(float x, float y) {
    float a = x * x + y * y - 1.0f;
    float f = a * a * a - x * x * y * y * y;
    return f <= 0.0f;
}

EXPORT(update)
void update(int tick_ms) {
    int count  = get_param_i32(0);
    int size_p = get_param_i32(1);
    int speed  = get_param_i32(2);
    int spin   = get_param_i32(3);
    int bright = get_param_i32(4);
    int pink_p = get_param_i32(5);

    int W = get_width();
    int H = get_height();
    if (W < 1) W = 1;
    if (H < 1) H = 1;
    if (W > MAX_W) W = MAX_W;
    if (H > MAX_H) H = MAX_H;
    cur_w = W; cur_h = H;
    if (count < 1) count = 1;
    if (count > MAX_HEARTS) count = MAX_HEARTS;
    if (size_p < 2) size_p = 2;

    rng ^= (uint32_t)tick_ms;

    int32_t delta = tick_ms - prev_tick;
    if (delta <= 0 || delta > 200) delta = 33;
    prev_tick = tick_ms;
    float dt = (float)delta / 1000.0f;

    float spd   = (float)speed / 30.0f;       /* normalize: 30 -> 1.0 */
    float spinr = (float)spin / 100.0f;
    float pinkamt = (float)pink_p / 100.0f;

    /* gentle trail: dim the framebuffer a little each frame */
    for (int i = 0; i < W * H; i++) {
        fb_r[i] = (uint8_t)((int)fb_r[i] * 70 / 256);
        fb_g[i] = (uint8_t)((int)fb_g[i] * 70 / 256);
        fb_b[i] = (uint8_t)((int)fb_b[i] * 70 / 256);
    }

    for (int i = 0; i < count; i++) {
        /* motion */
        hsway[i] += dt * 1.5f * spd;
        hy[i] += hvy[i] * spd * dt * 6.0f;
        hx[i] += (hvx[i] + fsin((int)hsway[i]) * 0.6f) * spd * dt * 6.0f;
        htheta[i] += homega[i] * spinr * dt * 90.0f;

        /* wrap horizontally (cylinder) */
        while (hx[i] < 0.0f)        hx[i] += (float)W;
        while (hx[i] >= (float)W)   hx[i] -= (float)W;

        /* respawn once floated past the top.
           scale so the heart is roughly `size_p` pixels tall (~2.8 units high) */
        float s = (float)size_p * hsize[i] * 0.42f;
        if (hy[i] - s * 1.5f > (float)H + 2.0f) spawn(i, W, H, 1);

        /* colour: lerp white -> pink, scaled by the Pink param */
        float pk = hpink[i] * pinkamt;
        int cr = bright;                                   /* pink and white share high red */
        int cg = (int)((float)bright * (1.0f - 0.70f * pk));
        int cb = (int)((float)bright * (1.0f - 0.36f * pk));

        float c = fcos((int)htheta[i]);
        float sn = fsin((int)htheta[i]);
        float inv_s = 1.0f / s;

        float cx = hx[i], cy = hy[i];
        int half = (int)(s * 1.4f + 1.0f);
        int x0 = (int)(cx) - half, x1 = (int)(cx) + half;
        int y0 = (int)(cy) - half, y1 = (int)(cy) + half;
        if (y0 < 0) y0 = 0;
        if (y1 >= H) y1 = H - 1;

        for (int py = y0; py <= y1; py++) {
            for (int px = x0; px <= x1; px++) {
                /* 2x2 supersample for smooth edges */
                int hits = 0;
                for (int sxi = 0; sxi < 2; sxi++) {
                    for (int syi = 0; syi < 2; syi++) {
                        float sx = (float)px + (sxi ? 0.25f : -0.25f) - cx;
                        float sy = (float)py + (syi ? 0.25f : -0.25f) - cy;
                        /* rotate into heart-local space */
                        float rx = (sx * c + sy * sn) * inv_s;
                        float ry = (-sx * sn + sy * c) * inv_s;
                        if (heart_inside(rx, ry)) hits++;
                    }
                }
                if (!hits) continue;
                float cov = (float)hits * 0.25f;

                int wx = px % W; if (wx < 0) wx += W;
                int idx = py * W + wx;
                int nr = (int)fb_r[idx] + (int)(cr * cov);
                int ng = (int)fb_g[idx] + (int)(cg * cov);
                int nb = (int)fb_b[idx] + (int)(cb * cov);
                if (nr > 255) nr = 255;
                if (ng > 255) ng = 255;
                if (nb > 255) nb = 255;
                fb_r[idx] = (uint8_t)nr;
                fb_g[idx] = (uint8_t)ng;
                fb_b[idx] = (uint8_t)nb;
            }
        }
    }

    /* present */
    for (int x = 0; x < W; x++)
        for (int y = 0; y < H; y++) {
            int idx = y * W + x;
            set_pixel(x, y, fb_r[idx], fb_g[idx], fb_b[idx]);
        }

    draw();
}
