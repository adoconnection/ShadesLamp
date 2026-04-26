#include "api.h"

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

/* ---- HSV to RGB ---- */
static void hsv2rgb(int hue, int sat, int val, int *r, int *g, int *b) {
    if (val == 0) { *r = *g = *b = 0; return; }
    if (sat == 0) { *r = *g = *b = val; return; }
    int h = hue & 0xFF; int region = h / 43; int frac = (h - region * 43) * 6;
    int p = (val * (255 - sat)) >> 8; int q = (val * (255 - ((sat * frac) >> 8))) >> 8;
    int t = (val * (255 - ((sat * (255 - frac)) >> 8))) >> 8;
    switch (region) {
        case 0: *r=val;*g=t;*b=p;break; case 1: *r=q;*g=val;*b=p;break;
        case 2: *r=p;*g=val;*b=t;break; case 3: *r=p;*g=q;*b=val;break;
        case 4: *r=t;*g=p;*b=val;break; default: *r=val;*g=p;*b=q;break;
    }
}

/* ---- Math helpers ---- */
#define TWO_PI 6.28318530f
#define PI 3.14159265f
#define HALF_PI 1.57079632f
static float fsin(float x) {
    while (x < 0.0f) x += TWO_PI; while (x >= TWO_PI) x -= TWO_PI;
    float sign = 1.0f; if (x > PI) { x -= PI; sign = -1.0f; }
    float num = 16.0f * x * (PI - x);
    float den = 5.0f * PI * PI - 4.0f * x * (PI - x);
    if (den == 0.0f) return 0.0f; return sign * num / den;
}
static float fcos(float x) { return fsin(x + HALF_PI); }

static float fabs_f(float x) { return x < 0.0f ? -x : x; }

/* ---- Jellyfish state ---- */
#define MAX_JELLY 5

static float jf_x[MAX_JELLY];      /* horizontal position (float, wraps) */
static float jf_y[MAX_JELLY];      /* vertical position (float, moves up) */
static int   jf_size[MAX_JELLY];   /* bell width (3-6) */
static float jf_phase[MAX_JELLY];  /* pulsing phase */
static float jf_drift[MAX_JELLY];  /* horizontal drift phase */
static int   jf_hue_off[MAX_JELLY];/* per-jellyfish hue variation */

static int cur_w, cur_h;
static uint32_t tick_total;

/* Plankton sparkle state */
#define MAX_SPARKLE 12
static int sparkle_x[MAX_SPARKLE];
static int sparkle_y[MAX_SPARKLE];
static int sparkle_life[MAX_SPARKLE];

static void spawn_jellyfish(int i) {
    jf_x[i] = (float)rand_range(0, cur_w);
    jf_y[i] = (float)(cur_h + rand_range(2, 8));  /* start below bottom */
    jf_size[i] = rand_range(3, 7);  /* bell width 3-6 */
    jf_phase[i] = (float)rand_range(0, 628) / 100.0f;
    jf_drift[i] = (float)rand_range(0, 628) / 100.0f;
    jf_hue_off[i] = rand_range(-15, 16);
}

static void spawn_sparkle(int i) {
    sparkle_x[i] = rand_range(0, cur_w);
    sparkle_y[i] = rand_range(0, cur_h);
    sparkle_life[i] = rand_range(3, 15);
}

/* Clamp int to 0-255 */
static int clamp255(int v) {
    if (v < 0) return 0;
    if (v > 255) return 255;
    return v;
}

/* Wrap X coordinate for cylinder */
static int wrapx(int x) {
    while (x < 0) x += cur_w;
    while (x >= cur_w) x -= cur_w;
    return x;
}

/* Set a pixel with additive blending, clamped */
static void set_pixel_add(int x, int y, int r, int g, int b) {
    if (y < 0 || y >= cur_h) return;
    x = wrapx(x);
    set_pixel(x, y, clamp255(r), clamp255(g), clamp255(b));
}

EXPORT(init)
void init(void) {
    cur_w = get_width();
    cur_h = get_height();
    if (cur_w < 1) cur_w = 1;
    if (cur_h < 1) cur_h = 1;
    tick_total = 0;

    for (int i = 0; i < MAX_JELLY; i++) {
        spawn_jellyfish(i);
        /* Spread initial positions vertically so they don't all start together */
        jf_y[i] = (float)rand_range(0, cur_h);
    }

    for (int i = 0; i < MAX_SPARKLE; i++) {
        spawn_sparkle(i);
    }
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
    if (count < 1) count = 1;
    if (count > MAX_JELLY) count = MAX_JELLY;

    rng ^= (uint32_t)tick_ms;
    tick_total += (uint32_t)tick_ms;

    float dt = (float)tick_ms / 1000.0f;
    float spd = (float)speed / 25.0f;  /* normalize: 25 -> 1.0 */

    /* ---- Clear to dark deep blue background ---- */
    {
        /* Slight vertical gradient: darker at top, slightly brighter at bottom */
        for (int x = 0; x < cur_w; x++) {
            for (int y = 0; y < cur_h; y++) {
                int bg_b = 3 + (y * 5) / (cur_h > 1 ? cur_h : 1);
                int bg_g = 1 + (y * 2) / (cur_h > 1 ? cur_h : 1);
                bg_b = bg_b * bright / 255;
                bg_g = bg_g * bright / 255;
                set_pixel(x, y, 0, bg_g, bg_b);
            }
        }
    }

    /* ---- Update and draw plankton sparkles ---- */
    for (int i = 0; i < MAX_SPARKLE; i++) {
        sparkle_life[i]--;
        if (sparkle_life[i] <= 0) {
            spawn_sparkle(i);
        }
        /* Only draw some sparkles at a time for a subtle effect */
        if (sparkle_life[i] > 0 && sparkle_life[i] < 8) {
            int sv = (sparkle_life[i] * 12) * bright / 255;
            if (sv > 0) {
                int r, g, b;
                /* Dim cyan-white plankton */
                hsv2rgb((base_hue + 20) & 0xFF, 80, sv, &r, &g, &b);
                set_pixel_add(sparkle_x[i], sparkle_y[i], r, g, b);
            }
        }
    }

    /* ---- Update and draw jellyfish ---- */
    for (int j = 0; j < count; j++) {
        /* Advance phases */
        jf_phase[j] += dt * 2.5f * spd;
        jf_drift[j] += dt * 0.7f * spd;

        /* Pulse factor: oscillates bell size slightly */
        float pulse = fsin(jf_phase[j]);  /* -1 to 1 */
        float pulse01 = 0.5f + 0.5f * pulse;  /* 0 to 1 */

        /* Move upward */
        float rise_speed = (1.5f + (float)jf_size[j] * 0.3f) * spd;
        /* Slower during "contraction", faster during "expansion" (like real jellyfish) */
        float swim_boost = 1.0f + 0.5f * pulse;
        jf_y[j] -= rise_speed * swim_boost * dt;

        /* Horizontal drift: gentle sine wave */
        float drift_amount = fsin(jf_drift[j]) * 0.8f * spd;
        jf_x[j] += drift_amount * dt;

        /* Wrap X */
        while (jf_x[j] < 0.0f) jf_x[j] += (float)cur_w;
        while (jf_x[j] >= (float)cur_w) jf_x[j] -= (float)cur_w;

        /* Respawn if exited top */
        if (jf_y[j] < -(float)jf_size[j] - 6) {
            spawn_jellyfish(j);
        }

        /* ---- Draw the jellyfish ---- */
        int cx = (int)jf_x[j];
        int cy = (int)jf_y[j];
        int bell_w = jf_size[j];  /* half-width of bell */
        int bell_h = (bell_w * 2) / 3;  /* bell height */
        if (bell_h < 2) bell_h = 2;

        int hue = (base_hue + jf_hue_off[j]) & 0xFF;

        /* Pulsing brightness */
        int pulse_bright = (int)((float)bright * (0.6f + 0.4f * pulse01));

        /* -- Bell/dome: draw an approximate semicircle -- */
        /* For each row of the bell (from top of bell to bottom) */
        for (int dy = -bell_h; dy <= 0; dy++) {
            int py = cy + dy;
            if (py < 0 || py >= cur_h) continue;

            /* Width at this row: semicircle profile */
            /* At dy=0 (bottom of bell): full width. At dy=-bell_h (top): 0 */
            float t_frac = 1.0f - (float)(-dy) / (float)bell_h;  /* 0 at top, 1 at bottom */
            /* Semicircle: width = bell_w * sqrt(1 - (1-t)^2) roughly */
            float norm = 1.0f - t_frac;
            float half_span = (float)bell_w * (1.0f - norm * norm);
            /* Apply pulse: slightly contract/expand */
            half_span *= (0.85f + 0.15f * pulse01);

            int hw = (int)(half_span + 0.5f);
            if (hw < 0) hw = 0;

            for (int dx = -hw; dx <= hw; dx++) {
                int px = cx + dx;

                /* Brightness falloff: brighter in center, dimmer at edges */
                float dist = fabs_f((float)dx) / (half_span > 0.5f ? half_span : 0.5f);
                float edge_dim = 1.0f - dist * dist;
                if (edge_dim < 0.0f) edge_dim = 0.0f;

                /* Vertical brightness: brighter at top of bell */
                float vert_bright = 0.5f + 0.5f * (1.0f - t_frac);

                int val = (int)((float)pulse_bright * edge_dim * vert_bright);
                if (val < 1) continue;
                if (val > 255) val = 255;

                /* Slightly less saturated at center for glow effect */
                int sat = 200 + (int)(55.0f * dist);
                if (sat > 255) sat = 255;

                int r, g, b;
                hsv2rgb(hue, sat, val, &r, &g, &b);
                set_pixel_add(px, py, r, g, b);
            }
        }

        /* -- Bell rim: brighter line at bottom of bell -- */
        {
            float rim_span = (float)bell_w * (0.85f + 0.15f * pulse01);
            int rim_hw = (int)(rim_span + 0.5f);
            int rim_y = cy;
            if (rim_y >= 0 && rim_y < cur_h) {
                for (int dx = -rim_hw; dx <= rim_hw; dx++) {
                    float dist = fabs_f((float)dx) / (rim_span > 0.5f ? rim_span : 0.5f);
                    int val = (int)((float)pulse_bright * (1.0f - dist * 0.3f));
                    if (val > 255) val = 255;
                    if (val < 1) continue;
                    int r, g, b;
                    hsv2rgb(hue, 180, val, &r, &g, &b);
                    set_pixel_add(cx + dx, rim_y, r, g, b);
                }
            }
        }

        /* -- Tentacles: wavy lines hanging below the bell -- */
        int num_tentacles = bell_w;  /* roughly one per pixel of width */
        if (num_tentacles < 2) num_tentacles = 2;
        if (num_tentacles > 6) num_tentacles = 6;

        int tent_len = bell_w + 2 + (int)(2.0f * pulse01);  /* longer during expansion */

        for (int ti = 0; ti < num_tentacles; ti++) {
            /* Tentacle attachment point spread across bell bottom */
            float attach_offset;
            if (num_tentacles > 1) {
                attach_offset = (float)bell_w * (-1.0f + 2.0f * (float)ti / (float)(num_tentacles - 1));
            } else {
                attach_offset = 0.0f;
            }
            attach_offset *= (0.85f + 0.15f * pulse01);

            float tent_phase = jf_phase[j] * 0.8f + (float)ti * 1.3f;

            for (int seg = 1; seg <= tent_len; seg++) {
                int ty = cy + seg;
                if (ty < 0 || ty >= cur_h) continue;

                /* Horizontal wave for tentacle */
                float wave_amp = 0.5f + (float)seg * 0.15f;
                float wave_x = fsin(tent_phase + (float)seg * 0.7f) * wave_amp;

                int tx = cx + (int)(attach_offset + wave_x);

                /* Dimmer further from bell */
                float fade = 1.0f - (float)seg / (float)(tent_len + 1);
                fade = fade * fade;  /* quadratic falloff */
                int val = (int)((float)(pulse_bright / 2) * fade);
                if (val < 2) continue;
                if (val > 255) val = 255;

                int r, g, b;
                hsv2rgb(hue, 220, val, &r, &g, &b);
                set_pixel_add(tx, ty, r, g, b);
            }
        }

        /* -- Inner glow: a bright spot near the center of the bell -- */
        {
            int glow_y = cy - bell_h / 2;
            if (glow_y >= 0 && glow_y < cur_h) {
                int glow_val = (int)((float)pulse_bright * (0.7f + 0.3f * pulse01));
                if (glow_val > 255) glow_val = 255;
                int r, g, b;
                hsv2rgb(hue, 100, glow_val, &r, &g, &b);
                set_pixel_add(cx, glow_y, r, g, b);
                /* Adjacent glow pixels */
                int dim_glow = glow_val * 2 / 3;
                hsv2rgb(hue, 140, dim_glow, &r, &g, &b);
                set_pixel_add(cx - 1, glow_y, r, g, b);
                set_pixel_add(cx + 1, glow_y, r, g, b);
            }
        }
    }

    draw();
}
