#include "api.h"

/* ---- Metadata JSON ---- */
static const char META[] =
    "{\"name\":\"Fireflies\","
    "\"desc\":\"Random floating bright dots that drift and fade\","
    "\"params\":["
        "{\"id\":0,\"name\":\"Count\",\"type\":\"int\","
         "\"min\":1,\"max\":20,\"default\":8,"
         "\"desc\":\"Number of fireflies\"},"
        "{\"id\":1,\"name\":\"Speed\",\"type\":\"int\","
         "\"min\":1,\"max\":10,\"default\":5,"
         "\"desc\":\"Movement speed\"},"
        "{\"id\":2,\"name\":\"Brightness\",\"type\":\"int\","
         "\"min\":1,\"max\":255,\"default\":200,"
         "\"desc\":\"Overall brightness\"},"
        "{\"id\":3,\"name\":\"Mode\",\"type\":\"select\","
         "\"options\":[\"Drift\",\"Flight\"],\"default\":1,"
         "\"desc\":\"Drift = smooth wander, Flight = curved darts with pauses\"}"
    "]}";

EXPORT(get_meta_ptr)
int get_meta_ptr(void) { return (int)META; }

EXPORT(get_meta_len)
int get_meta_len(void) { return sizeof(META) - 1; }

/* ---- PRNG (xorshift32) ---- */
static uint32_t rng_state = 92731;

static uint32_t rng_next(void) {
    uint32_t x = rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng_state = x;
    return x;
}

static int random8(void) {
    return (int)(rng_next() & 0xFF);
}

static int random_range(int lo, int hi) {
    if (lo >= hi) return lo;
    return lo + (int)(rng_next() % (uint32_t)(hi - lo));
}

/* ---- Framebuffer fast-path ----
 * We render into our own RGB buffer and let the host copy it in one go. This
 * also lets us use the native anti-aliased splat (m_blend), which writes a
 * sub-pixel 2x2 bilinear additive blob — soft edges + jitter-free motion. */
#define MAX_W 64
#define MAX_H 64
static uint8_t FB[MAX_W * MAX_H * 3];
EXPORT(get_framebuffer)
int get_framebuffer(void) { return (int)FB; }

static int W, H;

/* ---- Firefly state ---- */
#define MAX_FLIES 20

/* Sub-pixel positions kept as float — never rounded — so motion stays smooth. */
static float fly_x[MAX_FLIES];
static float fly_y[MAX_FLIES];
static float fly_vx[MAX_FLIES];
static float fly_vy[MAX_FLIES];
static int   fly_hue[MAX_FLIES];
static int   fly_brightness[MAX_FLIES]; /* individual brightness for fade in/out */
static int   fly_phase[MAX_FLIES];      /* 0=fading in, 1=alive, 2=fading out */
static int   fly_life[MAX_FLIES];       /* ticks remaining in current phase */

/* Flight mode (id 3 == 1): a firefly alternates short curved darts with hovers,
 * which reads far more like a real firefly than a constant drift. */
static int   fly_mstate[MAX_FLIES];     /* 0 = darting, 1 = resting/hovering */
static int   fly_mtime[MAX_FLIES];      /* frames left in the current mstate */
static float fly_head[MAX_FLIES];       /* heading angle, radians */
static float fly_turn[MAX_FLIES];       /* per-frame heading delta -> curved path */
static float fly_dspd[MAX_FLIES];       /* dart speed magnitude */

static int step_counter;

/* Begin a new curved dart in a random direction. */
static void start_dart(int i) {
    fly_head[i] = (float)(rng_next() % 6283) / 1000.0f;             /* 0..2pi */
    fly_turn[i] = ((float)(rng_next() % 2000) / 1000.0f - 1.0f) * 0.12f; /* +/-0.12 rad/frame */
    fly_dspd[i] = 2.5f + (float)(rng_next() % 2000) / 1000.0f * 2.0f;    /* 2.5..4.5 */
    fly_mstate[i] = 0;
    fly_mtime[i] = random_range(6, 16);    /* short burst */
}

EXPORT(init)
void init(void) {
    W = get_width();
    H = get_height();

    step_counter = 0;

    for (int i = 0; i < MAX_FLIES; i++) {
        fly_x[i] = (float)random_range(0, W * 10) / 10.0f;
        fly_y[i] = (float)random_range(0, H * 10) / 10.0f;
        fly_vx[i] = (float)random_range(-10, 11) / 10.0f;
        fly_vy[i] = (float)random_range(-10, 11) / 10.0f;
        fly_hue[i] = random8();
        fly_brightness[i] = 0;
        fly_phase[i] = 0;  /* start fading in */
        fly_life[i] = random_range(10, 40);
        /* Flight mode starts hovering, then darts. */
        fly_mstate[i] = 1;
        fly_mtime[i] = random_range(8, 30);
        fly_head[i] = 0.0f;
        fly_turn[i] = 0.0f;
        fly_dspd[i] = 0.0f;
    }
}

static float f_abs(float x) {
    return x < 0.0f ? -x : x;
}

/* Add a packed 0xRRGGBB into FB at (wrapped) integer pixel, saturating. */
static void add_px(int x, int y, int rgb) {
    while (x < 0)   x += W;
    while (x >= W)  x -= W;
    if (y < 0 || y >= H) return;
    int o = (y * W + x) * 3;
    int r = FB[o]   + ((rgb >> 16) & 255); if (r > 255) r = 255; FB[o]   = (uint8_t)r;
    int g = FB[o+1] + ((rgb >> 8)  & 255); if (g > 255) g = 255; FB[o+1] = (uint8_t)g;
    int b = FB[o+2] + ( rgb        & 255); if (b > 255) b = 255; FB[o+2] = (uint8_t)b;
}

/* Soft round glow centred on the FLOAT position. Rendering a continuous radial
 * coverage field (not a 1-pixel stamp) is what actually reads as smooth on the
 * low-res matrix: as the centre drifts a fraction of a pixel the whole blob's
 * brightness flows between LEDs. Horizontal wrap handled per pixel.
 * A tiny m_blend core on top adds an anti-aliased highlight. */
static void glow_dot(float cx, float cy, float radius, int hue, int peak) {
    float inv = 1.0f / (radius * radius);
    int x0 = (int)(cx - radius) - 1, x1 = (int)(cx + radius) + 1;
    int y0 = (int)(cy - radius) - 1, y1 = (int)(cy + radius) + 1;
    for (int y = y0; y <= y1; y++) {
        if (y < 0 || y >= H) continue;
        float dy = (float)y - cy;
        for (int x = x0; x <= x1; x++) {
            float dx = (float)x - cx;
            float cover = 1.0f - (dx * dx + dy * dy) * inv;
            if (cover <= 0.0f) continue;
            cover *= cover;                       /* smoother shoulder */
            int v = (int)((float)peak * cover);
            if (v < 1) continue;
            add_px(x, y, m_hsv(hue, 230, v));
        }
    }
    /* Crisp sub-pixel highlight (AA primitive); seam-wrap the x coordinate. */
    float hx = cx;
    while (hx < 0.0f)      hx += (float)W;
    while (hx >= (float)W) hx -= (float)W;
    m_blend(FB, W, H, hx, cy, m_hsv(hue, 255, peak));
}

EXPORT(update)
void update(int tick_ms) {
    int count  = get_param_i32(0);
    int speed  = get_param_i32(1);
    int bright = get_param_i32(2);
    int mode   = get_param_i32(3);

    W = get_width();
    H = get_height();
    if (W < 1) W = 1;
    if (H < 1) H = 1;
    if (W > MAX_W) W = MAX_W;
    if (H > MAX_H) H = MAX_H;
    if (count > MAX_FLIES) count = MAX_FLIES;
    if (count < 1) count = 1;

    rng_state ^= (uint32_t)tick_ms;

    float speed_factor = (float)speed / 5.0f;

    step_counter++;

    /* Clear the framebuffer (m_blend is additive, so start from black). */
    m_fill(FB, W * H, 0);

    for (int i = 0; i < count; i++) {
        if (mode == 1) {
            /* Flight: dart along a gentle arc for a few frames, then hover. */
            fly_mtime[i]--;
            if (fly_mstate[i] == 0) {                 /* darting */
                fly_head[i] += fly_turn[i];           /* curved path */
                fly_vx[i] = m_cos(fly_head[i]) * fly_dspd[i];
                fly_vy[i] = m_sin(fly_head[i]) * fly_dspd[i];
                if (fly_mtime[i] <= 0) {              /* -> hover (freeze) */
                    fly_mstate[i] = 1;
                    fly_mtime[i] = random_range(12, 45);
                    fly_vx[i] = 0.0f;
                    fly_vy[i] = 0.0f;
                }
            } else {                                  /* hovering */
                fly_vx[i] = 0.0f;
                fly_vy[i] = 0.0f;
                if (fly_mtime[i] <= 0) start_dart(i);
            }
        } else {
            /* Drift: periodically nudge velocity (every ~20 frames). */
            if ((step_counter % 20) == (i % 20)) {
                fly_vx[i] += (float)random_range(-3, 4) / 10.0f;
                fly_vy[i] += (float)random_range(-3, 4) / 10.0f;
                /* Clamp velocity */
                if (fly_vx[i] > 2.0f) fly_vx[i] = 2.0f;
                if (fly_vx[i] < -2.0f) fly_vx[i] = -2.0f;
                if (fly_vy[i] > 2.0f) fly_vy[i] = 2.0f;
                if (fly_vy[i] < -2.0f) fly_vy[i] = -2.0f;
            }
        }

        /* Move */
        fly_x[i] += fly_vx[i] * speed_factor * 0.1f;
        fly_y[i] += fly_vy[i] * speed_factor * 0.1f;

        /* Wrap horizontally (cylinder) */
        if (fly_x[i] < 0.0f) fly_x[i] += (float)W;
        if (fly_x[i] >= (float)W) fly_x[i] -= (float)W;

        /* Bounce vertically */
        if (fly_y[i] < 0.0f) {
            fly_y[i] = -fly_y[i];
            fly_vy[i] = f_abs(fly_vy[i]);
        }
        if (fly_y[i] >= (float)(H - 1)) {
            fly_y[i] = (float)(H - 1) * 2.0f - fly_y[i];
            fly_vy[i] = -f_abs(fly_vy[i]);
        }
        /* In flight mode keep the heading in sync with the bounced velocity so
         * the dart continues in the reflected direction instead of into the wall. */
        if (mode == 1 && fly_mstate[i] == 0)
            fly_head[i] = m_atan2(fly_vy[i], fly_vx[i]);

        /* Phase/lifecycle management */
        fly_life[i]--;
        if (fly_life[i] <= 0) {
            fly_phase[i]++;
            if (fly_phase[i] > 2) {
                /* Respawn */
                fly_phase[i] = 0;
                fly_x[i] = (float)random_range(0, W);
                fly_y[i] = (float)random_range(0, H);
                fly_vx[i] = (float)random_range(-10, 11) / 10.0f;
                fly_vy[i] = (float)random_range(-10, 11) / 10.0f;
                fly_hue[i] = random8();
                /* Reset flight state too: start the new firefly hovering. */
                fly_mstate[i] = 1;
                fly_mtime[i] = random_range(8, 30);
            }
            fly_life[i] = random_range(15, 60);
        }

        /* Calculate brightness based on phase */
        switch (fly_phase[i]) {
            case 0: { /* Fading in */
                int max_life = 30;
                int elapsed = max_life - fly_life[i];
                if (elapsed < 0) elapsed = 0;
                fly_brightness[i] = bright * elapsed / max_life;
                if (fly_brightness[i] > bright) fly_brightness[i] = bright;
                break;
            }
            case 1: /* Alive - full brightness */
                fly_brightness[i] = bright;
                break;
            case 2: { /* Fading out */
                fly_brightness[i] = bright * fly_life[i] / 30;
                if (fly_brightness[i] < 0) fly_brightness[i] = 0;
                break;
            }
        }

        int b = fly_brightness[i];
        if (b < 1) continue;

        /* Warm yellow-green hues typical for fireflies */
        int hue = (fly_hue[i] % 64) + 32; /* restrict to yellow-green range (32-96) */

        /* One soft, sub-pixel-centred glow per firefly — smooth drift. */
        glow_dot(fly_x[i], fly_y[i], 1.8f, hue, b);
    }

    draw();
}
