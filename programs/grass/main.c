#include "api.h"

/*
 * Grass — field of grass blades swaying in the wind.
 * Each column is a blade with its own height; ambient breeze causes a gentle
 * sway and gusts travel left-to-right, bending blades harder as they pass.
 * Blades are drawn into an RGB framebuffer as anti-aliased poly-lines
 * (m_line, one short segment per height level) so the bending is smooth and
 * sub-pixel instead of snapping the tip cell-to-cell.
 * Y=0 is the BOTTOM (roots), Y=H-1 is the top (tips).
 */

static const char META[] =
    "{\"name\":\"Grass\","
    "\"desc\":\"Grass blades swaying in the wind with gusts\","
    "\"params\":["
        "{\"id\":0,\"name\":\"Density\",\"type\":\"int\","
         "\"min\":30,\"max\":100,\"default\":80,"
         "\"desc\":\"Percent of columns with blades\"},"
        "{\"id\":1,\"name\":\"Wind\",\"type\":\"int\","
         "\"min\":0,\"max\":100,\"default\":50,"
         "\"desc\":\"Ambient wind strength\"},"
        "{\"id\":2,\"name\":\"Gusts\",\"type\":\"int\","
         "\"min\":0,\"max\":100,\"default\":60,"
         "\"desc\":\"Frequency and power of gusts\"},"
        "{\"id\":3,\"name\":\"Color\",\"type\":\"select\","
         "\"options\":[\"Spring\",\"Summer\",\"Autumn\",\"Meadow\"],"
         "\"default\":1,"
         "\"desc\":\"Grass palette\"}"
    "]}";

EXPORT(get_meta_ptr)
int get_meta_ptr(void) { return (int)META; }

EXPORT(get_meta_len)
int get_meta_len(void) { return sizeof(META) - 1; }

/* ---- Tiny math ---- */
static float my_fabsf(float x) { return x < 0.0f ? -x : x; }

/* ---- PRNG ---- */
static uint32_t rng_state = 0xC0FFEEu;

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

static float random_float(void) {
    return (float)(rng_next() & 0xFFFF) / 65536.0f;
}

/* ---- packed colour ---- */
static int packrgb(int r, int g, int b) {
    if (r < 0) r = 0; if (r > 255) r = 255;
    if (g < 0) g = 0; if (g > 255) g = 255;
    if (b < 0) b = 0; if (b > 255) b = 255;
    return (r << 16) | (g << 8) | b;
}

/* ---- Framebuffer ---- */
#define MAX_W 64
#define MAX_H 64
static uint8_t FB[MAX_W * MAX_H * 3];
EXPORT(get_framebuffer)
int get_framebuffer(void) { return (int)FB; }

/* ---- Blade state ---- */
static int   bl_active[MAX_W];      /* 1 if column has a blade */
static int   bl_height[MAX_W];      /* base height in pixels */
static float bl_phase[MAX_W];       /* per-blade sway phase offset */
static int   bl_hue[MAX_W];         /* slight hue variation */
static int   bl_sat_jit[MAX_W];     /* saturation jitter -20..+20 */
static int   bl_val_jit[MAX_W];     /* value jitter -25..+25 */

static int prev_density = -1;
static int prev_W = -1;
static int prev_H = -1;

/* ---- Gust state (up to 3 simultaneous gusts traveling L->R) ---- */
#define MAX_GUSTS 3
static float gust_pos[MAX_GUSTS];
static float gust_speed[MAX_GUSTS];
static float gust_width[MAX_GUSTS];
static float gust_amp[MAX_GUSTS];
static int   gust_active[MAX_GUSTS];

static float gust_cooldown = 0.0f;

/* ---- Timing ---- */
static int32_t prev_tick;
static float ambient_phase;

/* ---- Palettes ---- */
static void palette_for(int mode, int *base_hue, int *base_sat, int *tip_hue) {
    switch (mode) {
    case 0: *base_hue = 80; *base_sat = 220; *tip_hue = 70;  break; /* Spring */
    case 1: *base_hue = 90; *base_sat = 235; *tip_hue = 85;  break; /* Summer */
    case 2: *base_hue = 60; *base_sat = 220; *tip_hue = 30;  break; /* Autumn */
    case 3: *base_hue = 85; *base_sat = 210; *tip_hue = 75;  break; /* Meadow */
    default:*base_hue = 90; *base_sat = 230; *tip_hue = 85;  break;
    }
}

/* ---- Init blades for a given density / dimensions ---- */
static void init_blades(int W, int H, int density) {
    if (W > MAX_W) W = MAX_W;
    int base_h = H * 60 / 100;
    if (base_h < 2) base_h = 2;

    for (int x = 0; x < W; x++) {
        int roll = random_range(0, 100);
        if (roll < density) {
            bl_active[x] = 1;
            int variation = H * 30 / 100 + 1;
            int delta = random_range(0, variation) - variation / 2;
            int h = base_h + delta;
            if (h < 2) h = 2;
            if (h > H) h = H;
            bl_height[x] = h;
            bl_phase[x] = random_float() * 6.28318530f;
            bl_hue[x] = random_range(-8, 9);
            bl_sat_jit[x] = random_range(-20, 21);
            bl_val_jit[x] = random_range(-25, 26);
        } else {
            bl_active[x] = 0;
        }
    }
}

/* ---- Spawn a new gust ---- */
static void spawn_gust(int H) {
    int slot = -1;
    for (int i = 0; i < MAX_GUSTS; i++) {
        if (!gust_active[i]) { slot = i; break; }
    }
    if (slot < 0) return;
    gust_active[slot] = 1;
    gust_pos[slot] = -8.0f;
    gust_speed[slot] = 6.0f + random_float() * 12.0f;
    gust_width[slot] = 4.0f + random_float() * 6.0f;
    float h_scale = (float)H / 16.0f;
    if (h_scale < 0.5f) h_scale = 0.5f;
    gust_amp[slot] = (1.5f + random_float() * 2.5f) * h_scale;
}

EXPORT(init)
void init(void) {
    rng_state = 0xC0FFEEu;
    prev_tick = 0;
    ambient_phase = 0.0f;
    gust_cooldown = 1.0f;
    for (int i = 0; i < MAX_GUSTS; i++) gust_active[i] = 0;
    for (int i = 0; i < MAX_W; i++) bl_active[i] = 0;
    prev_density = -1;
    prev_W = -1;
    prev_H = -1;
}

EXPORT(update)
void update(int tick_ms) {
    int density   = get_param_i32(0);
    int wind_p    = get_param_i32(1);
    int gusts_p   = get_param_i32(2);
    int color_p   = get_param_i32(3);

    int W = get_width();
    int H = get_height();
    if (W < 1) W = 1;
    if (H < 1) H = 1;
    if (W > MAX_W) W = MAX_W;
    if (H > MAX_H) H = MAX_H;
    if (density < 1) density = 1;
    if (density > 100) density = 100;

    rng_state ^= (uint32_t)tick_ms * 2654435761u;

    if (density != prev_density || W != prev_W || H != prev_H) {
        init_blades(W, H, density);
        prev_density = density;
        prev_W = W;
        prev_H = H;
    }

    int32_t delta_ms = tick_ms - prev_tick;
    if (delta_ms <= 0 || delta_ms > 200) delta_ms = 33;
    prev_tick = tick_ms;
    float dt = (float)delta_ms / 1000.0f;

    /* Ambient wind */
    float wind_strength = (float)wind_p / 100.0f;
    ambient_phase += dt * (0.5f + wind_strength * 1.8f);

    /* Spawn gusts */
    float gust_strength = (float)gusts_p / 100.0f;
    gust_cooldown -= dt;
    if (gust_cooldown <= 0.0f && gust_strength > 0.01f) {
        spawn_gust(H);
        float min_period = 4.0f - 2.5f * gust_strength;
        if (min_period < 0.8f) min_period = 0.8f;
        float jitter = random_float() * (4.0f / (gust_strength + 0.2f));
        gust_cooldown = min_period + jitter;
    }

    for (int i = 0; i < MAX_GUSTS; i++) {
        if (!gust_active[i]) continue;
        gust_pos[i] += gust_speed[i] * dt;
        if (gust_pos[i] > (float)(W + 8)) gust_active[i] = 0;
    }

    /* Palette */
    int base_hue, base_sat, tip_hue;
    palette_for(color_p, &base_hue, &base_sat, &tip_hue);
    int meadow_extra = (color_p == 3) ? 12 : 0;

    /* Clear framebuffer */
    for (int i = 0; i < W * H * 3; i++) FB[i] = 0;

    /* ---- Render each blade as an anti-aliased poly-line ---- */
    for (int x = 0; x < W; x++) {
        if (!bl_active[x]) continue;

        int h = bl_height[x];

        /* Ambient sway (px of tip displacement), scaled by wind */
        float ambient_offset = m_sin(ambient_phase + bl_phase[x])
                                * (0.6f + wind_strength * 1.4f);

        /* Gust contribution: soft bell falloff around each gust position */
        float gust_offset = 0.0f;
        for (int g = 0; g < MAX_GUSTS; g++) {
            if (!gust_active[g]) continue;
            float dx = (float)x - gust_pos[g];
            float w = gust_width[g];
            if (w < 1.0f) w = 1.0f;
            float t = dx / w;
            if (t > -3.0f && t < 3.0f) {
                float denom = 1.0f + t * t;
                gust_offset += gust_amp[g] / (denom * denom);
            }
        }

        float total_tip_offset = ambient_offset + gust_offset;

        int blade_hue_jit = bl_hue[x];
        if (meadow_extra) {
            blade_hue_jit += random_range(-meadow_extra, meadow_extra + 1) / 4;
        }

        int sat = base_sat + bl_sat_jit[x];
        if (sat < 80) sat = 80;
        if (sat > 255) sat = 255;

        float gust_brightness = my_fabsf(gust_offset) * 12.0f;
        if (gust_brightness > 50.0f) gust_brightness = 50.0f;

        /* Blade base sits at the centre of its column. Bend follows a t^2
         * curve so the blade arcs (straight at the root, most bent at the tip). */
        float bx = (float)x + 0.5f;
        float denom_h = (float)(h > 1 ? (h - 1) : 1);
        float prev_x = bx;
        float prev_y = 0.0f;

        for (int y = 1; y < h && y < H; y++) {
            float t = (float)y / denom_h;            /* 0 root .. 1 tip */
            float bend = t * t * total_tip_offset;
            float px = bx + bend;
            float py = (float)y;

            /* Colour gradient: darker green root -> brighter, tip hue in upper half */
            int hue;
            if (t < 0.5f) {
                hue = base_hue + blade_hue_jit;
            } else {
                float a = (t - 0.5f) * 2.0f;
                hue = base_hue + (int)((float)(tip_hue - base_hue) * a) + blade_hue_jit;
            }

            int val = 90 + (int)(130.0f * t) + bl_val_jit[x];
            val += (int)(t * gust_brightness);
            if (val < 0) val = 0;
            if (val > 255) val = 255;

            int c = m_hsv(hue & 0xFF, sat, val);
            int rgb = packrgb((c >> 16) & 255, (c >> 8) & 255, c & 255);

            /* one short anti-aliased segment for this height level */
            m_line(FB, W, H, prev_x, prev_y, px, py, rgb);

            prev_x = px;
            prev_y = py;
        }
    }

    draw();
}
