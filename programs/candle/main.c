#include "api.h"

/*
 * Candle — warm, living candlelight.
 *
 * Unlike "Flame" (a full Fire2012 wall of fire), this is a single calm
 * candle: a hot pool of light at the base that fades upward into a soft
 * teardrop glow, breathing with organic flicker and the occasional draft
 * (gust) that makes the whole flame dip and recover. A gentle "dance"
 * shimmer travels around the cylinder so the glow never looks static.
 */

static const char META[] =
    "{\"name\":\"Candle\","
    "\"desc\":\"Warm living candlelight with organic flicker\","
    "\"params\":["
        "{\"id\":0,\"name\":\"Base\",\"type\":\"select\","
         "\"default\":0,"
         "\"options\":[\"Bottom\",\"Middle\"],"
         "\"desc\":\"Where the flame sits: at the bottom or from mid-height\"},"
        "{\"id\":1,\"name\":\"Warmth\",\"type\":\"int\","
         "\"min\":0,\"max\":40,\"default\":16,"
         "\"desc\":\"Color warmth (0=deep red, 40=golden)\"},"
        "{\"id\":2,\"name\":\"Flicker\",\"type\":\"int\","
         "\"min\":0,\"max\":100,\"default\":45,"
         "\"desc\":\"Flicker amount (drafts and dips)\"},"
        "{\"id\":3,\"name\":\"Speed\",\"type\":\"int\","
         "\"min\":1,\"max\":200,\"default\":80,"
         "\"desc\":\"Flicker speed\"},"
        "{\"id\":4,\"name\":\"Height\",\"type\":\"int\","
         "\"min\":20,\"max\":100,\"default\":70,"
         "\"desc\":\"Glow height (% of lamp)\"},"
        "{\"id\":5,\"name\":\"Brightness\",\"type\":\"int\","
         "\"min\":1,\"max\":255,\"default\":220,"
         "\"desc\":\"Overall brightness\"},"
        "{\"id\":6,\"name\":\"Dance\",\"type\":\"int\","
         "\"min\":0,\"max\":100,\"default\":40,"
         "\"desc\":\"Side-to-side shimmer around the lamp\"}"
    "]}";

EXPORT(get_meta_ptr)
int get_meta_ptr(void) { return (int)META; }
EXPORT(get_meta_len)
int get_meta_len(void) { return sizeof(META) - 1; }

/* ---- PRNG (xorshift32) ---- */
static uint32_t rng = 91537;
static uint32_t rng_next(void) {
    uint32_t x = rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    rng = x; return x;
}

/* ---- Sine (Bhaskara I), bounded input expected (a few periods) ---- */
#define TWO_PI 6.28318530f
#define PI     3.14159265f

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

/* ---- Candle palette: heat 0..1 -> warm RGB ----
 * Red saturates fast (flame is red even at the cool tip), green grows with
 * heat up to a warmth-controlled ceiling (body=orange, core=amber/gold),
 * a whisper of blue only in the very hot core for a candle's white heart. */
static void candle_color(float heat, int warmth, int bright,
                         int *r, int *g, int *b) {
    if (heat <= 0.0f) { *r = *g = *b = 0; return; }
    if (heat > 1.0f) heat = 1.0f;
    int q = (int)(heat * 255.0f);

    int gtop = 70 + warmth * 4;          /* warmth 0..40 -> 70..230 */
    int rr = q * 4; if (rr > 255) rr = 255;   /* red saturates near q=64 */
    int gg = q * gtop / 255;                  /* green lags behind red */
    int bb = (q > 200) ? (q - 200) * warmth / 40 : 0;  /* faint hot core */

    *r = rr * bright / 255;
    *g = gg * bright / 255;
    *b = bb * bright / 255;
}

#define MAX_W 64
#define MAX_H 64

/* Bounded phase accumulators (kept small so fsin wrap stays cheap) */
static float gphase = 0.0f;   /* global flicker phase */
static float dphase = 0.0f;   /* dance rotation phase */
static float gust   = 0.0f;   /* current draft dip, decays each frame */
static int32_t prev_tick = 0;

EXPORT(init)
void init(void) {
    rng = 91537;
    gphase = 0.0f;
    dphase = 0.0f;
    gust = 0.0f;
    prev_tick = 0;
}

EXPORT(update)
void update(int tick_ms) {
    int base    = get_param_i32(0);   /* 0 = bottom, 1 = from mid-height */
    int warmth  = get_param_i32(1);
    int flicker = get_param_i32(2);
    int speed   = get_param_i32(3);
    int height  = get_param_i32(4);
    int bright  = get_param_i32(5);
    int dance   = get_param_i32(6);

    int W = get_width();
    int H = get_height();
    if (W > MAX_W) W = MAX_W;
    if (H > MAX_H) H = MAX_H;
    if (W < 1) W = 1;
    if (H < 1) H = 1;

    /* Delta time (bounded) */
    int32_t d = tick_ms - prev_tick;
    if (d <= 0 || d > 200) d = 33;
    prev_tick = tick_ms;
    float dt = (float)d * 0.001f;

    float flickAmt = (float)flicker / 100.0f;
    float danceAmt = (float)dance / 100.0f;
    float sp = (float)speed * 0.03f;

    /* Advance bounded phases */
    gphase += dt * sp;
    while (gphase >= TWO_PI) gphase -= TWO_PI;
    dphase += dt * sp * 0.35f * danceAmt;
    while (dphase >= TWO_PI) dphase -= TWO_PI;

    /* Global flicker noise: layered sines -> 0..1 (mostly high, short dips) */
    float s = fsin(gphase) * 0.55f
            + fsin(gphase * 2.7f + 1.3f) * 0.30f
            + fsin(gphase * 6.1f + 4.0f) * 0.15f;
    float n = 0.5f + 0.5f * s;
    if (n < 0.0f) n = 0.0f;
    if (n > 1.0f) n = 1.0f;

    /* Draft gusts: random sharp dips that decay back, scaled by Flicker */
    rng ^= (uint32_t)tick_ms;
    gust *= 0.90f;
    if ((rng_next() & 0xFF) < (uint32_t)(flicker * 32 / 100)) {
        float mag = (0.15f + (float)(rng_next() % 100) / 100.0f * 0.30f) * flickAmt;
        gust += mag;
    }
    if (gust > 0.6f) gust = 0.6f;

    /* Global dip factor: 1 = full flame, lower = dimmed by flicker + gust */
    float gd = 1.0f - flickAmt * (1.0f - n) - gust;
    if (gd < 0.15f) gd = 0.15f;

    /* Vertical origin of the flame. The flame grows upward from here, so its
     * height is measured against the space remaining above the origin — that
     * keeps a proper tapering tip in both modes instead of clipping flat. */
    float y0 = (base == 1) ? (float)H * 0.5f : 0.0f;
    float avail = (float)H - y0;
    if (avail < 2.0f) avail = 2.0f;
    float baseH = (float)height / 100.0f * avail;
    if (baseH < 2.0f) baseH = 2.0f;
    float colStep = TWO_PI / (float)W;

    /* Overall brightness also breathes a little with the dip */
    float bfac = 0.55f + 0.45f * gd;

    for (int x = 0; x < W; x++) {
        /* Per-column shimmer that slowly rotates around the cylinder */
        float cph = (float)x * colStep + dphase;
        float cs = fsin(gphase * 1.3f + cph);
        float cn = 0.5f + 0.5f * cs;            /* 0..1 */

        /* Column flame height: base, dimmed globally, varied by the dance */
        float colH = baseH * gd * (1.0f - danceAmt * 0.45f * (1.0f - cn));
        if (colH < 1.5f) colH = 1.5f;

        int colBright = (int)((float)bright * bfac);
        if (colBright < 1) colBright = 1;
        if (colBright > 255) colBright = 255;

        for (int y = 0; y < H; y++) {
            /* Distance above the flame origin (negative = below it) */
            float local = (float)y - y0;
            float heat;
            if (local >= 0.0f && local < colH) {
                /* Inside the flame body: hottest at the base (local=0) */
                float qy = 1.0f - local / colH;        /* 1 base .. 0 tip */
                heat = qy * (0.45f + 0.55f * qy);      /* fuller body */
            } else if (local >= colH && local - colH < 4.0f) {
                /* Soft halo just above the tip — a few pixels of dim red */
                heat = (1.0f - (local - colH) / 4.0f) * 0.20f;
            } else if (local < 0.0f && -local < 3.0f) {
                /* Faint pool of glow just below the base (visible when raised) */
                heat = (1.0f + local / 3.0f) * 0.18f;
            } else {
                heat = 0.0f;
            }

            int r, g, b;
            candle_color(heat, warmth, colBright, &r, &g, &b);
            set_pixel(x, y, r, g, b);
        }
    }

    draw();
}
