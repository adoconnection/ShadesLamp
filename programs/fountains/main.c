#include "api.h"

/*
 * Color Fountains -- Multiple fountain streams that launch particles upward
 * (Y=0 is bottom). Each stream has a drifting hue. Particles decelerate
 * under gravity, reach a peak, and fall back down while fading out.
 */

/* ---- Metadata JSON ---- */
static const char META[] =
    "{\"name\":\"Color Fountains\","
    "\"desc\":\"Colorful fountain streams that launch upward and fade as they fall\","
    "\"params\":["
        "{\"id\":0,\"name\":\"Fountains\",\"type\":\"int\","
         "\"min\":2,\"max\":6,\"default\":4,"
         "\"desc\":\"Number of fountain sources\"},"
        "{\"id\":1,\"name\":\"Intensity\",\"type\":\"int\","
         "\"min\":1,\"max\":10,\"default\":5,"
         "\"desc\":\"Particles per burst\"},"
        "{\"id\":2,\"name\":\"Brightness\",\"type\":\"int\","
         "\"min\":1,\"max\":255,\"default\":220,"
         "\"desc\":\"Overall brightness\"}"
    "]}";

EXPORT(get_meta_ptr)
int get_meta_ptr(void) { return (int)META; }

EXPORT(get_meta_len)
int get_meta_len(void) { return sizeof(META) - 1; }

/* ---- PRNG (xorshift32) ---- */
static uint32_t rng_state = 73291;

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

/* ---- Particle state ---- */
#define MAX_PARTICLES 30

static float part_x[MAX_PARTICLES];      /* horizontal position */
static float part_y[MAX_PARTICLES];      /* vertical position (Y=0 bottom) */
static float part_vx[MAX_PARTICLES];     /* horizontal velocity */
static float part_vy[MAX_PARTICLES];     /* vertical velocity */
static int   part_hue[MAX_PARTICLES];    /* hue 0-255 */
static int   part_peak_y[MAX_PARTICLES]; /* peak Y reached (for fade calc) */
static int   part_active[MAX_PARTICLES]; /* 1 = alive, 0 = dead */
static int   part_falling[MAX_PARTICLES];/* 1 = already past peak */

/* ---- Fountain source state ---- */
#define MAX_FOUNTAINS 6

static int   fount_x[MAX_FOUNTAINS];    /* fixed X position of each fountain */
static int   fount_hue[MAX_FOUNTAINS];  /* current hue of this fountain */
static int   fount_timer[MAX_FOUNTAINS];/* frames until next burst */

/* ---- Fade framebuffer ---- */
#define MAX_W 64
#define MAX_H 64

static uint8_t fb_r[MAX_W * MAX_H];
static uint8_t fb_g[MAX_W * MAX_H];
static uint8_t fb_b[MAX_W * MAX_H];

/* Saturating subtract for fading */
static uint8_t qsub(uint8_t a, uint8_t b) {
    return a > b ? (uint8_t)(a - b) : 0;
}

/* ---- Timing ---- */
static int32_t prev_tick;
static int initialized;

/* ---- Global hue offset that slowly drifts ---- */
static int global_hue_phase;

EXPORT(init)
void init(void) {
    int W = get_width();
    int H = get_height();
    if (W > MAX_W) W = MAX_W;
    if (H > MAX_H) H = MAX_H;

    /* Clear framebuffer */
    for (int i = 0; i < W * H; i++) {
        fb_r[i] = 0;
        fb_g[i] = 0;
        fb_b[i] = 0;
    }

    /* All particles inactive */
    for (int i = 0; i < MAX_PARTICLES; i++) {
        part_active[i] = 0;
    }

    /* Spread fountain sources evenly across width */
    for (int i = 0; i < MAX_FOUNTAINS; i++) {
        fount_x[i] = 0;
        fount_hue[i] = (i * 256 / MAX_FOUNTAINS) & 0xFF;
        fount_timer[i] = 0;
    }

    prev_tick = 0;
    initialized = 0;
    global_hue_phase = 0;
}

/* Find a free particle slot, return -1 if none */
static int find_free_particle(void) {
    for (int i = 0; i < MAX_PARTICLES; i++) {
        if (!part_active[i]) return i;
    }
    return -1;
}

/* Launch a burst of particles from a fountain */
static void launch_burst(int fx, int hue, int count, int H) {
    for (int j = 0; j < count; j++) {
        int slot = find_free_particle();
        if (slot < 0) return; /* no free slots */

        part_active[slot] = 1;
        part_falling[slot] = 0;

        /* Position: at the fountain X, at ground level */
        part_x[slot] = (float)fx + (float)random_range(-100, 101) / 200.0f;
        part_y[slot] = 0.0f;

        /* Velocity: upward with some variation */
        /* Base velocity scales with matrix height so particles reach ~60-90% of height */
        float base_vy = (float)H * 0.45f + (float)random_range(0, H * 30) / 100.0f;
        part_vy[slot] = base_vy;

        /* Slight horizontal spread */
        part_vx[slot] = (float)random_range(-80, 81) / 100.0f;

        /* Hue: slight variation around fountain's hue */
        part_hue[slot] = (hue + random_range(-15, 16)) & 0xFF;

        /* Peak tracking */
        part_peak_y[slot] = 0;
    }
}

EXPORT(update)
void update(int tick_ms) {
    int num_fountains = get_param_i32(0);
    int intensity     = get_param_i32(1);
    int bright        = get_param_i32(2);

    int W = get_width();
    int H = get_height();
    if (W > MAX_W) W = MAX_W;
    if (H > MAX_H) H = MAX_H;
    if (W < 1) W = 1;
    if (H < 1) H = 1;
    if (num_fountains > MAX_FOUNTAINS) num_fountains = MAX_FOUNTAINS;
    if (num_fountains < 2) num_fountains = 2;

    rng_state ^= (uint32_t)tick_ms;

    /* Compute delta time */
    int32_t delta_ms = tick_ms - prev_tick;
    if (delta_ms <= 0 || delta_ms > 200) delta_ms = 33;
    prev_tick = tick_ms;
    float dt = (float)delta_ms / 1000.0f;

    /* Setup fountain positions on first frame or if count changed */
    if (!initialized) {
        initialized = 1;
        for (int i = 0; i < num_fountains; i++) {
            /* Spread evenly with margin */
            int margin = W / (num_fountains + 1);
            if (margin < 1) margin = 1;
            fount_x[i] = margin * (i + 1);
            if (fount_x[i] >= W) fount_x[i] = W - 1;
            fount_hue[i] = (i * 256 / num_fountains) & 0xFF;
            fount_timer[i] = random_range(3, 15);
        }
    }

    /* Advance global hue phase (slowly rotate all fountain hues) */
    global_hue_phase = (tick_ms / 50) & 0xFF;

    /* Gravity constant: pixels per second^2 */
    float gravity = (float)H * 1.2f;

    /* ---- Fade framebuffer ---- */
    int fade_amount = 18;
    for (int i = 0; i < W * H; i++) {
        fb_r[i] = qsub(fb_r[i], (uint8_t)fade_amount);
        fb_g[i] = qsub(fb_g[i], (uint8_t)fade_amount);
        fb_b[i] = qsub(fb_b[i], (uint8_t)fade_amount);
    }

    /* ---- Update fountain timers and launch bursts ---- */
    for (int f = 0; f < num_fountains; f++) {
        fount_timer[f]--;
        if (fount_timer[f] <= 0) {
            /* Drift hue for this fountain */
            fount_hue[f] = (fount_hue[f] + random_range(5, 20)) & 0xFF;

            /* Combine with global hue phase for slow overall drift */
            int launch_hue = (fount_hue[f] + global_hue_phase) & 0xFF;

            /* Launch a burst */
            int burst_count = random_range(intensity, intensity + 3);
            if (burst_count > 6) burst_count = 6;
            launch_burst(fount_x[f], launch_hue, burst_count, H);

            /* Next burst in 8-20 frames */
            fount_timer[f] = random_range(8, 21);
        }
    }

    /* ---- Update particles ---- */
    for (int i = 0; i < MAX_PARTICLES; i++) {
        if (!part_active[i]) continue;

        /* Apply gravity (decelerates upward motion, then accelerates downward) */
        part_vy[i] -= gravity * dt;

        /* Move */
        part_x[i] += part_vx[i] * dt;
        part_y[i] += part_vy[i] * dt;

        /* Track peak */
        int cur_y_int = (int)part_y[i];
        if (cur_y_int > part_peak_y[i]) {
            part_peak_y[i] = cur_y_int;
        }

        /* Detect when particle starts falling */
        if (part_vy[i] < 0.0f && !part_falling[i]) {
            part_falling[i] = 1;
        }

        /* Kill particle when it falls below ground */
        if (part_y[i] < -0.5f) {
            part_active[i] = 0;
            continue;
        }

        /* Kill if off-screen vertically */
        if (part_y[i] > (float)(H + 2)) {
            part_active[i] = 0;
            continue;
        }

        /* Horizontal wrapping */
        if (part_x[i] < 0.0f) part_x[i] += (float)W;
        if (part_x[i] >= (float)W) part_x[i] -= (float)W;

        /* Compute brightness based on fade state */
        int particle_bright = bright;
        if (part_falling[i] && part_peak_y[i] > 0) {
            /* Fade as particle falls: brightness proportional to current height / peak height */
            float ratio = part_y[i] / (float)part_peak_y[i];
            if (ratio < 0.0f) ratio = 0.0f;
            if (ratio > 1.0f) ratio = 1.0f;
            /* Apply squared falloff for more visible fade */
            particle_bright = (int)((float)bright * ratio * ratio);
            if (particle_bright < 2) {
                part_active[i] = 0;
                continue;
            }
        }

        /* Rising particles: full saturation. At apex: slightly desaturated. */
        int sat = 255;
        if (!part_falling[i] && part_vy[i] < 2.0f) {
            /* Near apex: reduce saturation for a brief white flash */
            sat = 180;
        }

        /* Compute pixel position */
        int px = (int)part_x[i];
        int py = (int)part_y[i];
        if (px < 0) px = 0;
        if (px >= W) px = W - 1;
        if (py < 0) py = 0;
        if (py >= H) py = H - 1;

        /* Convert to RGB */
        int r, g, b;
        hsv_to_rgb(part_hue[i], sat, particle_bright, &r, &g, &b);

        /* Draw into framebuffer (max blend for overlapping particles) */
        int idx = py * W + px;
        if (idx >= 0 && idx < W * H) {
            if (r > fb_r[idx]) fb_r[idx] = (uint8_t)r;
            if (g > fb_g[idx]) fb_g[idx] = (uint8_t)g;
            if (b > fb_b[idx]) fb_b[idx] = (uint8_t)b;
        }

        /* Also draw a dimmer pixel at adjacent Y for more visible streams */
        int py2 = py - 1; /* trail below */
        if (py2 >= 0 && py2 < H) {
            int idx2 = py2 * W + px;
            int tr = r * 2 / 5;
            int tg = g * 2 / 5;
            int tb = b * 2 / 5;
            if (tr > fb_r[idx2]) fb_r[idx2] = (uint8_t)tr;
            if (tg > fb_g[idx2]) fb_g[idx2] = (uint8_t)tg;
            if (tb > fb_b[idx2]) fb_b[idx2] = (uint8_t)tb;
        }
    }

    /* ---- Draw fountain base glow ---- */
    for (int f = 0; f < num_fountains; f++) {
        int fx = fount_x[f];
        int launch_hue = (fount_hue[f] + global_hue_phase) & 0xFF;
        int r, g, b;

        /* Bright center pixel at base */
        hsv_to_rgb(launch_hue, 200, bright * 3 / 4, &r, &g, &b);
        int idx = 0 * W + fx;  /* y=0 row */
        if (fx >= 0 && fx < W && idx < W * H) {
            fb_r[idx] = (uint8_t)r;
            fb_g[idx] = (uint8_t)g;
            fb_b[idx] = (uint8_t)b;
        }

        /* Dimmer glow on neighbors */
        int glow_r = r / 3;
        int glow_g = g / 3;
        int glow_b = b / 3;
        for (int dx = -1; dx <= 1; dx += 2) {
            int nx = fx + dx;
            if (nx >= 0 && nx < W) {
                int nidx = 0 * W + nx;
                if (glow_r > fb_r[nidx]) fb_r[nidx] = (uint8_t)glow_r;
                if (glow_g > fb_g[nidx]) fb_g[nidx] = (uint8_t)glow_g;
                if (glow_b > fb_b[nidx]) fb_b[nidx] = (uint8_t)glow_b;
            }
        }
        /* Also row y=1 */
        int idx1 = 1 * W + fx;
        if (fx >= 0 && fx < W && idx1 < W * H) {
            int r1 = r / 2, g1 = g / 2, b1 = b / 2;
            if (r1 > fb_r[idx1]) fb_r[idx1] = (uint8_t)r1;
            if (g1 > fb_g[idx1]) fb_g[idx1] = (uint8_t)g1;
            if (b1 > fb_b[idx1]) fb_b[idx1] = (uint8_t)b1;
        }
    }

    /* ---- Render framebuffer to display ---- */
    for (int x = 0; x < W; x++) {
        for (int y = 0; y < H; y++) {
            int idx = y * W + x;
            set_pixel(x, y, fb_r[idx], fb_g[idx], fb_b[idx]);
        }
    }

    draw();
}
