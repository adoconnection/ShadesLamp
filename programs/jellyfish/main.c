#include "api.h"

/*
 * Jellyfish — bioluminescent jellyfish drifting upward through a dark ocean.
 * Rendered into an RGB framebuffer with anti-aliased primitives (m_blend for
 * soft glows, m_line for the bell body and wavy tentacles) so the slow drift
 * and pulsing read as smooth sub-pixel motion instead of stepping cell-to-cell.
 * X wraps around the cylinder; Y=0 is the bottom, jellyfish rise toward high Y.
 */

/* ---- Metadata JSON ---- */
static const char META[] =
    "{\"name\":\"Jellyfish\","
    "\"desc\":\"Bioluminescent jellyfish floating upward on a dark ocean\","
    "\"params\":["
        "{\"id\":0,\"name\":\"Hue\",\"type\":\"int\","
         "\"min\":0,\"max\":255,\"default\":180,"
         "\"desc\":\"Base color hue (180=cyan/turquoise)\"},"
        "{\"id\":1,\"name\":\"Count\",\"type\":\"int\","
         "\"min\":1,\"max\":5,\"default\":3,"
         "\"desc\":\"Number of jellyfish\"},"
        "{\"id\":2,\"name\":\"Speed\",\"type\":\"int\","
         "\"min\":1,\"max\":100,\"default\":25,"
         "\"desc\":\"Floating speed\"},"
        "{\"id\":3,\"name\":\"Brightness\",\"type\":\"int\","
         "\"min\":1,\"max\":255,\"default\":200,"
         "\"desc\":\"Overall brightness\"}"
    "]}";

EXPORT(get_meta_ptr)
int get_meta_ptr(void) { return (int)META; }

EXPORT(get_meta_len)
int get_meta_len(void) { return sizeof(META) - 1; }

/* ---- PRNG (xorshift32) ---- */
static uint32_t rng = 12345;
static uint32_t rng_next(void) {
    uint32_t x = rng; x ^= x << 13; x ^= x >> 17; x ^= x << 5; rng = x; return x;
}
static int rand_range(int lo, int hi) { if (lo >= hi) return lo; return lo + (int)(rng_next() % (uint32_t)(hi - lo)); }
static float frand(void) { return (float)(rng_next() & 0xFFFF) / 65536.0f; }

/* ---- helpers ---- */
static float fsin(float x) { return m_sin(x); }
static float fabs_f(float x) { return x < 0.0f ? -x : x; }

static int packrgb(int r, int g, int b) {
    if (r < 0) r = 0; if (r > 255) r = 255;
    if (g < 0) g = 0; if (g > 255) g = 255;
    if (b < 0) b = 0; if (b > 255) b = 255;
    return (r << 16) | (g << 8) | b;
}
/* packed HSV colour scaled by a 0..1 brightness factor */
static int hsv_scaled(int hue, int sat, float val01) {
    int v = (int)(val01 * 255.0f + 0.5f);
    if (v <= 0) return 0;
    if (v > 255) v = 255;
    return m_hsv(hue & 0xFF, sat, v);
}

/* ---- Framebuffer ---- */
#define MAX_W 64
#define MAX_H 64
static uint8_t FB[MAX_W * MAX_H * 3];
EXPORT(get_framebuffer)
int get_framebuffer(void) { return (int)FB; }

static int cur_w, cur_h;

/* anti-aliased splat / line with cylinder wrap (host clips off-screen copies) */
static void blend_w(float fx, float fy, int rgb) {
    if (rgb == 0) return;
    m_blend(FB, cur_w, cur_h, fx,              fy, rgb);
    m_blend(FB, cur_w, cur_h, fx - (float)cur_w, fy, rgb);
    m_blend(FB, cur_w, cur_h, fx + (float)cur_w, fy, rgb);
}
static void line_w(float x0, float y0, float x1, float y1, int rgb) {
    if (rgb == 0) return;
    m_line(FB, cur_w, cur_h, x0,            y0, x1,            y1, rgb);
    m_line(FB, cur_w, cur_h, x0 - (float)cur_w, y0, x1 - (float)cur_w, y1, rgb);
    m_line(FB, cur_w, cur_h, x0 + (float)cur_w, y0, x1 + (float)cur_w, y1, rgb);
}

/* ---- Jellyfish state ---- */
#define MAX_JELLY 5

static float jf_x[MAX_JELLY];      /* horizontal position (float, wraps) */
static float jf_y[MAX_JELLY];      /* vertical position (float, moves up) */
static float jf_size[MAX_JELLY];   /* bell half-width (float, 3-6) */
static float jf_phase[MAX_JELLY];  /* pulsing phase */
static float jf_drift[MAX_JELLY];  /* horizontal drift phase */
static int   jf_hue_off[MAX_JELLY];/* per-jellyfish hue variation */

static int32_t prev_tick;

/* Plankton sparkle state (float positions for soft AA dots) */
#define MAX_SPARKLE 12
static float sparkle_x[MAX_SPARKLE];
static float sparkle_y[MAX_SPARKLE];
static int   sparkle_life[MAX_SPARKLE];
static int   sparkle_max[MAX_SPARKLE];

static void spawn_jellyfish(int i) {
    jf_x[i] = frand() * (float)cur_w;
    jf_y[i] = -(float)rand_range(2, 8);  /* start below the bottom (Y=0) */
    jf_size[i] = 3.0f + frand() * 3.5f;  /* bell half-width 3-6.5 */
    jf_phase[i] = frand() * 6.28318530f;
    jf_drift[i] = frand() * 6.28318530f;
    jf_hue_off[i] = rand_range(-15, 16);
}

static void spawn_sparkle(int i) {
    sparkle_x[i] = frand() * (float)cur_w;
    sparkle_y[i] = frand() * (float)cur_h;
    sparkle_life[i] = rand_range(20, 70);
    sparkle_max[i] = sparkle_life[i];
}

EXPORT(init)
void init(void) {
    cur_w = get_width();
    cur_h = get_height();
    if (cur_w < 1) cur_w = 1;
    if (cur_h < 1) cur_h = 1;
    prev_tick = 0;

    for (int i = 0; i < MAX_JELLY; i++) {
        spawn_jellyfish(i);
        /* Spread initial positions vertically so they don't all start together */
        jf_y[i] = frand() * (float)cur_h;
    }
    for (int i = 0; i < MAX_SPARKLE; i++) spawn_sparkle(i);
}

EXPORT(update)
void update(int tick_ms) {
    int base_hue = get_param_i32(0);
    int count    = get_param_i32(1);
    int speed    = get_param_i32(2);
    int bright   = get_param_i32(3);

    cur_w = get_width();
    cur_h = get_height();
    if (cur_w < 1) cur_w = 1;
    if (cur_h < 1) cur_h = 1;
    if (cur_w > MAX_W) cur_w = MAX_W;
    if (cur_h > MAX_H) cur_h = MAX_H;
    if (count < 1) count = 1;
    if (count > MAX_JELLY) count = MAX_JELLY;

    rng ^= (uint32_t)tick_ms;
    int32_t delta_ms = tick_ms - prev_tick;
    if (delta_ms <= 0 || delta_ms > 200) delta_ms = 33;
    prev_tick = tick_ms;

    float dt = (float)delta_ms / 1000.0f;
    float spd = (float)speed / 25.0f;  /* normalize: 25 -> 1.0 */
    float bf = (float)bright / 255.0f; /* overall brightness 0..1 */

    /* ---- Background: dark deep-blue vertical gradient (overwrite FB) ---- */
    for (int y = 0; y < cur_h; y++) {
        int bg_b = (int)((3.0f + (float)y * 5.0f / (float)cur_h) * bf);
        int bg_g = (int)((1.0f + (float)y * 2.0f / (float)cur_h) * bf);
        int rgb = packrgb(0, bg_g, bg_b);
        uint8_t rr = (uint8_t)((rgb >> 16) & 255);
        uint8_t gg = (uint8_t)((rgb >> 8) & 255);
        uint8_t bb = (uint8_t)(rgb & 255);
        int row = y * cur_w * 3;
        for (int x = 0; x < cur_w; x++) {
            FB[row + x * 3 + 0] = rr;
            FB[row + x * 3 + 1] = gg;
            FB[row + x * 3 + 2] = bb;
        }
    }

    /* ---- Plankton sparkles: soft AA dots that fade in and out ---- */
    for (int i = 0; i < MAX_SPARKLE; i++) {
        sparkle_life[i]--;
        if (sparkle_life[i] <= 0) { spawn_sparkle(i); continue; }
        /* triangular fade over lifetime -> 0..1 */
        float t = (float)sparkle_life[i] / (float)(sparkle_max[i] > 0 ? sparkle_max[i] : 1);
        float env = 1.0f - fabs_f(t * 2.0f - 1.0f);  /* peak in the middle */
        float v = env * 0.35f * bf;
        blend_w(sparkle_x[i], sparkle_y[i], hsv_scaled((base_hue + 20) & 0xFF, 90, v));
    }

    /* ---- Update and draw jellyfish ---- */
    for (int j = 0; j < count; j++) {
        /* Advance phases */
        jf_phase[j] += dt * 2.5f * spd;
        jf_drift[j] += dt * 0.7f * spd;

        float pulse = fsin(jf_phase[j]);      /* -1 to 1 */
        float pulse01 = 0.5f + 0.5f * pulse;  /* 0 to 1 */

        /* Move upward; faster during "expansion" like a real jellyfish stroke */
        float rise_speed = (1.5f + jf_size[j] * 0.3f) * spd;
        float swim_boost = 1.0f + 0.5f * pulse;
        jf_y[j] += rise_speed * swim_boost * dt;

        /* Horizontal drift: gentle sine wave */
        float drift_amount = fsin(jf_drift[j]) * 0.8f * spd;
        jf_x[j] += drift_amount * dt;

        while (jf_x[j] < 0.0f)               jf_x[j] += (float)cur_w;
        while (jf_x[j] >= (float)cur_w)       jf_x[j] -= (float)cur_w;

        /* Respawn once it has fully floated past the top */
        if (jf_y[j] > (float)cur_h + jf_size[j] + 8.0f) {
            spawn_jellyfish(j);
        }

        float cx = jf_x[j];
        float cy = jf_y[j];
        float bell_w = jf_size[j] * (0.85f + 0.15f * pulse01);  /* half-width, pulses */
        float bell_h = bell_w * 0.75f;
        if (bell_h < 1.5f) bell_h = 1.5f;

        int hue = (base_hue + jf_hue_off[j]) & 0xFF;
        float pb = bf * (0.6f + 0.4f * pulse01);  /* pulsing brightness 0..1 */

        /* -- Bell/dome: stacked anti-aliased horizontal spans forming a dome.
              Sub-sample rows so the dome stays smooth as it drifts. -- */
        int rows = (int)(bell_h * 2.0f) + 2;
        for (int s = 0; s <= rows; s++) {
            float norm = (float)s / (float)rows;     /* 0 at rim, 1 at dome top */
            float py = cy + norm * bell_h;
            float half_span = bell_w * (1.0f - norm * norm);
            if (half_span < 0.15f) half_span = 0.15f;

            float vert = 0.5f + 0.5f * norm;         /* brighter toward the top */
            int sat = 200 + (int)(30.0f * norm);
            if (sat > 255) sat = 255;
            int rgb = hsv_scaled(hue, sat, pb * vert * 0.55f);
            line_w(cx - half_span, py, cx + half_span, py, rgb);
        }

        /* -- Bell rim: bright arc at the bottom of the bell -- */
        {
            float rim_span = bell_w;
            int rgb = hsv_scaled(hue, 180, pb);
            line_w(cx - rim_span, cy, cx + rim_span, cy, rgb);
        }

        /* -- Inner glow: soft bright core inside the bell -- */
        {
            float gy = cy + bell_h * 0.5f;
            blend_w(cx, gy, hsv_scaled(hue, 90, pb * (0.7f + 0.3f * pulse01)));
            blend_w(cx, gy + 0.6f, hsv_scaled(hue, 120, pb * 0.4f));
        }

        /* -- Tentacles: wavy anti-aliased lines hanging below the bell -- */
        int num_tentacles = (int)(bell_w) ;
        if (num_tentacles < 2) num_tentacles = 2;
        if (num_tentacles > 6) num_tentacles = 6;

        float tent_len = bell_w + 2.0f + 2.0f * pulse01;  /* longer during expansion */

        for (int ti = 0; ti < num_tentacles; ti++) {
            float attach_offset = (num_tentacles > 1)
                ? bell_w * (-1.0f + 2.0f * (float)ti / (float)(num_tentacles - 1))
                : 0.0f;
            float tent_phase = jf_phase[j] * 0.8f + (float)ti * 1.3f;

            /* poly-line down through the segments, colour fading with depth */
            float px = cx + attach_offset;
            float pyy = cy;
            int nseg = (int)tent_len;
            for (int seg = 1; seg <= nseg; seg++) {
                float fseg = (float)seg;
                float ty = cy - fseg;                 /* hang downward (toward Y=0) */
                float wave_amp = 0.5f + fseg * 0.18f;
                float wave_x = fsin(tent_phase + fseg * 0.7f) * wave_amp;
                float tx = cx + attach_offset + wave_x;

                float fade = 1.0f - fseg / (tent_len + 1.0f);
                fade = fade * fade;                   /* quadratic falloff */
                int rgb = hsv_scaled(hue, 220, pb * 0.5f * fade);
                line_w(px, pyy, tx, ty, rgb);
                px = tx; pyy = ty;
            }
        }
    }

    draw();
}
