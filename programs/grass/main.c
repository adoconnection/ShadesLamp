#include "api.h"

/*
 * Grass — Field of grass blades swaying in the wind.
 * Each column is a blade with its own height; ambient breeze causes
 * gentle sway, gusts travel left-to-right and bend blades stronger.
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

/* ---- Tiny math (no libm) ---- */
static float my_sinf(float x) {
    /* Reduce to [-pi, pi] */
    const float PI  = 3.14159265f;
    const float TAU = 6.28318530f;
    while (x >  PI) x -= TAU;
    while (x < -PI) x += TAU;
    /* Bhaskara approximation, mirror for negative */
    float ax = x < 0.0f ? -x : x;
    float n = 16.0f * ax * (PI - ax);
    float d = 5.0f * PI * PI - 4.0f * ax * (PI - ax);
    float r = n / d;
    return x < 0.0f ? -r : r;
}

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

/* ---- HSV→RGB ---- */
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

/* ---- Blade state ---- */
#define MAX_W 64

static int   bl_active[MAX_W];      /* 1 if column has a blade */
static int   bl_height[MAX_W];      /* base height in pixels */
static float bl_phase[MAX_W];       /* per-blade sway phase offset */
static int   bl_hue[MAX_W];         /* slight hue variation */
static int   bl_sat_jit[MAX_W];     /* saturation jitter -20..+20 */
static int   bl_val_jit[MAX_W];     /* value jitter -25..+25 */

static int prev_density = -1;
static int prev_W = -1;
static int prev_H = -1;

/* ---- Gust state (up to 3 simultaneous gusts traveling L→R) ---- */
#define MAX_GUSTS 3
static float gust_pos[MAX_GUSTS];    /* x position (float) */
static float gust_speed[MAX_GUSTS];  /* px/sec */
static float gust_width[MAX_GUSTS];  /* horizontal width in px */
static float gust_amp[MAX_GUSTS];    /* peak bend amount in pixels */
static int   gust_active[MAX_GUSTS];

static float gust_cooldown = 0.0f;   /* seconds until next spawn allowed */

/* ---- Timing ---- */
static int32_t prev_tick;
static float ambient_phase;          /* slow time accumulator for ambient sway */

/* ---- Palettes ---- */
/* Returns base hue (0-255) and saturation (0-255) for color mode */
static void palette_for(int mode, int *base_hue, int *base_sat, int *tip_hue) {
    switch (mode) {
    case 0: /* Spring — fresh light green */
        *base_hue = 80; *base_sat = 220; *tip_hue = 70;  break;
    case 1: /* Summer — deep saturated green */
        *base_hue = 90; *base_sat = 235; *tip_hue = 85;  break;
    case 2: /* Autumn — yellow-green to amber tips */
        *base_hue = 60; *base_sat = 220; *tip_hue = 30;  break;
    case 3: /* Meadow — varied green with hue spread */
        *base_hue = 85; *base_sat = 210; *tip_hue = 75;  break;
    default:
        *base_hue = 90; *base_sat = 230; *tip_hue = 85;  break;
    }
}

/* ---- Init blades for a given density / dimensions ---- */
static void init_blades(int W, int H, int density) {
    if (W > MAX_W) W = MAX_W;
    int base_h = H * 60 / 100;        /* 60% of height as base */
    if (base_h < 2) base_h = 2;

    for (int x = 0; x < W; x++) {
        int roll = random_range(0, 100);
        if (roll < density) {
            bl_active[x] = 1;
            int variation = H * 30 / 100 + 1;  /* ±30% */
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
    gust_pos[slot] = -8.0f;                                 /* enter from left */
    gust_speed[slot] = 6.0f + random_float() * 12.0f;       /* 6-18 px/s */
    gust_width[slot] = 4.0f + random_float() * 6.0f;        /* 4-10 px wide */
    /* Gust amplitude scales with height, smaller on tiny displays */
    float h_scale = (float)H / 16.0f;
    if (h_scale < 0.5f) h_scale = 0.5f;
    gust_amp[slot] = (1.5f + random_float() * 2.5f) * h_scale;
}

/* ---- Init ---- */
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

/* ---- Update / Render ---- */
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
    if (density < 1) density = 1;
    if (density > 100) density = 100;

    rng_state ^= (uint32_t)tick_ms * 2654435761u;

    /* Re-init blades on first run or when density/size changes */
    if (density != prev_density || W != prev_W || H != prev_H) {
        init_blades(W, H, density);
        prev_density = density;
        prev_W = W;
        prev_H = H;
    }

    /* Delta time */
    int32_t delta_ms = tick_ms - prev_tick;
    if (delta_ms <= 0 || delta_ms > 200) delta_ms = 33;
    prev_tick = tick_ms;
    float dt = (float)delta_ms / 1000.0f;

    /* Ambient wind: slow phase progression scaled by wind param */
    float wind_strength = (float)wind_p / 100.0f;       /* 0..1 */
    ambient_phase += dt * (0.5f + wind_strength * 1.8f);

    /* ---- Spawn gusts ---- */
    float gust_strength = (float)gusts_p / 100.0f;      /* 0..1 */
    gust_cooldown -= dt;
    if (gust_cooldown <= 0.0f && gust_strength > 0.01f) {
        spawn_gust(H);
        /* Average period: 6s at full → 30s at low */
        float min_period = 4.0f - 2.5f * gust_strength;
        if (min_period < 0.8f) min_period = 0.8f;
        float jitter = random_float() * (4.0f / (gust_strength + 0.2f));
        gust_cooldown = min_period + jitter;
    }

    /* Move existing gusts */
    for (int i = 0; i < MAX_GUSTS; i++) {
        if (!gust_active[i]) continue;
        gust_pos[i] += gust_speed[i] * dt;
        if (gust_pos[i] > (float)(W + 8)) gust_active[i] = 0;
    }

    /* ---- Palette ---- */
    int base_hue, base_sat, tip_hue;
    palette_for(color_p, &base_hue, &base_sat, &tip_hue);

    /* Meadow mode adds wider per-blade hue variance */
    int meadow_extra = (color_p == 3) ? 12 : 0;

    /* ---- Clear ---- */
    for (int x = 0; x < W; x++)
        for (int y = 0; y < H; y++)
            set_pixel(x, y, 0, 0, 0);

    /* ---- Render each blade ---- */
    for (int x = 0; x < W; x++) {
        if (!bl_active[x]) continue;

        int h = bl_height[x];

        /* Ambient sway: small phase-based offset, scaled by wind */
        float ambient_offset = my_sinf(ambient_phase + bl_phase[x])
                                * (0.6f + wind_strength * 1.4f);

        /* Sum gust contributions: gaussian-like falloff around gust_pos */
        float gust_offset = 0.0f;
        for (int g = 0; g < MAX_GUSTS; g++) {
            if (!gust_active[g]) continue;
            float dx = (float)x - gust_pos[g];
            float w = gust_width[g];
            if (w < 1.0f) w = 1.0f;
            float t = dx / w;
            if (t > -3.0f && t < 3.0f) {
                /* Bell shape: 1 / (1 + t^2)^2 — soft, fast falloff */
                float denom = 1.0f + t * t;
                float fall = 1.0f / (denom * denom);
                /* Direction: tilt to the right (positive) as gust passes */
                gust_offset += gust_amp[g] * fall;
            }
        }

        /* Total tip displacement in pixels */
        float total_tip_offset = ambient_offset + gust_offset;

        /* Base color for this blade */
        int blade_hue_jit = bl_hue[x];
        if (meadow_extra) {
            blade_hue_jit += random_range(-meadow_extra, meadow_extra + 1) / 4;
        }

        /* Render blade column-by-column.
         * Each y from 0 to h is bent by (y/h) * total_tip_offset (top bends most). */
        int prev_px = x;
        for (int y = 0; y < h && y < H; y++) {
            float t = (float)y / (float)(h > 1 ? (h - 1) : 1);  /* 0 at root, 1 at tip */
            float bend = t * total_tip_offset;
            int draw_x = x + (int)(bend + (bend >= 0.0f ? 0.5f : -0.5f));
            if (draw_x < 0) draw_x = 0;
            if (draw_x >= W) draw_x = W - 1;

            /* Color gradient: darker at root, brighter at tip; tip hue blends in */
            int hue;
            if (t < 0.5f) {
                hue = base_hue + blade_hue_jit;
            } else {
                /* Interpolate base_hue→tip_hue in upper half */
                float a = (t - 0.5f) * 2.0f;
                hue = base_hue + (int)((float)(tip_hue - base_hue) * a) + blade_hue_jit;
            }

            int sat = base_sat + bl_sat_jit[x];
            if (sat < 80) sat = 80;
            if (sat > 255) sat = 255;

            /* Brightness: 90 at root → 220 at tip */
            int val = 90 + (int)(130.0f * t) + bl_val_jit[x];
            /* Slight extra glow at the very tip when gust hits hard */
            float gust_brightness = my_fabsf(gust_offset) * 12.0f;
            if (gust_brightness > 50.0f) gust_brightness = 50.0f;
            val += (int)(t * gust_brightness);
            if (val < 0) val = 0;
            if (val > 255) val = 255;

            int r, g, b;
            hsv_to_rgb(hue, sat, val, &r, &g, &b);
            set_pixel(draw_x, y, r, g, b);

            /* Fill horizontal gap when bending steeply (avoid broken blade) */
            if (y > 0 && draw_x != prev_px) {
                int gap_x = draw_x > prev_px ? draw_x - 1 : draw_x + 1;
                /* Only fill if it's between the two positions */
                if ((draw_x > prev_px && gap_x > prev_px && gap_x < draw_x) ||
                    (draw_x < prev_px && gap_x < prev_px && gap_x > draw_x)) {
                    int dr, dg, db;
                    hsv_to_rgb(hue, sat, val * 3 / 4, &dr, &dg, &db);
                    if (gap_x >= 0 && gap_x < W) set_pixel(gap_x, y, dr, dg, db);
                }
            }
            prev_px = draw_x;
        }
    }

    draw();
}
