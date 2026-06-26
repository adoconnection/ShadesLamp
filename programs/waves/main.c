#include "api.h"

/* ---- Metadata JSON ---- */
static const char META[] =
    "{\"name\":\"Waves\","
    "\"desc\":\"An undulating sea surface with crests and foam\","
    "\"params\":["
        "{\"id\":0,\"name\":\"Speed\",\"type\":\"int\","
         "\"min\":1,\"max\":10,\"default\":4,"
         "\"desc\":\"How fast the waves travel\"},"
        "{\"id\":1,\"name\":\"Hue\",\"type\":\"int\","
         "\"min\":0,\"max\":255,\"default\":150,"
         "\"desc\":\"Water color hue (150=sea blue)\"},"
        "{\"id\":2,\"name\":\"Brightness\",\"type\":\"int\","
         "\"min\":1,\"max\":255,\"default\":210,"
         "\"desc\":\"Overall brightness\"},"
        "{\"id\":3,\"name\":\"Level\",\"type\":\"int\","
         "\"min\":5,\"max\":95,\"default\":55,"
         "\"desc\":\"Water fill level (%)\"},"
        "{\"id\":4,\"name\":\"Choppiness\",\"type\":\"int\","
         "\"min\":0,\"max\":100,\"default\":45,"
         "\"desc\":\"Wave height / roughness\"},"
        "{\"id\":5,\"name\":\"Foam\",\"type\":\"int\","
         "\"min\":0,\"max\":100,\"default\":55,"
         "\"desc\":\"Whitecap foam on the crests\"}"
    "]}";

EXPORT(get_meta_ptr)
int get_meta_ptr(void) { return (int)META; }

EXPORT(get_meta_len)
int get_meta_len(void) { return sizeof(META) - 1; }

/* sine of an integer angle (0..255 per period), result -1..1 — native */
static float fsin(int angle) {
    return m_sin((float)angle * (6.28318530f / 256.0f));
}

/* ---- HSV to RGB (native host primitive) ---- */
static void hsv_to_rgb(int h, int s, int v, int *r, int *g, int *b) {
    if (v < 0) v = 0; if (v > 255) v = 255;
    if (s < 0) s = 0; if (s > 255) s = 255;
    int c = m_hsv(h & 255, s, v);
    *r = (c >> 16) & 255; *g = (c >> 8) & 255; *b = c & 255;
}

/* ---- State ---- */
#define MAX_W 64
static int32_t prev_tick;
static uint32_t phase;            /* travels with time*speed */
static float surf[MAX_W];         /* surface height per column (px) */
static float crest[MAX_W];        /* 0..1 how high this column's crest is */

EXPORT(init)
void init(void) {
    prev_tick = 0;
    phase = 0;
}

EXPORT(update)
void update(int tick_ms) {
    int speed  = get_param_i32(0);
    int hue    = get_param_i32(1);
    int bright = get_param_i32(2);
    int level  = get_param_i32(3);
    int chop   = get_param_i32(4);
    int foam_p = get_param_i32(5);

    int W = get_width();
    int H = get_height();
    if (W < 1) W = 1;
    if (H < 1) H = 1;
    if (W > MAX_W) W = MAX_W;

    /* advance wave phase by real elapsed time * speed */
    int32_t delta = tick_ms - prev_tick;
    if (delta <= 0 || delta > 200) delta = 33;
    prev_tick = tick_ms;
    phase += (uint32_t)(delta * speed);

    /* traveling phases for three wave components (different rates/directions) */
    int ph1 = (int)(phase / 45);   /* long swell  */
    int ph2 = (int)(phase / 28);   /* mid wave    */
    int ph3 = (int)(phase / 17);   /* short chop  */

    float level_px = (float)level * 0.01f * (float)H;
    float amp_px   = (float)chop  * 0.01f * (float)H * 0.22f;
    float foam_amt = (float)foam_p * 0.01f;

    /* ---- 1. Build the wavy surface height for each column ----
       Integer wave numbers over the full width keep it seamless around
       the cylinder. Components travel in mixed directions for a living sea. */
    for (int x = 0; x < W; x++) {
        int base = x * 256 / W;
        float s1 = fsin(1 * base + ph1);
        float s2 = fsin(2 * base - ph2);
        float s3 = fsin(3 * base + ph3);
        float s = 0.55f * s1 + 0.30f * s2 + 0.20f * s3;   /* ~ -1.05..1.05 */
        surf[x]  = level_px + amp_px * s;
        crest[x] = (s + 1.05f) / 2.10f;                   /* 0..1 */
    }

    /* internal shimmer phase (light dancing under the surface) */
    int shph = (int)(phase / 12);

    /* ---- 2. Render columns ---- */
    for (int x = 0; x < W; x++) {
        float sy = surf[x];
        float cr = crest[x];
        float foam_depth = 0.8f + 2.6f * foam_amt;   /* px band below the line */

        for (int y = 0; y < H; y++) {
            float ds = sy - (float)y;        /* >0 underwater, depth in px */
            float cov = ds + 0.5f;           /* coverage for the surface line */
            if (cov > 1.0f) cov = 1.0f;

            int r = 0, g = 0, b = 0;

            if (cov > 0.0f) {
                /* depth 0 (surface) .. 1 (bottom) */
                float td = ds / (float)H;
                if (td < 0.0f) td = 0.0f;
                if (td > 1.0f) td = 1.0f;

                /* bright near the surface, darker (but alive) in the deep */
                float vf = 0.40f + 0.60f * (1.0f - td);

                /* gentle internal shimmer */
                float sh = fsin(x * 34 + y * 19 + shph) * 0.10f;
                vf += sh * (1.0f - td);
                if (vf < 0.0f) vf = 0.0f;

                int val = (int)(vf * (float)bright * cov + 0.5f);
                int sat = 205 + (int)(td * 50.0f);          /* deeper = richer */
                int phue = (hue + (int)(td * 16.0f)) & 255; /* deep slightly bluer */

                hsv_to_rgb(phue, sat, val, &r, &g, &b);
            }

            /* ---- foam: whitecaps near the crest line ---- */
            if (foam_amt > 0.0f && ds > -1.0f && ds < foam_depth) {
                float fb = 1.0f - ds / foam_depth;   /* 1 at line, 0 deeper */
                if (fb > 1.0f) fb = 1.0f;
                if (fb < 0.0f) fb = 0.0f;
                float fstr = foam_amt * cr * cr * fb;
                if (ds < 0.0f) fstr *= (1.0f + ds);  /* fade spray above line */
                if (fstr < 0.0f) fstr = 0.0f;
                if (fstr > 1.0f) fstr = 1.0f;
                int fw = (int)(fstr * (float)bright);
                r += (255 - r) * fw / 255;
                g += (255 - g) * fw / 255;
                b += (255 - b) * fw / 255;
            }

            if (r > 255) r = 255;
            if (g > 255) g = 255;
            if (b > 255) b = 255;
            set_pixel(x, y, r, g, b);
        }
    }

    draw();
}
