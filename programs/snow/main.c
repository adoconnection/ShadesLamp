#include "api.h"

/* Snow 2.0 — soft anti-aliased snowfall over a tinted sky.
 *
 * Flakes live in float coordinates and advance with delta-time; they are
 * rendered anti-aliased (m_blend sub-pixel splats for small flakes, smooth
 * additive discs for larger ones), so motion never snaps to the pixel grid.
 * Depth coupling: bigger flakes fall faster and glow brighter (parallax).
 * The sky is a vertical HSV gradient controlled by hue + intensity params.
 */

/* ---- Metadata JSON ---- */
static const char META[] =
    "{\"name\":\"Snow\","
    "\"desc\":\"Soft anti-aliased snowfall over a tinted sky\","
    "\"params\":["
        "{\"id\":0,\"name\":\"Density\",\"type\":\"int\","
         "\"min\":1,\"max\":30,\"default\":15,"
         "\"desc\":\"How many snowflakes are in the air\"},"
        "{\"id\":1,\"name\":\"Speed\",\"type\":\"int\","
         "\"min\":1,\"max\":10,\"default\":4,"
         "\"desc\":\"How fast snowflakes fall\"},"
        "{\"id\":2,\"name\":\"Brightness\",\"type\":\"int\","
         "\"min\":1,\"max\":255,\"default\":200,"
         "\"desc\":\"Snowflake brightness\"},"
        "{\"id\":3,\"name\":\"Flake size\",\"type\":\"int\","
         "\"min\":1,\"max\":10,\"default\":4,"
         "\"desc\":\"Average snowflake size\"},"
        "{\"id\":4,\"name\":\"Sky hue\",\"type\":\"int\","
         "\"min\":0,\"max\":255,\"default\":160,"
         "\"desc\":\"Background colour (hue)\"},"
        "{\"id\":5,\"name\":\"Sky light\",\"type\":\"int\","
         "\"min\":0,\"max\":100,\"default\":25,"
         "\"desc\":\"Background intensity (0 = black)\"}"
    "]}";

EXPORT(get_meta_ptr)
int get_meta_ptr(void) { return (int)META; }

EXPORT(get_meta_len)
int get_meta_len(void) { return sizeof(META) - 1; }

/* ---- PRNG (xorshift32) ---- */
static uint32_t rng_state = 91573;

static uint32_t rng_next(void) {
    uint32_t x = rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng_state = x;
    return x;
}

static float random_float(void) {
    return (float)(rng_next() & 0xFFFF) / 65536.0f;
}

/* ---- Framebuffer fast-path ---- */
#define MAX_W 64
#define MAX_H 64
static uint8_t FB[MAX_W * MAX_H * 3];

EXPORT(get_framebuffer)
int get_framebuffer(void) { return (int)FB; }

/* ---- Snowflakes ---- */
#define MAX_FLAKES 128
#define TWO_PI 6.2831853f

static uint8_t fl_on[MAX_FLAKES];
static float   fl_x[MAX_FLAKES];      /* float positions — never rounded */
static float   fl_y[MAX_FLAKES];
static float   fl_depth[MAX_FLAKES];  /* 0.6..1.4 — couples size, speed, brightness */
static float   fl_phase[MAX_FLAKES];  /* horizontal sway phase */
static float   fl_freq[MAX_FLAKES];   /* sway angular speed, rad/s */
static float   fl_amp[MAX_FLAKES];    /* sway amplitude, px/s */
static float   fl_glint[MAX_FLAKES];  /* twinkle phase */
static float   fl_age[MAX_FLAKES];    /* seconds since spawn */

/* Snow cover on the bottom row: 0..1 per column, slowly melts. */
static float accum[MAX_W];

static int32_t prev_tick;

EXPORT(init)
void init(void) {
    rng_state = 91573;
    prev_tick = 0;
    for (int i = 0; i < MAX_FLAKES; i++) fl_on[i] = 0;
    for (int i = 0; i < MAX_W; i++) accum[i] = 0.0f;
}

static uint8_t qadd8(uint8_t a, int add) {
    int s = (int)a + add;
    return (s > 255) ? 255 : (uint8_t)s;
}

static int ifloor(float v) {
    int i = (int)v;
    return ((float)i > v) ? i - 1 : i;
}

/* Cold-white additive splat colour for a peak brightness 0..255. */
static int pack_snow(float b) {
    int v = (int)b;
    if (v > 255) v = 255;
    if (v < 0) v = 0;
    return ((v * 235 / 255) << 16) | ((v * 245 / 255) << 8) | v;
}

/* Smooth additive disc: analytic coverage (1 - d^2/r^2)^2, cylinder-wrapped.
 * Same soft-edge idea as m_blend, extended to arbitrary radius. */
static void draw_disc(int W, int H, float fx, float fy, float rad, float b) {
    int x0 = ifloor(fx - rad), x1 = ifloor(fx + rad) + 1;
    int y0 = ifloor(fy - rad), y1 = ifloor(fy + rad) + 1;
    float inv = 1.0f / (rad * rad);
    for (int y = y0; y <= y1; y++) {
        if (y < 0 || y >= H) continue;
        float dy = (float)y - fy;
        float dy2 = dy * dy;
        for (int x = x0; x <= x1; x++) {
            float dx = (float)x - fx;
            float c = 1.0f - (dx * dx + dy2) * inv;
            if (c <= 0.0f) continue;
            c *= c;
            int add = (int)(b * c);
            if (add <= 0) continue;
            int xx = x;
            if (xx < 0) xx += W;
            else if (xx >= W) xx -= W;
            int idx = (y * W + xx) * 3;
            FB[idx]     = qadd8(FB[idx],     add * 235 / 255);
            FB[idx + 1] = qadd8(FB[idx + 1], add * 245 / 255);
            FB[idx + 2] = qadd8(FB[idx + 2], add);
        }
    }
}

EXPORT(update)
void update(int tick_ms) {
    int density = get_param_i32(0);   /* 1-30  */
    int speed   = get_param_i32(1);   /* 1-10  */
    int bright  = get_param_i32(2);   /* 1-255 */
    int size    = get_param_i32(3);   /* 1-10  */
    int bg_hue  = get_param_i32(4);   /* 0-255 */
    int bg_lvl  = get_param_i32(5);   /* 0-100 */

    int W = get_width();
    int H = get_height();
    if (W > MAX_W) W = MAX_W;
    if (H > MAX_H) H = MAX_H;
    if (W < 1) W = 1;
    if (H < 2) H = 2;

    int dms = tick_ms - prev_tick;
    if (dms <= 0 || dms > 200) dms = 33;
    prev_tick = tick_ms;
    float dt = (float)dms * 0.001f;
    float t = (float)tick_ms * 0.001f;

    rng_state ^= (uint32_t)tick_ms;

    /* ---- Sky: vertical HSV gradient, a touch brighter towards the top ---- */
    int val_top = bg_lvl * 140 / 100;                  /* 0..140 */
    for (int y = 0; y < H; y++) {
        float k = 0.65f + 0.35f * (float)y / (float)(H - 1);   /* y=0 is bottom */
        int v = (int)((float)val_top * k + 0.5f);
        int c = (v > 0) ? m_hsv(bg_hue, 205, v) : 0;
        uint8_t r = (uint8_t)((c >> 16) & 255);
        uint8_t g = (uint8_t)((c >> 8) & 255);
        uint8_t b = (uint8_t)(c & 255);
        uint8_t* row = &FB[y * W * 3];
        for (int x = 0; x < W; x++) {
            row[x * 3]     = r;
            row[x * 3 + 1] = g;
            row[x * 3 + 2] = b;
        }
    }

    /* ---- Spawn: hold a density-scaled flake count, easing in one per frame ---- */
    int target = density * W / 8;
    if (target > MAX_FLAKES) target = MAX_FLAKES;
    int active = 0;
    for (int i = 0; i < MAX_FLAKES; i++) active += fl_on[i];
    if (active < target) {
        for (int i = 0; i < MAX_FLAKES; i++) {
            if (fl_on[i]) continue;
            fl_on[i]    = 1;
            fl_x[i]     = random_float() * (float)W;
            fl_y[i]     = (float)H + random_float() * 2.0f;  /* just above the top */
            fl_depth[i] = 0.6f + random_float() * 0.8f;
            fl_phase[i] = random_float() * TWO_PI;
            fl_freq[i]  = 0.5f + random_float() * 1.2f;
            fl_amp[i]   = 0.35f + random_float() * 0.65f;
            fl_glint[i] = random_float() * TWO_PI;
            fl_age[i]   = 0.0f;
            break;
        }
    }

    /* Shared slow wind so the whole snowfall leans together. */
    float wind = m_sin(t * 0.13f) * 0.55f + m_sin(t * 0.047f) * 0.35f;  /* px/s */

    float v_base = 1.6f + (float)speed * 1.35f;   /* px/s at depth 1 */
    float r_mean = 0.55f + (float)size * 0.16f;   /* px */
    float snow_b = (float)bright;

    for (int i = 0; i < MAX_FLAKES; i++) {
        if (!fl_on[i]) continue;
        fl_age[i] += dt;

        float depth = fl_depth[i];
        fl_y[i] -= v_base * (0.55f + 0.5f * depth) * dt;   /* y=0 is bottom */
        fl_x[i] += (wind + m_sin(fl_age[i] * fl_freq[i] + fl_phase[i]) * fl_amp[i]) * dt;

        if (fl_x[i] < 0.0f)           fl_x[i] += (float)W;
        else if (fl_x[i] >= (float)W) fl_x[i] -= (float)W;

        /* Landed: melt into the bottom-row snow cover (bilinear split). */
        if (fl_y[i] < 0.3f) {
            int x0 = (int)fl_x[i];
            if (x0 >= W) x0 = W - 1;
            int x1 = (x0 + 1) % W;
            float fx = fl_x[i] - (float)x0;
            float amt = 0.35f * (0.6f + 0.4f * depth);
            accum[x0] += amt * (1.0f - fx);
            accum[x1] += amt * fx;
            if (accum[x0] > 1.0f) accum[x0] = 1.0f;
            if (accum[x1] > 1.0f) accum[x1] = 1.0f;
            fl_on[i] = 0;
            continue;
        }

        /* Brightness: depth parallax + soft spawn fade-in + gentle twinkle. */
        float a = fl_age[i] * 1.6f;
        if (a > 1.0f) a = 1.0f;
        float tw = 0.86f + 0.14f * m_sin(fl_age[i] * 2.7f + fl_glint[i]);
        float b = snow_b * (0.45f + 0.55f * depth) * a * tw;

        float rad = r_mean * depth;
        if (rad <= 0.85f) {
            /* Small flake: native anti-aliased 2x2 sub-pixel splat. */
            int c = pack_snow(b);
            m_blend(FB, W, H, fl_x[i], fl_y[i], c);
            if (fl_x[i] > (float)(W - 1))            /* seam: mirrored splat */
                m_blend(FB, W, H, fl_x[i] - (float)W, fl_y[i], c);
        } else {
            draw_disc(W, H, fl_x[i], fl_y[i], rad, b);
        }
    }

    /* ---- Bottom-row snow cover: additive over the sky, slowly melting ---- */
    for (int x = 0; x < W; x++) {
        if (accum[x] <= 0.004f) { accum[x] = 0.0f; continue; }
        int add = (int)(snow_b * 0.9f * accum[x]);
        FB[x * 3]     = qadd8(FB[x * 3],     add * 225 / 255);
        FB[x * 3 + 1] = qadd8(FB[x * 3 + 1], add * 238 / 255);
        FB[x * 3 + 2] = qadd8(FB[x * 3 + 2], add);
        accum[x] -= dt * 0.10f;                      /* ~10 s to melt fully */
    }

    draw();
}
