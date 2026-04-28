#include "api.h"

/*
 * Fireworks — Rockets launch from bottom, explode into particles.
 * 4 explosion types: burst, chrysanthemum, palm, ring.
 * Y=0 is the bottom of the display.
 */

/* ---- Metadata JSON ---- */
static const char META[] =
    "{\"name\":\"Fireworks\","
    "\"desc\":\"Rockets launch upward and explode into colorful particle bursts\","
    "\"params\":["
        "{\"id\":0,\"name\":\"Count\",\"type\":\"int\","
         "\"min\":1,\"max\":10,\"default\":5,"
         "\"desc\":\"Number of simultaneous fireworks\"},"
        "{\"id\":1,\"name\":\"Brightness\",\"type\":\"int\","
         "\"min\":1,\"max\":255,\"default\":200,"
         "\"desc\":\"Overall brightness\"}"
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

static int random_range(int lo, int hi) {
    if (lo >= hi) return lo;
    return lo + (int)(rng_next() % (uint32_t)(hi - lo));
}

/* Returns a float in [0.0, 1.0) */
static float random_float(void) {
    return (float)(rng_next() & 0xFFFF) / 65536.0f;
}

/* ---- HSV to RGB (hue 0-255, sat 0-255, val 0-255) ---- */
static void hsv_to_rgb(int h, int s, int v, int *r, int *g, int *b) {
    if (s == 0) {
        *r = *g = *b = v;
        return;
    }
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

/* ---- Sin lookup table (16 entries for 0..2*PI) ---- */
static const float SIN_TABLE[16] = {
     0.0000f,  0.3827f,  0.7071f,  0.9239f,
     1.0000f,  0.9239f,  0.7071f,  0.3827f,
     0.0000f, -0.3827f, -0.7071f, -0.9239f,
    -1.0000f, -0.9239f, -0.7071f, -0.3827f
};

/* angle: 0..15 maps to 0..2*PI, with linear interpolation for fractional part */
static float fast_sin(float angle_16) {
    /* Normalize to 0..16 range */
    while (angle_16 < 0.0f)  angle_16 += 16.0f;
    while (angle_16 >= 16.0f) angle_16 -= 16.0f;
    int idx = (int)angle_16;
    float frac = angle_16 - (float)idx;
    int next = (idx + 1) & 15;
    return SIN_TABLE[idx] + frac * (SIN_TABLE[next] - SIN_TABLE[idx]);
}

static float fast_cos(float angle_16) {
    return fast_sin(angle_16 + 4.0f); /* cos = sin shifted by PI/2 = 4 entries */
}

/* ---- Constants ---- */
#define MAX_FIREWORKS 10
#define PARTICLES_PER 20
#define MAX_PARTICLES (MAX_FIREWORKS * PARTICLES_PER)

#define PHASE_INACTIVE 0
#define PHASE_ROCKET   1
#define PHASE_EXPLODING 2

/* Explosion types */
#define EXPL_BURST        0
#define EXPL_CHRYSANTHEMUM 1
#define EXPL_PALM         2
#define EXPL_RING         3

/* ---- Firework state ---- */
static int   fw_phase[MAX_FIREWORKS];        /* 0=inactive, 1=rocket, 2=exploding */
static float fw_x[MAX_FIREWORKS];            /* rocket X position */
static float fw_y[MAX_FIREWORKS];            /* rocket Y position */
static float fw_target_y[MAX_FIREWORKS];     /* target explosion height */
static float fw_speed[MAX_FIREWORKS];        /* rocket upward speed */
static int   fw_hue[MAX_FIREWORKS];          /* base hue */
static int   fw_expl_type[MAX_FIREWORKS];    /* explosion type 0-3 */
static int32_t fw_respawn_timer[MAX_FIREWORKS]; /* ms until respawn */

/* ---- Particle state ---- */
static float p_x[MAX_PARTICLES];
static float p_y[MAX_PARTICLES];
static float p_vx[MAX_PARTICLES];
static float p_vy[MAX_PARTICLES];
static int   p_ttl[MAX_PARTICLES];       /* remaining life in ms */
static int   p_max_ttl[MAX_PARTICLES];   /* initial life for fade calc */
static int   p_hue[MAX_PARTICLES];

/* ---- Timing ---- */
static int32_t prev_tick;

EXPORT(init)
void init(void) {
    rng_state = 92731;
    prev_tick = 0;
    for (int i = 0; i < MAX_FIREWORKS; i++) {
        fw_phase[i] = PHASE_INACTIVE;
        fw_respawn_timer[i] = random_range(100, 800);
    }
    for (int i = 0; i < MAX_PARTICLES; i++) {
        p_ttl[i] = 0;
    }
}

/* ---- Spawn a rocket ---- */
static void spawn_rocket(int i, int W, int H) {
    fw_phase[i] = PHASE_ROCKET;
    fw_x[i] = (float)random_range(1, W - 1) + 0.5f;
    fw_y[i] = 0.0f;
    fw_target_y[i] = (float)H * (0.5f + random_float() * 0.45f);
    fw_speed[i] = (float)H * (0.8f + random_float() * 0.6f); /* pixels/sec */
    fw_hue[i] = random_range(0, 256);
    fw_expl_type[i] = random_range(0, 4);
}

/* ---- Trigger explosion ---- */
static void explode(int i) {
    fw_phase[i] = PHASE_EXPLODING;
    int base = i * PARTICLES_PER;
    int type = fw_expl_type[i];
    int hue = fw_hue[i];
    float cx = fw_x[i];
    float cy = fw_y[i];

    for (int j = 0; j < PARTICLES_PER; j++) {
        int pi = base + j;
        p_x[pi] = cx;
        p_y[pi] = cy;

        /* Angle: evenly distributed + slight randomness */
        float angle = (float)j * (16.0f / (float)PARTICLES_PER)
                    + (random_float() - 0.5f) * 1.0f;

        float speed;
        float vx_out, vy_out;

        switch (type) {
        case EXPL_BURST:
            speed = 15.0f + random_float() * 20.0f;
            vx_out = fast_cos(angle) * speed;
            vy_out = fast_sin(angle) * speed;
            p_max_ttl[pi] = random_range(400, 800);
            break;

        case EXPL_CHRYSANTHEMUM:
            speed = 25.0f + random_float() * 15.0f;
            vx_out = fast_cos(angle) * speed;
            vy_out = fast_sin(angle) * speed;
            p_max_ttl[pi] = random_range(800, 1400);
            break;

        case EXPL_PALM:
            speed = 18.0f + random_float() * 12.0f;
            vx_out = fast_cos(angle) * speed * 0.7f;
            vy_out = fast_sin(angle) * speed;
            if (vy_out < 0) vy_out *= 0.3f; /* suppress downward */
            vy_out += 8.0f; /* bias upward */
            p_max_ttl[pi] = random_range(600, 1100);
            break;

        case EXPL_RING:
            speed = 20.0f + random_float() * 8.0f;
            vx_out = fast_cos(angle) * speed;
            vy_out = fast_sin(angle) * speed * 0.15f; /* squash vertical */
            p_max_ttl[pi] = random_range(500, 900);
            break;

        default:
            speed = 15.0f;
            vx_out = fast_cos(angle) * speed;
            vy_out = fast_sin(angle) * speed;
            p_max_ttl[pi] = 600;
            break;
        }

        p_vx[pi] = vx_out;
        p_vy[pi] = vy_out;
        p_ttl[pi] = p_max_ttl[pi];
        p_hue[pi] = (hue + random_range(-15, 16)) & 0xFF;
    }
}

EXPORT(update)
void update(int tick_ms) {
    int count   = get_param_i32(0);  /* 1-10 */
    int bright  = get_param_i32(1);  /* 1-255 */

    int W = get_width();
    int H = get_height();
    if (W < 1) W = 1;
    if (H < 1) H = 1;
    if (count > MAX_FIREWORKS) count = MAX_FIREWORKS;
    if (count < 1) count = 1;

    rng_state ^= (uint32_t)tick_ms;

    /* Delta time */
    int32_t delta_ms = tick_ms - prev_tick;
    if (delta_ms <= 0 || delta_ms > 200) delta_ms = 33;
    prev_tick = tick_ms;
    float dt = (float)delta_ms / 1000.0f;

    /* Gravity: pixels per second per second */
    float gravity = (float)H * 0.8f;

    /* ---- Update fireworks ---- */
    for (int i = 0; i < count; i++) {
        switch (fw_phase[i]) {
        case PHASE_INACTIVE:
            fw_respawn_timer[i] -= delta_ms;
            if (fw_respawn_timer[i] <= 0) {
                spawn_rocket(i, W, H);
            }
            break;

        case PHASE_ROCKET:
            /* Move rocket upward */
            fw_y[i] += fw_speed[i] * dt;
            /* Slight horizontal wobble */
            fw_x[i] += (random_float() - 0.5f) * 1.5f * dt;
            /* Clamp X */
            if (fw_x[i] < 0.0f) fw_x[i] = 0.0f;
            if (fw_x[i] >= (float)W) fw_x[i] = (float)(W - 1);

            /* Check if reached target */
            if (fw_y[i] >= fw_target_y[i]) {
                fw_y[i] = fw_target_y[i];
                explode(i);
            }
            break;

        case PHASE_EXPLODING: {
            /* Check if all particles of this firework are dead */
            int base = i * PARTICLES_PER;
            int alive = 0;
            for (int j = 0; j < PARTICLES_PER; j++) {
                int pi = base + j;
                if (p_ttl[pi] > 0) {
                    alive = 1;
                    break;
                }
            }
            if (!alive) {
                fw_phase[i] = PHASE_INACTIVE;
                fw_respawn_timer[i] = random_range(300, 1500);
            }
            break;
        }
        }
    }

    /* ---- Update particles ---- */
    for (int i = 0; i < count * PARTICLES_PER; i++) {
        if (p_ttl[i] <= 0) continue;

        p_ttl[i] -= delta_ms;
        if (p_ttl[i] <= 0) {
            p_ttl[i] = 0;
            continue;
        }

        /* Physics */
        p_vy[i] -= gravity * dt;     /* gravity pulls down */
        p_vx[i] *= 0.98f;            /* air drag */
        p_vy[i] *= 0.98f;
        p_x[i] += p_vx[i] * dt;
        p_y[i] += p_vy[i] * dt;
    }

    /* ---- Render ---- */
    /* Clear display */
    for (int x = 0; x < W; x++)
        for (int y = 0; y < H; y++)
            set_pixel(x, y, 0, 0, 0);

    /* Draw rockets */
    for (int i = 0; i < count; i++) {
        if (fw_phase[i] != PHASE_ROCKET) continue;

        int rx = (int)fw_x[i];
        int ry = (int)fw_y[i];
        if (rx < 0 || rx >= W || ry < 0 || ry >= H) continue;

        /* Bright rocket head */
        int r, g, b;
        hsv_to_rgb(fw_hue[i], 120, bright, &r, &g, &b);
        set_pixel(rx, ry, r, g, b);

        /* Dim trail below */
        int trail_len = 3;
        for (int t = 1; t <= trail_len; t++) {
            int ty = ry - t;
            if (ty < 0) break;
            int tv = bright / (t + 2);
            if (tv < 1) break;
            hsv_to_rgb(fw_hue[i], 80, tv, &r, &g, &b);
            set_pixel(rx, ty, r, g, b);
        }
    }

    /* Draw particles (older ttl = dimmer) */
    for (int i = 0; i < count * PARTICLES_PER; i++) {
        if (p_ttl[i] <= 0) continue;

        int px = (int)p_x[i];
        int py = (int)p_y[i];
        if (px < 0 || px >= W || py < 0 || py >= H) continue;

        /* Brightness fades with TTL */
        float life_frac = (float)p_ttl[i] / (float)p_max_ttl[i];
        int val = (int)((float)bright * life_frac);
        if (val < 1) continue;
        if (val > 255) val = 255;

        int r, g, b;
        hsv_to_rgb(p_hue[i], 220, val, &r, &g, &b);
        set_pixel(px, py, r, g, b);
    }

    draw();
}
