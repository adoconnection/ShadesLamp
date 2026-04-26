#include "api.h"

/* ---- Metadata JSON ---- */
static const char META[] =
    "{\"name\":\"Lava Lamp\","
    "\"desc\":\"Warm blobs rising and falling like a lava lamp\","
    "\"params\":["
        "{\"id\":0,\"name\":\"Count\",\"type\":\"int\","
         "\"min\":2,\"max\":6,\"default\":4,"
         "\"desc\":\"Number of lava blobs\"},"
        "{\"id\":1,\"name\":\"Speed\",\"type\":\"int\","
         "\"min\":1,\"max\":10,\"default\":3,"
         "\"desc\":\"Rise/fall speed\"},"
        "{\"id\":2,\"name\":\"Brightness\",\"type\":\"int\","
         "\"min\":1,\"max\":255,\"default\":200,"
         "\"desc\":\"Overall brightness\"}"
    "]}";

EXPORT(get_meta_ptr)
int get_meta_ptr(void) { return (int)META; }

EXPORT(get_meta_len)
int get_meta_len(void) { return sizeof(META) - 1; }

/* ---- PRNG (xorshift32) ---- */
static uint32_t rng_state = 48271;

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

/* ---- Warm color palette: heat value 0-255 -> RGB ---- */
/* Dark red -> Red -> Orange -> Yellow */
static void warm_color(int heat, int brightness, int *r, int *g, int *b) {
    int t = heat;
    if (t > 255) t = 255;
    if (t < 0) t = 0;

    int r0, g0, b0;
    if (t < 85) {
        /* Dark to Red */
        r0 = t * 3;
        g0 = 0;
        b0 = 0;
    } else if (t < 170) {
        /* Red to Orange */
        int s = t - 85;
        r0 = 255;
        g0 = s * 2;  /* up to ~170 green = orange */
        b0 = 0;
    } else {
        /* Orange to Yellow */
        int s = t - 170;
        r0 = 255;
        g0 = 170 + s;
        if (g0 > 255) g0 = 255;
        b0 = s / 3;  /* hint of white warmth */
    }

    *r = r0 * brightness / 255;
    *g = g0 * brightness / 255;
    *b = b0 * brightness / 255;
}

/* ---- Blob state ---- */
#define MAX_BLOBS 6

/* Using float for smooth vertical movement */
static float blob_x[MAX_BLOBS];
static float blob_y[MAX_BLOBS];
static float blob_vy[MAX_BLOBS];
static float blob_wobble_phase[MAX_BLOBS];
static int   blob_radius[MAX_BLOBS];  /* blob size in pixels (2-4) */
static int   blob_heat[MAX_BLOBS];    /* color warmth 100-255 */

/* ---- Sine table for wobble (256 entries, -127..127) ---- */
static const signed char sin_table[256] = {
      0,   3,   6,   9,  12,  16,  19,  22,  25,  28,  31,  34,  37,  40,  43,  46,
     49,  51,  54,  57,  60,  63,  65,  68,  71,  73,  76,  78,  81,  83,  85,  88,
     90,  92,  94,  96,  98, 100, 102, 104, 106, 107, 109, 111, 112, 113, 115, 116,
    117, 118, 120, 121, 122, 122, 123, 124, 125, 125, 126, 126, 126, 127, 127, 127,
    127, 127, 127, 127, 126, 126, 126, 125, 125, 124, 123, 122, 122, 121, 120, 118,
    117, 116, 115, 113, 112, 111, 109, 107, 106, 104, 102, 100,  98,  96,  94,  92,
     90,  88,  85,  83,  81,  78,  76,  73,  71,  68,  65,  63,  60,  57,  54,  51,
     49,  46,  43,  40,  37,  34,  31,  28,  25,  22,  19,  16,  12,   9,   6,   3,
      0,  -3,  -6,  -9, -12, -16, -19, -22, -25, -28, -31, -34, -37, -40, -43, -46,
    -49, -51, -54, -57, -60, -63, -65, -68, -71, -73, -76, -78, -81, -83, -85, -88,
    -90, -92, -94, -96, -98,-100,-102,-104,-106,-107,-109,-111,-112,-113,-115,-116,
   -117,-118,-120,-121,-122,-122,-123,-124,-125,-125,-126,-126,-126,-127,-127,-127,
   -127,-127,-127,-127,-126,-126,-126,-125,-125,-124,-123,-122,-122,-121,-120,-118,
   -117,-116,-115,-113,-112,-111,-109,-107,-106,-104,-102,-100, -98, -96, -94, -92,
    -90, -88, -85, -83, -81, -78, -76, -73, -71, -68, -65, -63, -60, -57, -54, -51,
    -49, -46, -43, -40, -37, -34, -31, -28, -25, -22, -19, -16, -12,  -9,  -6,  -3
};

static int isin(int angle) {
    return (int)sin_table[angle & 255];
}

static int stored_w, stored_h;

static void init_blob(int i, int w, int h) {
    blob_x[i] = (float)random_range(1, w - 1) + 0.5f;
    blob_y[i] = (float)random_range(0, h / 3);  /* start near bottom */
    blob_vy[i] = (float)random_range(5, 15) / 100.0f;  /* slow upward drift */
    blob_wobble_phase[i] = (float)random_range(0, 256);
    blob_radius[i] = random_range(2, 5);
    blob_heat[i] = random_range(120, 255);
}

EXPORT(init)
void init(void) {
    stored_w = get_width();
    stored_h = get_height();
    if (stored_w < 1) stored_w = 1;
    if (stored_h < 1) stored_h = 1;

    for (int i = 0; i < MAX_BLOBS; i++) {
        init_blob(i, stored_w, stored_h);
        /* Stagger initial Y positions */
        blob_y[i] = (float)random_range(0, stored_h);
    }
}

EXPORT(update)
void update(int tick_ms) {
    int count  = get_param_i32(0);
    int speed  = get_param_i32(1);
    int bright = get_param_i32(2);

    int W = get_width();
    int H = get_height();
    if (W < 1) W = 1;
    if (H < 1) H = 1;
    if (count < 2) count = 2;
    if (count > MAX_BLOBS) count = MAX_BLOBS;

    rng_state ^= (uint32_t)tick_ms;

    float speed_f = (float)speed * 0.015f;

    /* Move blobs */
    for (int i = 0; i < count; i++) {
        blob_y[i] += blob_vy[i] * speed_f * 10.0f;

        /* Horizontal wobble using sin table */
        blob_wobble_phase[i] += speed_f * 2.0f;
        if (blob_wobble_phase[i] >= 256.0f) blob_wobble_phase[i] -= 256.0f;
        float wobble = (float)isin((int)blob_wobble_phase[i]) / 127.0f * 0.3f;
        blob_x[i] += wobble * speed_f * 5.0f;

        /* Horizontal cylinder wrap */
        if (blob_x[i] < 0) blob_x[i] += (float)W;
        if (blob_x[i] >= (float)W) blob_x[i] -= (float)W;

        /* Bounce at top and bottom - slow down near edges like real lava lamp */
        if (blob_y[i] >= (float)(H - 1)) {
            blob_vy[i] = -(float)random_range(5, 15) / 100.0f;
            blob_y[i] = (float)(H - 1) - 0.01f;
            blob_heat[i] = random_range(100, 180);  /* cool down at top */
        }
        if (blob_y[i] <= 0.0f) {
            blob_vy[i] = (float)random_range(5, 15) / 100.0f;
            blob_y[i] = 0.01f;
            blob_heat[i] = random_range(180, 255);  /* reheat at bottom */
        }

        /* Decelerate near top/bottom for organic feel */
        float dist_to_edge;
        if (blob_vy[i] > 0) {
            dist_to_edge = (float)(H - 1) - blob_y[i];
        } else {
            dist_to_edge = blob_y[i];
        }
        if (dist_to_edge < (float)blob_radius[i]) {
            float factor = dist_to_edge / (float)blob_radius[i];
            if (factor < 0.1f) factor = 0.1f;
            /* Gradual deceleration near edges */
            blob_y[i] += blob_vy[i] * (1.0f - factor) * speed_f * (-3.0f);
        }
    }

    /* Clear and render blobs using metaball-like field */
    for (int py = 0; py < H; py++) {
        for (int px = 0; px < W; px++) {
            int total_field = 0;
            int heat_accum = 0;
            int weight_sum = 0;

            for (int i = 0; i < count; i++) {
                /* Distance to blob center with horizontal wrap */
                float fdx = (float)px - blob_x[i];
                float half_w = (float)W * 0.5f;
                if (fdx > half_w) fdx -= (float)W;
                if (fdx < -half_w) fdx += (float)W;
                float fdy = (float)py - blob_y[i];

                /* Integer distance squared approximation */
                int dx = (int)(fdx * 16.0f);
                int dy = (int)(fdy * 16.0f);
                int dist_sq = dx * dx + dy * dy;
                if (dist_sq < 1) dist_sq = 1;

                /* Influence: r^2 * K / dist_sq */
                int r = blob_radius[i];
                int influence = (r * r * 65536) / dist_sq;
                if (influence > 255) influence = 255;

                total_field += influence;
                heat_accum += blob_heat[i] * influence;
                weight_sum += influence;
            }

            if (total_field > 255) total_field = 255;

            if (total_field > 25) {
                int heat = (weight_sum > 0) ? (heat_accum / weight_sum) : 180;
                int val = total_field * bright / 255;
                if (val > 255) val = 255;
                int r, g, b;
                warm_color(heat, val, &r, &g, &b);
                set_pixel(px, py, r, g, b);
            } else {
                /* Very dark warm background glow */
                int bg = total_field * bright / (255 * 10);
                set_pixel(px, py, bg, 0, 0);
            }
        }
    }

    draw();
}
