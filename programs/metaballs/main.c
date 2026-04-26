#include "api.h"

/* ---- Metadata JSON ---- */
static const char META[] =
    "{\"name\":\"Metaballs\","
    "\"desc\":\"Organic morphing blobs using isosurface math\","
    "\"params\":["
        "{\"id\":0,\"name\":\"Count\",\"type\":\"int\","
         "\"min\":2,\"max\":6,\"default\":3,"
         "\"desc\":\"Number of metaballs\"},"
        "{\"id\":1,\"name\":\"Speed\",\"type\":\"int\","
         "\"min\":1,\"max\":10,\"default\":4,"
         "\"desc\":\"Movement speed\"},"
        "{\"id\":2,\"name\":\"Brightness\",\"type\":\"int\","
         "\"min\":1,\"max\":255,\"default\":200,"
         "\"desc\":\"Overall brightness\"}"
    "]}";

EXPORT(get_meta_ptr)
int get_meta_ptr(void) { return (int)META; }

EXPORT(get_meta_len)
int get_meta_len(void) { return sizeof(META) - 1; }

/* ---- PRNG (xorshift32) ---- */
static uint32_t rng_state = 73541;

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

/* ---- Sine lookup table (256 entries, values -127..127) ---- */
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

static int icos(int angle) {
    return (int)sin_table[(angle + 64) & 255];
}

/* ---- HSV to RGB ---- */
static void hsv_to_rgb(int h, int s, int v, int *r, int *g, int *b) {
    h = h & 255;
    if (s == 0) { *r = *g = *b = v; return; }
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

/* ---- Blob state ---- */
#define MAX_BLOBS 6

/* Blob positions and velocities stored as fixed-point * 256 */
static int blob_x[MAX_BLOBS];
static int blob_y[MAX_BLOBS];
static int blob_vx[MAX_BLOBS];
static int blob_vy[MAX_BLOBS];
static int blob_hue[MAX_BLOBS];
static int blob_radius_sq[MAX_BLOBS]; /* radius squared * 256 for influence calc */

static int matrix_w, matrix_h;
static uint32_t frame;

EXPORT(init)
void init(void) {
    matrix_w = get_width();
    matrix_h = get_height();
    if (matrix_w < 1) matrix_w = 1;
    if (matrix_h < 1) matrix_h = 1;
    frame = 0;

    /* Initialize blobs spread across matrix */
    for (int i = 0; i < MAX_BLOBS; i++) {
        blob_x[i] = random_range(0, matrix_w * 256);
        blob_y[i] = random_range(0, matrix_h * 256);
        blob_vx[i] = random_range(-200, 200);
        blob_vy[i] = random_range(-200, 200);
        if (blob_vx[i] >= 0 && blob_vx[i] < 40) blob_vx[i] = 40;
        if (blob_vx[i] < 0 && blob_vx[i] > -40) blob_vx[i] = -40;
        if (blob_vy[i] >= 0 && blob_vy[i] < 40) blob_vy[i] = 40;
        if (blob_vy[i] < 0 && blob_vy[i] > -40) blob_vy[i] = -40;
        blob_hue[i] = (i * 256) / MAX_BLOBS;
        /* Radius of influence: ~3-5 pixels, stored as r^2 * 65536 */
        int rad = random_range(3, 6);
        blob_radius_sq[i] = rad * rad * 65536;
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
    frame++;

    /* Move blobs */
    int speed_mult = speed * 3;
    for (int i = 0; i < count; i++) {
        blob_x[i] += (blob_vx[i] * speed_mult) / 10;
        blob_y[i] += (blob_vy[i] * speed_mult) / 10;

        /* Horizontal cylinder wrap */
        if (blob_x[i] < 0) blob_x[i] += W * 256;
        if (blob_x[i] >= W * 256) blob_x[i] -= W * 256;

        /* Vertical bounce */
        if (blob_y[i] < 0) {
            blob_y[i] = -blob_y[i];
            blob_vy[i] = -blob_vy[i];
        }
        if (blob_y[i] >= (H - 1) * 256) {
            blob_y[i] = (H - 1) * 256 * 2 - blob_y[i];
            blob_vy[i] = -blob_vy[i];
        }

        /* Add slight wobble using sin table */
        blob_vx[i] += isin((int)(frame * 3 + i * 60)) / 32;
        blob_vy[i] += icos((int)(frame * 5 + i * 45)) / 32;

        /* Clamp velocity */
        if (blob_vx[i] > 300) blob_vx[i] = 300;
        if (blob_vx[i] < -300) blob_vx[i] = -300;
        if (blob_vy[i] > 300) blob_vy[i] = 300;
        if (blob_vy[i] < -300) blob_vy[i] = -300;
    }

    /* Render metaballs */
    for (int py = 0; py < H; py++) {
        for (int px = 0; px < W; px++) {
            int total = 0;
            int hue_sum = 0;
            int weight_sum = 0;

            for (int i = 0; i < count; i++) {
                /* Pixel position in fixed-point */
                int px256 = px * 256 + 128;
                int py256 = py * 256 + 128;

                /* Distance with cylinder wrap (horizontal) */
                int dxx = blob_x[i] - px256;
                /* Wrap around horizontally */
                int half_w = W * 128;
                if (dxx > half_w) dxx -= W * 256;
                if (dxx < -half_w) dxx += W * 256;

                int dyy = blob_y[i] - py256;

                /* dist_sq in fixed-point (divided by 256 to avoid overflow) */
                int dx_r = dxx / 16;
                int dy_r = dyy / 16;
                int dist_sq = dx_r * dx_r + dy_r * dy_r;

                if (dist_sq < 1) dist_sq = 1;

                /* Influence: radius_sq / dist_sq (result is intensity 0..many) */
                /* blob_radius_sq[i] is r^2 * 65536, dist_sq is in (pix/16)^2 = pix^2*256 */
                int influence = blob_radius_sq[i] / dist_sq;
                if (influence > 255) influence = 255;

                total += influence;
                hue_sum += blob_hue[i] * influence;
                weight_sum += influence;
            }

            if (total > 255) total = 255;

            /* Threshold - only draw where influence is significant */
            if (total > 20) {
                int hue = (weight_sum > 0) ? (hue_sum / weight_sum) : 0;
                int sat = 255;
                int val = total * bright / 255;
                if (val > 255) val = 255;

                int r, g, b;
                hsv_to_rgb(hue, sat, val, &r, &g, &b);
                set_pixel(px, py, r, g, b);
            } else {
                set_pixel(px, py, 0, 0, 0);
            }
        }
    }

    draw();
}
