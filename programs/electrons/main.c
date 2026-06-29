#include "api.h"

/*
 * Electrons — fast particles orbiting a nucleus.
 * All particles orbit in the same direction with slight orbital tilts,
 * clustered around the equator. Adjustable trail length.
 */

static const char META[] =
    "{\"name\":\"Electrons\","
    "\"desc\":\"Fast electrons orbiting around a nucleus\","
    "\"params\":["
        "{\"id\":0,\"name\":\"Hue\",\"type\":\"int\","
         "\"min\":0,\"max\":255,\"default\":0,"
         "\"desc\":\"Base hue (0=palette rotation, 25=fire-orange)\"},"
        "{\"id\":1,\"name\":\"Electrons\",\"type\":\"int\","
         "\"min\":1,\"max\":80,\"default\":40,"
         "\"desc\":\"Number of electrons\"},"
        "{\"id\":2,\"name\":\"Speed\",\"type\":\"int\","
         "\"min\":1,\"max\":600,\"default\":231,"
         "\"desc\":\"Orbital speed\"},"
        "{\"id\":3,\"name\":\"Trail\",\"type\":\"int\","
         "\"min\":1,\"max\":50,\"default\":30,"
         "\"desc\":\"Trail length (less=shorter)\"},"
        "{\"id\":4,\"name\":\"Brightness\",\"type\":\"int\","
         "\"min\":1,\"max\":255,\"default\":230,"
         "\"desc\":\"Overall brightness\"},"
        "{\"id\":5,\"name\":\"White Head\",\"type\":\"int\","
         "\"min\":0,\"max\":1,\"default\":1,"
         "\"options\":[\"Off\",\"On\"],"
         "\"desc\":\"White electron head\"},"
        "{\"id\":6,\"name\":\"Spread\",\"type\":\"int\","
         "\"min\":1,\"max\":100,\"default\":42,"
         "\"desc\":\"Vertical spread (% of height)\"},"
        "{\"id\":7,\"name\":\"Rotation\",\"type\":\"int\","
         "\"min\":0,\"max\":200,\"default\":38,"
         "\"desc\":\"Rotate equator-crossing points around the lamp\"},"
        "{\"id\":8,\"name\":\"Tilt\",\"type\":\"int\","
         "\"min\":0,\"max\":100,\"default\":71,"
         "\"desc\":\"Orbit axis tilt; rocks between -tilt and +tilt (% of height)\"}"
    "]}";

EXPORT(get_meta_ptr)
int get_meta_ptr(void) { return (int)META; }
EXPORT(get_meta_len)
int get_meta_len(void) { return sizeof(META) - 1; }

/* ---- PRNG ---- */
static uint32_t rng = 77317;
static uint32_t rng_next(void) {
    uint32_t x = rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    rng = x; return x;
}

/* ---- HSV -> RGB ---- */
static void hsv2rgb(int hue, int sat, int val, int *r, int *g, int *b) {
    int c = m_hsv(hue & 0xFF, sat, val); *r = (c>>16)&255; *g = (c>>8)&255; *b = c&255;
}

#define TWO_PI  6.28318530f

/* ---- Particles ---- */
#define MAX_P 80

static float p_angle[MAX_P];   /* orbital angle */
static float p_incl[MAX_P];    /* vertical amplitude fraction: -1.0 to +1.0 */
static float p_speed[MAX_P];   /* individual speed mult */
static uint8_t p_hue[MAX_P];   /* hue offset */

/* Phase offset of the vertical oscillation. Advancing it rotates the
 * equator-crossing points (nodes) around the cylinder instead of pinning
 * them to fixed columns (x=0 and x=width/2). */
static float rot_phase = 0.0f;

/* Phase of the orbit-axis tilt. The tilt magnitude is animated as a slow
 * sine that sweeps the axis from -strength to +strength and back, so the
 * whole ring of electrons rocks like a wobbling coin. */
static float tilt_phase = 0.0f;

/* Hue=0 is a special mode: instead of red, the base hue rotates through the
 * whole spectrum over time (palette rotation). This phase drives it. */
static float pal_phase = 0.0f;

/* ---- Framebuffer ---- */
#define MAX_W 64
#define MAX_H 64
static uint8_t fb_hue[MAX_W * MAX_H];
static uint8_t fb_val[MAX_W * MAX_H];

/* Head positions for white overlay */
static int head_x[MAX_P];
static int head_y[MAX_P];

static int cur_w, cur_h;
#define FB(x,y) ((x) * cur_h + (y))

/* ---- Draw single pixel with max-V blend ---- */
static void plot(int px, int py, uint8_t bright, uint8_t hue_val) {
    while (px < 0) px += cur_w;
    while (px >= cur_w) px -= cur_w;
    if (py < 0 || py >= cur_h) return;
    int fi = FB(px, py);
    if (bright > fb_val[fi]) {
        fb_hue[fi] = hue_val;
        fb_val[fi] = bright;
    }
}

EXPORT(init)
void init(void) {
    for (int i = 0; i < MAX_W * MAX_H; i++) {
        fb_hue[i] = 0; fb_val[i] = 0;
    }
    rng = 77317;
    rot_phase = 0.0f;
    tilt_phase = 0.0f;
    pal_phase = 0.0f;

    for (int i = 0; i < MAX_P; i++) {
        /* Spread starting angles evenly + jitter */
        p_angle[i] = (float)i / (float)MAX_P * TWO_PI
                   + ((float)(rng_next() % 1000) / 1000.0f - 0.5f) * 0.5f;

        /* Vertical amplitude fraction: evenly distributed -1.0 to +1.0 */
        p_incl[i] = ((float)(rng_next() % 10000) / 10000.0f) * 2.0f - 1.0f;

        /* Speed: 0.6x to 1.5x of base — all positive (same direction) */
        p_speed[i] = 0.6f + (float)(rng_next() % 10000) / 11111.0f;

        /* Hue offset: 0-20 */
        p_hue[i] = (uint8_t)(rng_next() % 20);
    }
}

EXPORT(update)
void update(int tick_ms) {
    int base_hue   = get_param_i32(0);
    int num_p      = get_param_i32(1);
    int speed      = get_param_i32(2);
    int trail      = get_param_i32(3);
    int bright     = get_param_i32(4);
    int white_head = get_param_i32(5);
    int spread     = get_param_i32(6);
    int rotation   = get_param_i32(7);
    int tilt       = get_param_i32(8);

    cur_w = get_width();
    cur_h = get_height();
    if (cur_w > MAX_W) cur_w = MAX_W;
    if (cur_h > MAX_H) cur_h = MAX_H;
    if (cur_w < 1) cur_w = 1;
    if (cur_h < 1) cur_h = 1;
    if (num_p > MAX_P) num_p = MAX_P;

    float equator = (float)cur_h / 2.0f;
    /* Spread controls max vertical amplitude: 1%=tight, 100%=full height */
    float max_amp = (equator - 1.0f) * (float)spread / 100.0f;
    if (max_amp < 0.5f) max_amp = 0.5f;

    float base_speed = (float)speed * 0.0007f;

    /* Tilt: same scaling idea as spread — strength is a % of the half-height.
     * The actual tilt is animated, swinging -max_tilt .. +max_tilt over time. */
    if (tilt < 0) tilt = 0; if (tilt > 100) tilt = 100;
    float max_tilt = (equator - 1.0f) * (float)tilt / 100.0f;
    tilt_phase += 0.02f;                       /* ~10 s full rock at 30 FPS */
    while (tilt_phase >= TWO_PI) tilt_phase -= TWO_PI;
    float cur_tilt = max_tilt * m_sin(tilt_phase);

    /* Hue=0 -> palette rotation: sweep the base hue through the spectrum.
     * Any other value pins the base hue as before. */
    int eff_hue = base_hue;
    if (base_hue == 0) {
        pal_phase += 0.5f;                     /* ~17 s per full rainbow at 30 FPS */
        while (pal_phase >= 256.0f) pal_phase -= 256.0f;
        eff_hue = (int)pal_phase;
    }

    /* Trail: trail=1 -> very short (fast fade), trail=50 -> very long (slow fade) */
    /* fade = 256 - (51-trail)*4: trail=1->56/256, trail=50->252/256 */
    int fade = 256 - (51 - trail) * 4;
    if (fade < 40) fade = 40;
    if (fade > 253) fade = 253;

    rng ^= (uint32_t)tick_ms;

    /* Advance the node rotation phase (0 = classic fixed crossing points) */
    rot_phase += (float)rotation * 0.0003f;
    while (rot_phase >= TWO_PI) rot_phase -= TWO_PI;

    /* Fade framebuffer */
    for (int i = 0; i < cur_w * cur_h; i++)
        fb_val[FB(i / cur_h, i % cur_h)] = (uint8_t)((int)fb_val[FB(i / cur_h, i % cur_h)] * fade >> 8);

    /* Update and draw electrons */
    for (int i = 0; i < num_p; i++) {
        p_angle[i] += base_speed * p_speed[i];
        if (p_angle[i] >= TWO_PI) p_angle[i] -= TWO_PI;

        /* X: linear around cylinder */
        float fx = p_angle[i] / TWO_PI * (float)cur_w;
        /* Y: oscillation around equator, amplitude = max_amp * incl fraction.
         * Subtracting rot_phase decouples the crossing nodes from x position,
         * letting them rotate around the cylinder over time.
         * The tilt slants the whole ring (one side rides high, the opposite
         * side low) — a tilted orbital plane that rocks in time. It shares the
         * same `- rot_phase` reference as the spread oscillation, so Rotation
         * moves the zero-crossing nodes of the tilt too, not just the spread. */
        float fy = equator
                 + (cur_tilt + max_amp * p_incl[i]) * m_sin(p_angle[i] - rot_phase);

        int px = (int)(fx + 0.5f);

        /* With strong tilt an electron can swing off the top or bottom of the
         * field. Don't clamp it to the edge (that smears a bright dot along the
         * rim) — just skip drawing it this frame; it reappears as it orbits
         * back into range. */
        if (fy < 0.0f || fy >= (float)cur_h) {
            head_x[i] = px;
            head_y[i] = -1;          /* sentinel: white-head pass skips it */
            continue;
        }

        int py = (int)(fy + 0.5f);
        if (py >= cur_h) py = cur_h - 1;

        uint8_t hue = (uint8_t)(eff_hue + p_hue[i]);
        plot(px, py, 255, hue);

        /* Remember head position for white overlay */
        head_x[i] = px;
        head_y[i] = py;
    }

    /* Render trails (colored) */
    for (int x = 0; x < cur_w; x++) {
        for (int y = 0; y < cur_h; y++) {
            int fi = FB(x, y);
            int v = (int)fb_val[fi] * bright / 255;
            if (v < 1) {
                set_pixel(x, y, 0, 0, 0);
            } else {
                int r, g, b;
                hsv2rgb(fb_hue[fi], 255, v, &r, &g, &b);
                set_pixel(x, y, r, g, b);
            }
        }
    }

    /* White electron heads on top (if enabled) */
    if (white_head) {
        for (int i = 0; i < num_p; i++) {
            int px = head_x[i];
            int py = head_y[i];
            while (px < 0) px += cur_w;
            while (px >= cur_w) px -= cur_w;
            if (py >= 0 && py < cur_h) {
                set_pixel(px, py, bright, bright, bright);
            }
        }
    }

    draw();
}
