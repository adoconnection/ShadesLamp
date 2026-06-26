#include "api.h"

/* ---- Metadata JSON ---- */
static const char META[] =
    "{\"name\":\"Breathing\","
    "\"desc\":\"Lifelike breathing or a lub-dub heartbeat glow\","
    "\"params\":["
        "{\"id\":0,\"name\":\"Inhale Hue\",\"type\":\"int\","
         "\"min\":0,\"max\":255,\"default\":140,"
         "\"desc\":\"Color while inhaling (140=cyan)\"},"
        "{\"id\":1,\"name\":\"Exhale Hue\",\"type\":\"int\","
         "\"min\":0,\"max\":255,\"default\":20,"
         "\"desc\":\"Color while exhaling (20=amber)\"},"
        "{\"id\":2,\"name\":\"Saturation\",\"type\":\"int\","
         "\"min\":0,\"max\":255,\"default\":255,"
         "\"desc\":\"Color saturation (peak always goes white)\"},"
        "{\"id\":3,\"name\":\"Speed\",\"type\":\"int\","
         "\"min\":1,\"max\":100,\"default\":30,"
         "\"desc\":\"Breathing / heart rate\"},"
        "{\"id\":4,\"name\":\"Max Brightness\",\"type\":\"int\","
         "\"min\":1,\"max\":255,\"default\":200,"
         "\"desc\":\"Peak brightness\"},"
        "{\"id\":5,\"name\":\"Mode\",\"type\":\"select\","
         "\"options\":[\"Breath\",\"Heartbeat\"],"
         "\"default\":0,"
         "\"desc\":\"Breathing rhythm or a beating heart\"}"
    "]}";

EXPORT(get_meta_ptr)
int get_meta_ptr(void) { return (int)META; }

EXPORT(get_meta_len)
int get_meta_len(void) { return sizeof(META) - 1; }

/* ---- HSV to RGB (native host primitive) ---- */
static void hsv2rgb(int hue, int sat, int val, int *r, int *g, int *b) {
    int c = m_hsv(hue & 0xFF, sat, val);
    *r = (c >> 16) & 255;
    *g = (c >> 8) & 255;
    *b = c & 255;
}

/* ---- Helpers ---- */

/* fractional part in [0,1) */
static float fracf(float x) {
    x -= (float)((int)x);
    if (x < 0.0f) x += 1.0f;
    return x;
}

/* smoothstep clamped to [0,1] */
static float smoothstep01(float t) {
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    return t * t * (3.0f - 2.0f * t);
}

/* A single smooth pulse: fast attack, slower decay.
   p     - cycle position in [0,1)
   start - where the pulse begins
   width - how long it lasts
   amp   - peak height */
static float pulse(float p, float start, float width, float amp) {
    float l = (p - start) / width;
    if (l < 0.0f || l > 1.0f) return 0.0f;
    float s;
    if (l < 0.30f) s = l / 0.30f;             /* quick rise */
    else           s = 1.0f - (l - 0.30f) / 0.70f; /* gentler fall */
    return amp * smoothstep01(s);
}

/* Respiration: quick inhale, hold, slow exhale, rest with near-empty lungs */
static float breath_curve(float p) {
    if (p < 0.34f)       return smoothstep01(p / 0.34f);            /* inhale */
    else if (p < 0.42f)  return 1.0f;                              /* hold full */
    else if (p < 0.80f)  return 1.0f - smoothstep01((p - 0.42f) / 0.38f); /* exhale */
    else                 return 0.0f;                              /* pause */
}

/* Heartbeat: a strong "lub", a softer "dub", then a long diastole pause */
static float heart_curve(float p) {
    float a = pulse(p, 0.00f, 0.16f, 1.00f);   /* lub */
    float b = pulse(p, 0.20f, 0.14f, 0.55f);   /* dub */
    return a > b ? a : b;
}

EXPORT(init)
void init(void) {
    /* Nothing to initialize */
}

static float prev_vc = 0.0f;   /* previous raw curve value, for inhale/exhale detection */

EXPORT(update)
void update(int tick_ms) {
    int inhale_hue = get_param_i32(0);
    int exhale_hue = get_param_i32(1);
    int sat        = get_param_i32(2);
    int speed      = get_param_i32(3);
    int max_bright = get_param_i32(4);
    int mode       = get_param_i32(5);   /* 0 = breath, 1 = heartbeat */
    int W = get_width();
    int H = get_height();

    /* Cycles per millisecond. Tuned so speed=30 ~ a calm 2.6s breath.
       Heartbeat runs ~3x faster so the same speed reads like a real pulse. */
    float inc = (float)speed * 0.0000127f;
    if (mode == 1) inc *= 3.0f;

    float p = fracf((float)tick_ms * inc);

    /* raw curve 0..1 — used for both brightness and the colour journey */
    float vc = (mode == 1) ? heart_curve(p) : breath_curve(p);

    /* inhale = rising, exhale = falling */
    int rising = (vc >= prev_vc);
    prev_vc = vc;

    /* Keep a faint glow at the trough so the lamp never goes fully black */
    const float floor_v = 0.05f;
    float v = floor_v + vc * (1.0f - floor_v);

    int val = (int)(v * (float)max_bright + 0.5f);
    if (val < 0)   val = 0;
    if (val > 255) val = 255;

    /* Colour passes through three stops per cycle:
       inhale colour -> white (at the peak) -> exhale colour.
       Saturation falls to 0 as the breath nears its peak. */
    int hue = rising ? inhale_hue : exhale_hue;
    int sat_eff = (int)((float)sat * (1.0f - vc));
    if (sat_eff < 0) sat_eff = 0;

    int r, g, b;
    hsv2rgb(hue, sat_eff, val, &r, &g, &b);

    for (int x = 0; x < W; x++) {
        for (int y = 0; y < H; y++) {
            set_pixel(x, y, r, g, b);
        }
    }

    draw();
}
