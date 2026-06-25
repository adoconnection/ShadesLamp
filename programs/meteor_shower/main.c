#include "api.h"

/*
 * Meteor Shower — meteors streak in from the top, fall diagonally with a
 * glowing tail and explode into a spray of sparks when they hit the ground.
 * Based on the Fireworks program. Y=0 is the BOTTOM of the display, so
 * "falling" means y decreasing and the impact splash flies upward (+y).
 */

static const char META[] =
    "{\"name\":\"Meteor Shower\","
    "\"desc\":\"Meteors fall from the sky and burst on impact\","
    "\"params\":["
        "{\"id\":0,\"name\":\"Count\",\"type\":\"int\","
         "\"min\":1,\"max\":12,\"default\":6,"
         "\"desc\":\"Number of simultaneous meteors\"},"
        "{\"id\":1,\"name\":\"Brightness\",\"type\":\"int\","
         "\"min\":1,\"max\":255,\"default\":210,"
         "\"desc\":\"Overall brightness\"},"
        "{\"id\":2,\"name\":\"Speed\",\"type\":\"int\","
         "\"min\":10,\"max\":100,\"default\":50,"
         "\"desc\":\"Fall speed\"},"
        "{\"id\":3,\"name\":\"Trail\",\"type\":\"int\","
         "\"min\":1,\"max\":14,\"default\":7,"
         "\"desc\":\"Tail length\"},"
        "{\"id\":4,\"name\":\"Colour\",\"type\":\"select\","
         "\"options\":[\"Fire\",\"Icy\",\"Mixed\"],\"default\":0,"
         "\"desc\":\"Meteor colour palette\"},"
        "{\"id\":5,\"name\":\"Max Angle\",\"type\":\"int\","
         "\"min\":0,\"max\":70,\"default\":30,"
         "\"desc\":\"Max fall angle from vertical (deg)\"},"
        "{\"id\":6,\"name\":\"Blast Velocity\",\"type\":\"int\","
         "\"min\":20,\"max\":200,\"default\":100,"
         "\"desc\":\"How fast sparks fly out on impact\"},"
        "{\"id\":7,\"name\":\"Blast Size\",\"type\":\"int\","
         "\"min\":3,\"max\":16,\"default\":14,"
         "\"desc\":\"Number of sparks per impact\"},"
        "{\"id\":8,\"name\":\"Blast Tail\",\"type\":\"int\","
         "\"min\":0,\"max\":6,\"default\":2,"
         "\"desc\":\"Spark tail length\"},"
        "{\"id\":9,\"name\":\"Sparks\",\"type\":\"select\","
         "\"options\":[\"Gravity\",\"Float\",\"Rise\"],\"default\":0,"
         "\"desc\":\"Spark behaviour after impact\"}"
    "]}";

EXPORT(get_meta_ptr)
int get_meta_ptr(void) { return (int)META; }
EXPORT(get_meta_len)
int get_meta_len(void) { return sizeof(META) - 1; }

/* ---- PRNG (xorshift32) ---- */
static uint32_t rng_state = 92731;
static uint32_t rng_next(void) {
    uint32_t x = rng_state;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    rng_state = x; return x;
}
static int random_range(int lo, int hi) {
    if (lo >= hi) return lo;
    return lo + (int)(rng_next() % (uint32_t)(hi - lo));
}
static float random_float(void) { return (float)(rng_next() & 0xFFFF) / 65536.0f; }

/* ---- HSV to RGB (hue 0-255, sat 0-255, val 0-255) ---- */
static void hsv_to_rgb(int h, int s, int v, int *r, int *g, int *b) {
    if (s == 0) { *r = *g = *b = v; return; }
    h = h & 0xFF;
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

/* ---- Sin lookup (16 entries over 0..2*PI) ---- */
static const float SIN_TABLE[16] = {
     0.0000f,  0.3827f,  0.7071f,  0.9239f,
     1.0000f,  0.9239f,  0.7071f,  0.3827f,
     0.0000f, -0.3827f, -0.7071f, -0.9239f,
    -1.0000f, -0.9239f, -0.7071f, -0.3827f
};
static float fast_sin(float a) {
    while (a < 0.0f) a += 16.0f;
    while (a >= 16.0f) a -= 16.0f;
    int idx = (int)a; float frac = a - (float)idx;
    return SIN_TABLE[idx] + frac * (SIN_TABLE[(idx + 1) & 15] - SIN_TABLE[idx]);
}
static float fast_cos(float a) { return fast_sin(a + 4.0f); }

/* max horizontal slant (vx/|vy|) for a fall angle in degrees = tan(angle) */
static float angle_to_slant(int deg) {
    float a16 = (float)deg / 360.0f * 16.0f;
    float c = fast_cos(a16);
    if (c < 0.05f) c = 0.05f;
    return fast_sin(a16) / c;
}

/* ---- Constants ---- */
#define MAX_METEORS   12
#define PARTICLES_PER 16
#define MAX_PARTICLES (MAX_METEORS * PARTICLES_PER)

#define PHASE_INACTIVE  0
#define PHASE_FALLING   1
#define PHASE_EXPLODING 2

/* ---- Meteor state ---- */
static int   m_phase[MAX_METEORS];
static float m_x[MAX_METEORS], m_y[MAX_METEORS];
static float m_vx[MAX_METEORS], m_vy[MAX_METEORS];   /* px/sec; vy<0 = down */
static int   m_hue[MAX_METEORS];
static int32_t m_respawn[MAX_METEORS];
static int   m_flash[MAX_METEORS];                   /* impact flash ms left */
static float m_flash_x[MAX_METEORS];

/* shared fall direction (horizontal slant as a fraction of fall speed); it
 * drifts a little on each spawn so consecutive meteors fly roughly alike. */
static float shower_slant;

/* ---- Particle state ---- */
static float p_x[MAX_PARTICLES], p_y[MAX_PARTICLES];
static float p_vx[MAX_PARTICLES], p_vy[MAX_PARTICLES];
static int   p_ttl[MAX_PARTICLES], p_max_ttl[MAX_PARTICLES];
static int   p_hue[MAX_PARTICLES];

static int32_t prev_tick;

EXPORT(init)
void init(void) {
    rng_state = 92731;
    prev_tick = 0;
    shower_slant = (random_float() - 0.5f) * 0.6f;   /* initial diagonal */
    for (int i = 0; i < MAX_METEORS; i++) {
        m_phase[i] = PHASE_INACTIVE;
        m_respawn[i] = random_range(50, 900);
        m_flash[i] = 0;
    }
    for (int i = 0; i < MAX_PARTICLES; i++) p_ttl[i] = 0;
}

/* ---- Spawn a meteor at the top ---- */
static void spawn_meteor(int i, int W, int H, int speed_p, int cmode, float max_slant) {
    m_phase[i] = PHASE_FALLING;
    m_x[i] = (float)random_range(0, W) + 0.5f;
    m_y[i] = (float)H + random_float() * (float)H * 0.4f;  /* enter from above top */

    float spd = (float)H * (0.9f + random_float() * 0.8f) * ((float)speed_p / 50.0f);
    m_vy[i] = -spd;                                         /* downward */

    /* drift the shared slant a little, then add a small per-meteor jitter so
     * each meteor flies roughly like the previous one — bounded by max angle */
    shower_slant += (random_float() - 0.5f) * 0.22f * (max_slant + 0.05f);
    if (shower_slant < -max_slant) shower_slant = -max_slant;
    if (shower_slant >  max_slant) shower_slant =  max_slant;
    float jit = (random_float() - 0.5f) * 0.06f * max_slant;
    m_vx[i] = (shower_slant + jit) * spd;

    if (cmode == 0)      m_hue[i] = random_range(6, 42);    /* Fire */
    else if (cmode == 1) m_hue[i] = random_range(138, 178); /* Icy */
    else                 m_hue[i] = random_range(0, 256);   /* Mixed */
}

/* ---- Impact: splash sparks upward from the ground ---- */
static void explode(int i, int H, float bvel, int nparts) {
    m_phase[i] = PHASE_EXPLODING;
    m_flash[i] = 120;
    m_flash_x[i] = m_x[i];

    if (nparts < 1) nparts = 1;
    if (nparts > PARTICLES_PER) nparts = PARTICLES_PER;

    int base = i * PARTICLES_PER;
    int hue = m_hue[i];
    float cx = m_x[i];

    /* unused particle slots stay dead */
    for (int j = nparts; j < PARTICLES_PER; j++) p_ttl[base + j] = 0;

    for (int j = 0; j < nparts; j++) {
        int pi = base + j;
        p_x[pi] = cx;
        p_y[pi] = 0.5f;                                   /* at the floor */

        /* angle in the upper hemisphere: ~ from 1 to 7 (over 0..16 = 2*PI) */
        float angle = 1.0f + ((float)j / (float)nparts) * 6.0f
                    + (random_float() - 0.5f) * 0.8f;
        float speed = (float)H * (0.5f + random_float() * 0.7f) * bvel;
        float vx = fast_cos(angle) * speed;
        float vy = fast_sin(angle) * speed;              /* sin>0 here -> upward */
        if (vy < 0.0f) vy = -vy;                          /* force splash up */
        vy += (float)H * 0.15f * bvel;                    /* extra upward kick */

        p_vx[pi] = vx;
        p_vy[pi] = vy;
        p_max_ttl[pi] = random_range(650, 1300);
        p_ttl[pi] = p_max_ttl[pi];
        p_hue[pi] = (hue + random_range(-12, 13)) & 0xFF;
    }
}

EXPORT(update)
void update(int tick_ms) {
    int count  = get_param_i32(0);
    int bright = get_param_i32(1);
    int speed  = get_param_i32(2);
    int trail  = get_param_i32(3);
    int cmode  = get_param_i32(4);
    int maxang = get_param_i32(5);
    int bvel   = get_param_i32(6);
    int bsize  = get_param_i32(7);
    int btail  = get_param_i32(8);
    int smode  = get_param_i32(9);      /* 0 gravity, 1 float, 2 rise */
    if (bvel  < 20) bvel  = 100;        /* guards for saves predating these params */
    if (bsize < 1)  bsize = 14;
    if (btail < 0)  btail = 0;
    if (maxang < 0) maxang = 0;
    float bvelf = (float)bvel / 100.0f;
    float max_slant = angle_to_slant(maxang);

    int W = get_width();
    int H = get_height();
    if (W < 1) W = 1;
    if (H < 1) H = 1;
    if (count > MAX_METEORS) count = MAX_METEORS;
    if (count < 1) count = 1;
    if (speed < 10) speed = 10;
    if (trail < 1) trail = 1;

    rng_state ^= (uint32_t)tick_ms;

    int32_t delta_ms = tick_ms - prev_tick;
    if (delta_ms <= 0 || delta_ms > 200) delta_ms = 33;
    prev_tick = tick_ms;
    float dt = (float)delta_ms / 1000.0f;

    /* spark behaviour: pull down (gravity), none (float), or push up (rise) */
    float gravity;
    if (smode == 1)      gravity = 0.0f;
    else if (smode == 2) gravity = -(float)H * 0.55f;   /* anti-gravity: rise away */
    else                 gravity = (float)H * 1.1f;

    /* ---- Update meteors ---- */
    for (int i = 0; i < count; i++) {
        if (m_flash[i] > 0) m_flash[i] -= delta_ms;

        switch (m_phase[i]) {
        case PHASE_INACTIVE:
            m_respawn[i] -= delta_ms;
            if (m_respawn[i] <= 0) spawn_meteor(i, W, H, speed, cmode, max_slant);
            break;

        case PHASE_FALLING:
            m_y[i] += m_vy[i] * dt;
            m_x[i] += m_vx[i] * dt;
            /* wrap horizontally around the cylinder */
            while (m_x[i] < 0.0f)        m_x[i] += (float)W;
            while (m_x[i] >= (float)W)   m_x[i] -= (float)W;
            if (m_y[i] <= 0.0f) {        /* hit the ground */
                m_y[i] = 0.0f;
                explode(i, H, bvelf, bsize);
            }
            break;

        case PHASE_EXPLODING: {
            int base = i * PARTICLES_PER, alive = 0;
            for (int j = 0; j < PARTICLES_PER; j++)
                if (p_ttl[base + j] > 0) { alive = 1; break; }
            if (!alive && m_flash[i] <= 0) {
                m_phase[i] = PHASE_INACTIVE;
                m_respawn[i] = random_range(200, 1600);
            }
            break;
        }
        }
    }

    /* ---- Update particles ---- */
    for (int i = 0; i < count * PARTICLES_PER; i++) {
        if (p_ttl[i] <= 0) continue;
        p_ttl[i] -= delta_ms;
        if (p_ttl[i] <= 0) { p_ttl[i] = 0; continue; }
        p_vy[i] -= gravity * dt;
        p_vx[i] *= 0.97f;
        p_vy[i] *= 0.99f;
        p_x[i] += p_vx[i] * dt;
        p_y[i] += p_vy[i] * dt;
    }

    /* ---- Render ---- */
    for (int x = 0; x < W; x++)
        for (int y = 0; y < H; y++)
            set_pixel(x, y, 0, 0, 0);

    /* meteors: bright head + tail along reverse velocity */
    for (int i = 0; i < count; i++) {
        if (m_phase[i] != PHASE_FALLING) continue;

        float spd = __builtin_sqrtf(m_vx[i]*m_vx[i] + m_vy[i]*m_vy[i]);
        float dx = 0.0f, dy = -1.0f;
        if (spd > 0.001f) { dx = m_vx[i] / spd; dy = m_vy[i] / spd; }

        int r, g, b;
        for (int t = trail; t >= 1; t--) {           /* tail first (dim) */
            float fx = m_x[i] - dx * (float)t;
            float fy = m_y[i] - dy * (float)t;
            int tx = (int)fx, ty = (int)fy;
            while (tx < 0)  tx += W;
            while (tx >= W) tx -= W;
            if (ty < 0 || ty >= H) continue;
            int tv = bright * (trail - t + 1) / (trail + 1);
            tv = tv * (trail - t + 1) / (trail + 1);   /* steeper falloff */
            if (tv < 1) continue;
            hsv_to_rgb(m_hue[i], 210, tv, &r, &g, &b);
            set_pixel(tx, ty, r, g, b);
        }
        /* white-hot head */
        int hx = (int)m_x[i], hy = (int)m_y[i];
        while (hx < 0) hx += W; while (hx >= W) hx -= W;
        if (hy >= 0 && hy < H) {
            hsv_to_rgb(m_hue[i], 50, bright, &r, &g, &b);
            set_pixel(hx, hy, r, g, b);
        }
    }

    /* impact flash: bright spot at the floor */
    for (int i = 0; i < count; i++) {
        if (m_flash[i] <= 0) continue;
        float f = (float)m_flash[i] / 120.0f;
        int val = (int)((float)bright * f);
        if (val < 1) continue;
        int fx = (int)m_flash_x[i];
        while (fx < 0) fx += W; while (fx >= W) fx -= W;
        int r, g, b;
        hsv_to_rgb(m_hue[i], 20, val, &r, &g, &b);   /* near-white flash */
        set_pixel(fx, 0, r, g, b);
        set_pixel((fx + 1) % W, 0, r/2, g/2, b/2);
        set_pixel((fx + W - 1) % W, 0, r/2, g/2, b/2);
        if (H > 1) set_pixel(fx, 1, r/2, g/2, b/2);
    }

    /* splash particles (fade with ttl) + short tail along reverse velocity */
    for (int i = 0; i < count * PARTICLES_PER; i++) {
        if (p_ttl[i] <= 0) continue;
        float life = (float)p_ttl[i] / (float)p_max_ttl[i];
        int val = (int)((float)bright * life);
        if (val < 1) continue;
        if (val > 255) val = 255;
        int r, g, b;

        /* tail: dim streak trailing behind the spark's motion */
        if (btail > 0) {
            float spd = __builtin_sqrtf(p_vx[i]*p_vx[i] + p_vy[i]*p_vy[i]);
            if (spd > 0.001f) {
                float dx = p_vx[i] / spd, dy = p_vy[i] / spd;
                for (int t = btail; t >= 1; t--) {
                    int tx = (int)(p_x[i] - dx * (float)t);
                    int ty = (int)(p_y[i] - dy * (float)t);
                    while (tx < 0) tx += W; while (tx >= W) tx -= W;
                    if (ty < 0 || ty >= H) continue;
                    int tv = val * (btail - t + 1) / (btail + 1);
                    if (tv < 1) continue;
                    hsv_to_rgb(p_hue[i], 230, tv, &r, &g, &b);
                    set_pixel(tx, ty, r, g, b);
                }
            }
        }

        int px = (int)p_x[i], py = (int)p_y[i];
        while (px < 0) px += W; while (px >= W) px -= W;
        if (py < 0 || py >= H) continue;
        hsv_to_rgb(p_hue[i], 220, val, &r, &g, &b);
        set_pixel(px, py, r, g, b);
    }

    draw();
}
