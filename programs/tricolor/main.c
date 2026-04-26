#include "api.h"

/*
 * Tricolor — Russian flag waving in the wind.
 * Three horizontal stripes (white, blue, red), each ~25% of height,
 * with gaps between them. Each stripe waves independently.
 */

static const char META[] =
    "{\"name\":\"Tricolor\","
    "\"desc\":\"Waving Russian flag (white, blue, red stripes)\","
    "\"params\":["
        "{\"id\":0,\"name\":\"Speed\",\"type\":\"int\","
         "\"min\":1,\"max\":100,\"default\":35,"
         "\"desc\":\"Wave speed\"},"
        "{\"id\":1,\"name\":\"Amplitude\",\"type\":\"int\","
         "\"min\":1,\"max\":50,\"default\":20,"
         "\"desc\":\"Wave amplitude (% of stripe height)\"},"
        "{\"id\":2,\"name\":\"Brightness\",\"type\":\"int\","
         "\"min\":1,\"max\":255,\"default\":200,"
         "\"desc\":\"Overall brightness\"}"
    "]}";

EXPORT(get_meta_ptr)
int get_meta_ptr(void) { return (int)META; }
EXPORT(get_meta_len)
int get_meta_len(void) { return sizeof(META) - 1; }

/* ---- PRNG ---- */
static uint32_t rng = 44221;
static uint32_t rng_next(void) {
    uint32_t x = rng; x ^= x << 13; x ^= x >> 17; x ^= x << 5; rng = x; return x;
}

/* ---- Sine (Bhaskara I) ---- */
#define TWO_PI 6.28318530f
#define PI     3.14159265f

static float fsin(float x) {
    while (x < 0.0f) x += TWO_PI;
    while (x >= TWO_PI) x -= TWO_PI;
    float sign = 1.0f;
    if (x > PI) { x -= PI; sign = -1.0f; }
    float num = 16.0f * x * (PI - x);
    float den = 5.0f * PI * PI - 4.0f * x * (PI - x);
    if (den == 0.0f) return 0.0f;
    return sign * num / den;
}

/* Per-column phase offsets for more organic wave */
#define MAX_W 64
static float col_phase[MAX_W];

EXPORT(init)
void init(void) {
    rng = 44221;
    for (int i = 0; i < MAX_W; i++) {
        col_phase[i] = (float)(rng_next() % 1000) / 1000.0f * 0.5f;
    }
}

/* Clamp helper */
static int clamp255(int v) {
    if (v < 0) return 0;
    if (v > 255) return 255;
    return v;
}

EXPORT(update)
void update(int tick_ms) {
    int speed  = get_param_i32(0);
    int amp_p  = get_param_i32(1);
    int bright = get_param_i32(2);

    int W = get_width();
    int H = get_height();
    if (W < 1) W = 1;
    if (H < 1) H = 1;
    if (W > MAX_W) W = MAX_W;
    if (H > 64) H = 64;

    /* Time as a continuous phase */
    float t = (float)tick_ms * (float)speed * 0.00003f;

    /* Stripe layout: each stripe is ~25% of height, with ~12.5% gaps
     *   stripe 0 (white): centered at 20% of H
     *   stripe 1 (blue):  centered at 50% of H
     *   stripe 2 (red):   centered at 80% of H
     */
    /* Per-stripe heights: white slightly taller to look equal visually */
    float stripe_heights[3] = {
        (float)H * 0.30f,   /* white */
        (float)H * 0.25f,   /* blue */
        (float)H * 0.25f    /* red */
    };

    /* Stripe centers (Y=0 is bottom of physical display) */
    float centers[3] = {
        (float)H * 0.80f,   /* white — top */
        (float)H * 0.50f,   /* blue  — middle */
        (float)H * 0.20f    /* red   — bottom */
    };

    /* Stripe colors (RGB) */
    int colors[3][3] = {
        {bright, bright, bright},                              /* white */
        {0, clamp255(bright * 57 / 255), bright},             /* blue */
        {bright, clamp255(bright * 15 / 255), clamp255(bright * 15 / 255)}  /* red */
    };

    /* Maximum wave displacement in pixels */
    float max_disp = (float)H * (float)amp_p / 100.0f * 0.5f;

    /* Clear to black */
    for (int x = 0; x < W; x++) {
        for (int y = 0; y < H; y++) {
            set_pixel(x, y, 0, 0, 0);
        }
    }

    /* Draw each stripe */
    for (int s = 0; s < 3; s++) {
        /* Each stripe has its own wave frequency and phase offset */
        float freq = 1.5f + (float)s * 0.4f;      /* slightly different freq per stripe */
        float phase_off = (float)s * 2.1f;          /* phase offset between stripes */

        for (int x = 0; x < W; x++) {
            /* Wave displacement for this column and stripe */
            float col_x = (float)x / (float)W;
            float wave = fsin(t + col_x * freq * TWO_PI + phase_off + col_phase[x % MAX_W]);
            /* Add a second harmonic for more natural look */
            float wave2 = fsin(t * 1.3f + col_x * freq * 1.7f * TWO_PI + phase_off * 0.7f) * 0.3f;

            float displacement = (wave + wave2) * max_disp;

            /* Compute displaced center for this column */
            float cy = centers[s] + displacement;
            float sh = stripe_heights[s];
            if (sh < 1.0f) sh = 1.0f;
            float hh = sh * 0.5f;

            /* Draw the stripe column */
            int y_start = (int)(cy - hh);
            int y_end = (int)(cy + hh);

            for (int y = y_start; y <= y_end; y++) {
                if (y < 0 || y >= H) continue;

                /* Fade at the edges of the stripe for softer look */
                float dist_from_center = (float)(y) - cy;
                if (dist_from_center < 0.0f) dist_from_center = -dist_from_center;
                float edge_fade = 1.0f;
                if (dist_from_center > hh * 0.6f) {
                    edge_fade = 1.0f - (dist_from_center - hh * 0.6f) / (hh * 0.4f);
                    if (edge_fade < 0.0f) edge_fade = 0.0f;
                }

                /* Slight brightness variation along X for cloth texture */
                float cloth = 0.85f + 0.15f * fsin(col_x * TWO_PI * 3.0f + t * 0.5f + (float)s);

                int r = clamp255((int)((float)colors[s][0] * edge_fade * cloth));
                int g = clamp255((int)((float)colors[s][1] * edge_fade * cloth));
                int b = clamp255((int)((float)colors[s][2] * edge_fade * cloth));

                set_pixel(x, y, r, g, b);
            }
        }
    }

    draw();
}
