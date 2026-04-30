#include "api.h"

/*
 * Tricolor — Flag waving in the wind.
 * Supports multiple flags via select parameter.
 */

static const char META[] =
    "{\"name\":\"Tricolor\","
    "\"desc\":\"Waving flag (select country)\","
    "\"params\":["
        "{\"id\":0,\"name\":\"Flag\",\"type\":\"select\","
         "\"options\":[\"Russia\",\"Belarus\"],"
         "\"default\":0,"
         "\"desc\":\"Flag to display\"},"
        "{\"id\":1,\"name\":\"Speed\",\"type\":\"int\","
         "\"min\":1,\"max\":100,\"default\":35,"
         "\"desc\":\"Wave speed\"},"
        "{\"id\":2,\"name\":\"Amplitude\",\"type\":\"int\","
         "\"min\":1,\"max\":50,\"default\":20,"
         "\"desc\":\"Wave amplitude (% of stripe height)\"},"
        "{\"id\":3,\"name\":\"Brightness\",\"type\":\"int\","
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
    int flag   = get_param_i32(0);
    int speed  = get_param_i32(1);
    int amp_p  = get_param_i32(2);
    int bright = get_param_i32(3);

    int W = get_width();
    int H = get_height();
    if (W < 1) W = 1;
    if (H < 1) H = 1;
    if (W > MAX_W) W = MAX_W;
    if (H > 64) H = 64;

    /* Time as a continuous phase */
    float t = (float)tick_ms * (float)speed * 0.00003f;

    /* Stripe configuration per flag
     * Y=0 is the BOTTOM of the physical display
     */
    int num_stripes;
    float stripe_heights[3];
    float centers[3];
    int colors[3][3];

    if (flag == 1) {
        /* Belarus: red (top 2/3), green (bottom 1/3) */
        num_stripes = 3;

        stripe_heights[0] = (float)H * 0.30f;  /* green — bottom */
        stripe_heights[1] = (float)H * 0.28f;  /* red — middle */
        stripe_heights[2] = (float)H * 0.28f;  /* red — top */

        centers[0] = (float)H * 0.17f;  /* green */
        centers[1] = (float)H * 0.52f;  /* red */
        centers[2] = (float)H * 0.82f;  /* red */

        /* Green (#007C30 scaled by brightness) */
        colors[0][0] = 0;
        colors[0][1] = clamp255(bright * 124 / 255);
        colors[0][2] = clamp255(bright * 48 / 255);
        /* Red (#CC0930 scaled by brightness) */
        colors[1][0] = bright;
        colors[1][1] = clamp255(bright * 9 / 255);
        colors[1][2] = clamp255(bright * 15 / 255);
        /* Red (same) */
        colors[2][0] = bright;
        colors[2][1] = clamp255(bright * 9 / 255);
        colors[2][2] = clamp255(bright * 15 / 255);
    } else {
        /* Russia: white (top), blue (middle), red (bottom) — default */
        num_stripes = 3;

        stripe_heights[0] = (float)H * 0.25f;  /* red */
        stripe_heights[1] = (float)H * 0.25f;  /* blue */
        stripe_heights[2] = (float)H * 0.30f;  /* white */

        centers[0] = (float)H * 0.20f;  /* red — bottom */
        centers[1] = (float)H * 0.50f;  /* blue — middle */
        centers[2] = (float)H * 0.80f;  /* white — top */

        /* Red */
        colors[0][0] = bright;
        colors[0][1] = clamp255(bright * 15 / 255);
        colors[0][2] = clamp255(bright * 15 / 255);
        /* Blue */
        colors[1][0] = 0;
        colors[1][1] = clamp255(bright * 57 / 255);
        colors[1][2] = bright;
        /* White */
        colors[2][0] = bright;
        colors[2][1] = bright;
        colors[2][2] = bright;
    }

    /* Maximum wave displacement in pixels */
    float max_disp = (float)H * (float)amp_p / 100.0f * 0.5f;

    /* Clear to black */
    for (int x = 0; x < W; x++) {
        for (int y = 0; y < H; y++) {
            set_pixel(x, y, 0, 0, 0);
        }
    }

    /* Draw each stripe */
    for (int s = 0; s < num_stripes; s++) {
        /* Each stripe has its own wave frequency and phase offset */
        float freq = 1.5f + (float)s * 0.4f;
        float phase_off = (float)s * 2.1f;

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
