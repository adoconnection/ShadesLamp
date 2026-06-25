#include "api.h"

/* ---- Metadata JSON ---- */
static const char META[] =
    "{\"name\":\"Sinusoid\","
    "\"desc\":\"Glowing sine waves flowing around the lamp\","
    "\"params\":["
        "{\"id\":0,\"name\":\"Speed\",\"type\":\"int\","
         "\"min\":1,\"max\":10,\"default\":5,"
         "\"desc\":\"How fast the waves travel\"},"
        "{\"id\":1,\"name\":\"Count\",\"type\":\"int\","
         "\"min\":1,\"max\":5,\"default\":3,"
         "\"desc\":\"Number of sine waves\"},"
        "{\"id\":2,\"name\":\"Thickness\",\"type\":\"int\","
         "\"min\":1,\"max\":6,\"default\":2,"
         "\"desc\":\"Line thickness in pixels\"},"
        "{\"id\":3,\"name\":\"Period\",\"type\":\"int\","
         "\"min\":1,\"max\":100,\"default\":16,"
         "\"desc\":\"Wavelength in pixels; beyond screen width it glides up/down\"},"
        "{\"id\":4,\"name\":\"Amplitude\",\"type\":\"int\","
         "\"min\":10,\"max\":90,\"default\":60,"
         "\"desc\":\"Wave height (% of display)\"},"
        "{\"id\":5,\"name\":\"Brightness\",\"type\":\"int\","
         "\"min\":1,\"max\":255,\"default\":210,"
         "\"desc\":\"Overall brightness\"},"
        "{\"id\":6,\"name\":\"Hue\",\"type\":\"int\","
         "\"min\":0,\"max\":255,\"default\":0,"
         "\"desc\":\"Base color (0 = rainbow)\"},"
        "{\"id\":7,\"name\":\"Noise\",\"type\":\"int\","
         "\"min\":0,\"max\":100,\"default\":0,"
         "\"desc\":\"Max noise amplitude (oscilloscope signal)\"},"
        "{\"id\":8,\"name\":\"Uniformity\",\"type\":\"int\","
         "\"min\":0,\"max\":100,\"default\":60,"
         "\"desc\":\"100 = constant noise, low = occasional spikes\"}"
    "]}";

EXPORT(get_meta_ptr)
int get_meta_ptr(void) { return (int)META; }

EXPORT(get_meta_len)
int get_meta_len(void) { return sizeof(META) - 1; }

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

/* sine of integer angle (256 per period), result -1..1 */
static float fsin(int angle) {
    return (float)sin_table[angle & 255] / 127.0f;
}

static float fabs_f(float x) { return x < 0.0f ? -x : x; }

/* integer hash -> well-mixed 32 bits (for per-column noise) */
static uint32_t hash32(uint32_t a) {
    a ^= a >> 16; a *= 0x7feb352du;
    a ^= a >> 15; a *= 0x846ca68bu;
    a ^= a >> 16;
    return a;
}

/* ---- HSV to RGB ---- */
static void hsv_to_rgb(int h, int s, int v, int *r, int *g, int *b) {
    h = h & 255;
    if (v < 0) v = 0; if (v > 255) v = 255;
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

/* ---- State ---- */
#define MAX_W     64
#define MAX_WAVES 5

static float wave_y[MAX_WAVES][MAX_W];   /* curve height per column */
static int   wave_r[MAX_WAVES];
static int   wave_g[MAX_WAVES];
static int   wave_b[MAX_WAVES];

static int32_t prev_tick;
static uint32_t t_acc;
static uint32_t frame_ctr;

EXPORT(init)
void init(void) {
    prev_tick = 0;
    t_acc = 0;
    frame_ctr = 0;
}

EXPORT(update)
void update(int tick_ms) {
    int speed  = get_param_i32(0);
    int count  = get_param_i32(1);
    int thick  = get_param_i32(2);
    int period = get_param_i32(3);
    int amp_p  = get_param_i32(4);
    int bright = get_param_i32(5);
    int hue_p  = get_param_i32(6);
    int noise_p = get_param_i32(7);
    int unif_p  = get_param_i32(8);

    int W = get_width();
    int H = get_height();
    if (W < 1) W = 1;
    if (H < 1) H = 1;
    if (W > MAX_W) W = MAX_W;
    if (count < 1) count = 1;
    if (count > MAX_WAVES) count = MAX_WAVES;
    /* Period is the wavelength in columns. While it fits the ring we show an
       integer number of cycles (seamless). When it is longer than the screen we
       can't draw <1 spatial cycle without a seam, so the excess turns into a slow
       temporal "carrier" that glides the whole trace up and down (see below). */
    if (period < 1) period = 1;
    if (period > 100) period = 100;
    int base_cycles = (W + period / 2) / period;    /* round(W / period) */
    if (base_cycles < 1) base_cycles = 1;
    if (base_cycles > 8) base_cycles = 8;

    if (thick < 1) thick = 1;
    if (thick > 6) thick = 6;
    if (noise_p < 0) noise_p = 0;
    if (unif_p < 0) unif_p = 0; if (unif_p > 100) unif_p = 100;

    frame_ctr++;

    /* noise: max vertical jitter (px) and how dense it is along the trace */
    float noise_amp = (float)noise_p * 0.01f * (float)(H - 1) * 0.5f;
    float density   = 0.04f + 0.96f * (float)unif_p * 0.01f;  /* low = sparse spikes */

    /* advance phase by real elapsed time */
    int32_t delta = tick_ms - prev_tick;
    if (delta <= 0 || delta > 200) delta = 33;
    prev_tick = tick_ms;
    t_acc += (uint32_t)(delta * speed);

    float center  = (float)(H - 1) * 0.5f;
    float amp_max = (float)(H - 1) * 0.5f;
    float amp_base = (float)amp_p * 0.01f * amp_max;
    int hue_drift = (int)(t_acc / 220) & 255;

    /* Carrier: when the wavelength is longer than the screen, the part that
       doesn't fit becomes a slow vertical glide of the whole trace over time.
       Longer period -> slower, larger glide; the wave shrinks to leave headroom. */
    float carrier_off = 0.0f;
    float amp_scale = 1.0f;
    if (period > W) {
        float overshoot = (float)period / (float)W;     /* > 1 */
        float cf = (overshoot - 1.0f) / 3.0f;           /* ramps 0..1 over period W..4W */
        if (cf > 1.0f) cf = 1.0f;
        amp_scale = 1.0f - 0.45f * cf;                  /* make room for the glide */
        uint32_t cdiv = (uint32_t)(40.0f * overshoot);  /* slower carrier for longer period */
        if (cdiv < 1u) cdiv = 1u;
        carrier_off = amp_max * 0.5f * cf * fsin((int)(t_acc / cdiv));
    }

    /* ---- 1. Compute each wave's curve + color ---- */
    for (int w = 0; w < count; w++) {
        int   wfreq = base_cycles + w;                  /* integer => seamless wrap */
        float amp   = amp_base * (1.0f - 0.12f * (float)w);
        if (amp < 1.0f) amp = 1.0f;
        int   ph    = (int)(t_acc / (uint32_t)(60 - w * 8 > 12 ? 60 - w * 8 : 12));
        if (w & 1) ph = -ph;                            /* alternate travel direction */

        for (int x = 0; x < W; x++) {
            int angle = wfreq * x * 256 / W + ph;
            float ny = 0.0f;
            if (noise_amp > 0.0f) {
                uint32_t h = hash32(((uint32_t)x * 73856093u)
                                  ^ ((uint32_t)w * 19349663u)
                                  ^ (frame_ctr * 83492791u));
                float gate = (float)(h & 0xFFFF) / 65536.0f;
                if (gate < density) {
                    float rn = (float)((h >> 16) & 0xFFFF) / 32768.0f - 1.0f; /* -1..1 */
                    ny = rn * noise_amp;
                }
            }
            wave_y[w][x] = center + carrier_off + amp * amp_scale * fsin(angle) + ny;
        }

        int hue;
        if (hue_p == 0) hue = (w * (256 / count) + hue_drift) & 255;
        else            hue = (hue_p + w * 40) & 255;
        hsv_to_rgb(hue, 255, bright, &wave_r[w], &wave_g[w], &wave_b[w]);
    }

    /* ---- 2. Render: glowing anti-aliased lines, additively blended ----
       Each column draws the segment toward its neighbour, so steep parts of
       the wave stay connected (no broken dots) and it wraps around the loop. */
    const float glow = 0.4f + (float)thick * 0.6f;   /* line half-thickness in px */

    for (int x = 0; x < W; x++) {
        int xn = (x + 1) % W;
        float lo[MAX_WAVES], hi[MAX_WAVES];
        for (int w = 0; w < count; w++) {
            float a = wave_y[w][x];
            float b2 = wave_y[w][xn];
            lo[w] = a < b2 ? a : b2;
            hi[w] = a < b2 ? b2 : a;
        }

        for (int y = 0; y < H; y++) {
            int r = 0, g = 0, b = 0;
            float fy = (float)y;

            for (int w = 0; w < count; w++) {
                float d;
                if (fy < lo[w])      d = lo[w] - fy;
                else if (fy > hi[w]) d = fy - hi[w];
                else                 d = 0.0f;     /* inside the segment span */
                if (d >= glow) continue;
                float inten = 1.0f - d / glow;
                inten *= inten;                    /* sharper, brighter core */
                r += (int)(wave_r[w] * inten);
                g += (int)(wave_g[w] * inten);
                b += (int)(wave_b[w] * inten);
            }

            if (r > 255) r = 255;
            if (g > 255) g = 255;
            if (b > 255) b = 255;
            set_pixel(x, y, r, g, b);
        }
    }

    draw();
}
