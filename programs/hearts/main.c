#include "api.h"

/* ---- Metadata JSON ---- */
static const char META[] =
    "{\"name\":\"Shapes\","
    "\"desc\":\"Hearts, stars or squares gently floating and spinning\","
    "\"params\":["
        "{\"id\":6,\"name\":\"Shape\",\"type\":\"select\","
         "\"options\":[\"Hearts\",\"Stars\",\"Squares\"],"
         "\"default\":0,"
         "\"desc\":\"Which shape floats\"},"
        "{\"id\":0,\"name\":\"Count\",\"type\":\"int\","
         "\"min\":1,\"max\":10,\"default\":5,"
         "\"desc\":\"Number of shapes\"},"
        "{\"id\":1,\"name\":\"Size\",\"type\":\"int\","
         "\"min\":2,\"max\":10,\"default\":5,"
         "\"desc\":\"Shape size in pixels\"},"
        "{\"id\":2,\"name\":\"Speed\",\"type\":\"int\","
         "\"min\":1,\"max\":100,\"default\":30,"
         "\"desc\":\"How fast they float\"},"
        "{\"id\":3,\"name\":\"Spin\",\"type\":\"int\","
         "\"min\":0,\"max\":100,\"default\":40,"
         "\"desc\":\"Rotation speed\"},"
        "{\"id\":4,\"name\":\"Brightness\",\"type\":\"int\","
         "\"min\":1,\"max\":255,\"default\":220,"
         "\"desc\":\"Overall brightness\"},"
        "{\"id\":7,\"name\":\"Palette\",\"type\":\"select\","
         "\"options\":[\"White-Pink\",\"Green\",\"UV Neon\",\"Rainbow\",\"Yellow-Red-Green\",\"Magenta-Cyan\"],"
         "\"default\":0,"
         "\"desc\":\"Colour scheme (laser palettes)\"},"
        "{\"id\":5,\"name\":\"Pink\",\"type\":\"int\","
         "\"min\":0,\"max\":100,\"default\":60,"
         "\"desc\":\"White (0) to pink (100), for the White-Pink palette\"}"
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

/* ---- Shape state ---- */
#define MAX_SHAPES 10

static float hx[MAX_SHAPES];      /* position x */
static float hy[MAX_SHAPES];      /* position y (0 = bottom) */
static float hvx[MAX_SHAPES];     /* horizontal drift */
static float hvy[MAX_SHAPES];     /* rise speed */
static float hsize[MAX_SHAPES];   /* per-shape size multiplier */
static float htheta[MAX_SHAPES];  /* rotation angle (table units 0..256) */
static float homega[MAX_SHAPES];  /* rotation speed */
static float hpink[MAX_SHAPES];   /* 0 = white, 1 = pink */
static float hsway[MAX_SHAPES];   /* sway phase */

/* ---- Framebuffer (glow + gentle trail), RGB row-major ---- */
#define MAX_W 64
#define MAX_H 64
static uint8_t FB[MAX_W * MAX_H * 3];
EXPORT(get_framebuffer)
int get_framebuffer(void) { return (int)FB; }

/* ---- Shape masks, sampled with 2x2 supersampling at render time.
 * Built once in init() from analytic tests over unit space [-1.4, 1.4]
 * (y up). A mask lookup per sample is cheaper than any inside-test and
 * makes all shapes cost the same. MRES=64 => ~0.044 units per cell —
 * far finer than one screen pixel at every allowed Size. ---- */
#define SHAPE_HEART  0
#define SHAPE_STAR   1
#define SHAPE_SQUARE 2
#define MRES 64
static uint8_t MASK[3][MRES * MRES];
/* unit height of each shape, to keep the Size param meaning "pixels tall" */
static const float SHAPE_SCALE[3] = { 0.42f, 0.385f, 0.53f };

/* Pronounced heart: two round lobes + a sharp V wedge (not the mushy
 * implicit curve). Unit space, y up, notch at the top, point at (0,-1.35). */
static int heart_inside(float x, float y) {
    float ax = x < 0.0f ? -x : x;
    float dx = ax - 0.55f, dy = y - 0.45f;
    if (dx * dx + dy * dy <= 0.58f * 0.58f) return 1;            /* lobes */
    if (y <= 0.45f && y >= -1.35f &&
        ax <= 1.03f * (y + 1.35f) / 1.80f) return 1;             /* wedge */
    return 0;
}

/* 5-pointed star as a 10-vertex polygon, even-odd ray cast. init-only. */
static float star_vx[10], star_vy[10];
static int star_inside(float x, float y) {
    int in = 0;
    for (int i = 0, j = 9; i < 10; j = i++) {
        if ((star_vy[i] > y) != (star_vy[j] > y) &&
            x < (star_vx[j] - star_vx[i]) * (y - star_vy[i]) /
                (star_vy[j] - star_vy[i]) + star_vx[i]) {
            in = !in;
        }
    }
    return in;
}

static int square_inside(float x, float y) {
    float ax = x < 0.0f ? -x : x;
    float ay = y < 0.0f ? -y : y;
    return ax <= 0.95f && ay <= 0.95f;
}

static void build_masks(void) {
    for (int k = 0; k < 10; k++) {  /* outer/inner vertices, point up */
        float ang = (90.0f + (float)k * 36.0f) * (6.28318530f / 360.0f);
        float r = (k & 1) ? 0.55f : 1.30f;
        star_vx[k] = m_cos(ang) * r;
        star_vy[k] = m_sin(ang) * r;
    }
    for (int my = 0; my < MRES; my++) {
        float uy = ((float)my + 0.5f) * (2.8f / (float)MRES) - 1.4f;
        for (int mx = 0; mx < MRES; mx++) {
            float ux = ((float)mx + 0.5f) * (2.8f / (float)MRES) - 1.4f;
            int idx = my * MRES + mx;
            MASK[SHAPE_HEART][idx]  = (uint8_t)heart_inside(ux, uy);
            MASK[SHAPE_STAR][idx]   = (uint8_t)star_inside(ux, uy);
            MASK[SHAPE_SQUARE][idx] = (uint8_t)square_inside(ux, uy);
        }
    }
}

static int cur_w, cur_h;
static int32_t prev_tick;

static void spawn(int i, int W, int H, int fresh_bottom) {
    hx[i] = rnd() * (float)W;
    hy[i] = fresh_bottom ? (-2.0f - rnd() * 6.0f) : (rnd() * (float)H);
    hvx[i] = rnds() * 0.5f;
    hvy[i] = 0.5f + rnd() * 0.7f;
    hsize[i] = 0.7f + rnd() * 0.6f;
    /* near-upright spawn (±~28°): a heart at a random angle stops reading
     * as a heart the moment Spin is low; Spin still rotates it freely */
    htheta[i] = rnds() * 20.0f;
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
    for (uint32_t i = 0; i < sizeof(FB); i++) FB[i] = 0;
    build_masks();
    for (int i = 0; i < MAX_SHAPES; i++) spawn(i, cur_w, cur_h, 0);
}

EXPORT(update)
void update(int tick_ms) {
    int count  = get_param_i32(0);
    int size_p = get_param_i32(1);
    int speed  = get_param_i32(2);
    int spin   = get_param_i32(3);
    int bright = get_param_i32(4);
    int pink_p = get_param_i32(5);
    int shape  = get_param_i32(6);
    int palm   = get_param_i32(7);
    if (palm < 0 || palm > 5) palm = 0;

    int W = get_width();
    int H = get_height();
    if (W < 1) W = 1;
    if (H < 1) H = 1;
    if (W > MAX_W) W = MAX_W;
    if (H > MAX_H) H = MAX_H;
    cur_w = W; cur_h = H;
    if (count < 1) count = 1;
    if (count > MAX_SHAPES) count = MAX_SHAPES;
    if (size_p < 2) size_p = 2;
    if (shape < 0 || shape > 2) shape = 0;

    rng ^= (uint32_t)tick_ms;

    int32_t delta = tick_ms - prev_tick;
    if (delta <= 0 || delta > 200) delta = 33;
    prev_tick = tick_ms;
    float dt = (float)delta / 1000.0f;

    float spd   = (float)speed / 30.0f;       /* normalize: 30 -> 1.0 */
    float spinr = (float)spin / 100.0f;
    float pinkamt = (float)pink_p / 100.0f;

    /* gentle trail: dim the framebuffer a little each frame (native batch) */
    m_fade(FB, W * H * 3, 70);

    const uint8_t* mask = MASK[shape];
    const float mscale = (float)MRES / 2.8f;

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
           scale so the shape is roughly `size_p` pixels tall */
        float s = (float)size_p * hsize[i] * SHAPE_SCALE[shape];
        if (hy[i] - s * 1.5f > (float)H + 2.0f) spawn(i, W, H, 1);

        /* colour: default is the classic white->pink lerp; the rest are the
         * laser_sky palettes (pure saturated hues, some drifting with time) */
        int cr, cg, cb;
        if (palm == 0) {
            float pk = hpink[i] * pinkamt;
            cr = bright;                                   /* pink and white share high red */
            cg = (int)((float)bright * (1.0f - 0.70f * pk));
            cb = (int)((float)bright * (1.0f - 0.36f * pk));
        } else {
            float tsec = (float)tick_ms * 0.001f;
            int hue;
            switch (palm) {
                case 2:  hue = 96 + (i * 37 + (int)(tsec * 15.0f)) % 150; break; /* UV neon */
                case 3:  hue = (i * 40 + (int)(tsec * 30.0f)) & 255; break;      /* rainbow */
                case 4:  { int k = i % 3; hue = (k == 0) ? 42 : (k == 1) ? 0 : 85; } break;
                case 5:  hue = (i & 1) ? 128 : 213; break;                       /* magenta-cyan */
                default: hue = 92; break;                                        /* green */
            }
            int col = m_hsv(hue & 255, 255, bright);
            cr = (col >> 16) & 255; cg = (col >> 8) & 255; cb = col & 255;
        }

        float c = fcos((int)htheta[i]);
        float sn = fsin((int)htheta[i]);
        float inv_s = 1.0f / s;

        float cx = hx[i], cy = hy[i];
        int half = (int)(s * 1.5f + 1.0f);
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
                        /* rotate into shape-local space */
                        float rx = (sx * c + sy * sn) * inv_s;
                        float ry = (-sx * sn + sy * c) * inv_s;
                        int mx = (int)((rx + 1.4f) * mscale);
                        int my = (int)((ry + 1.4f) * mscale);
                        if (mx < 0 || mx >= MRES || my < 0 || my >= MRES) continue;
                        hits += mask[my * MRES + mx];
                    }
                }
                if (!hits) continue;
                float cov = (float)hits * 0.25f;

                int wx = px % W; if (wx < 0) wx += W;
                uint8_t* p = FB + ((uint32_t)py * W + wx) * 3;
                int nr = (int)p[0] + (int)(cr * cov);
                int ng = (int)p[1] + (int)(cg * cov);
                int nb = (int)p[2] + (int)(cb * cov);
                if (nr > 255) nr = 255;
                if (ng > 255) ng = 255;
                if (nb > 255) nb = 255;
                p[0] = (uint8_t)nr;
                p[1] = (uint8_t)ng;
                p[2] = (uint8_t)nb;
            }
        }
    }

    draw();
}
