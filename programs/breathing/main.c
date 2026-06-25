#include "api.h"

/* ---- Metadata JSON ---- */
static const char META[] =
    "{\"name\":\"Breathing\","
    "\"desc\":\"Lifelike breathing or a lub-dub heartbeat glow\","
    "\"params\":["
        "{\"id\":0,\"name\":\"Hue\",\"type\":\"int\","
         "\"min\":0,\"max\":255,\"default\":140,"
         "\"desc\":\"Color hue (140=cyan)\"},"
        "{\"id\":1,\"name\":\"Saturation\",\"type\":\"int\","
         "\"min\":0,\"max\":255,\"default\":255,"
         "\"desc\":\"Color saturation (0=white pulse)\"},"
        "{\"id\":2,\"name\":\"Speed\",\"type\":\"int\","
         "\"min\":1,\"max\":100,\"default\":30,"
         "\"desc\":\"Breathing / heart rate\"},"
        "{\"id\":3,\"name\":\"Max Brightness\",\"type\":\"int\","
         "\"min\":1,\"max\":255,\"default\":200,"
         "\"desc\":\"Peak brightness\"},"
        "{\"id\":4,\"name\":\"Mode\",\"type\":\"select\","
         "\"options\":[\"Breath\",\"Heartbeat\"],"
         "\"default\":0,"
         "\"desc\":\"Breathing rhythm or a beating heart\"}"
    "]}";

EXPORT(get_meta_ptr)
int get_meta_ptr(void) { return (int)META; }

EXPORT(get_meta_len)
int get_meta_len(void) { return sizeof(META) - 1; }

/* ---- HSV to RGB ---- */
static void hsv2rgb(int hue, int sat, int val, int *r, int *g, int *b) {
    if (val == 0) { *r = *g = *b = 0; return; }
    if (sat == 0) { *r = *g = *b = val; return; }
    int h = hue & 0xFF;
    int region = h / 43;
    int frac = (h - region * 43) * 6;
    int p = (val * (255 - sat)) >> 8;
    int q = (val * (255 - ((sat * frac) >> 8))) >> 8;
    int t = (val * (255 - ((sat * (255 - frac)) >> 8))) >> 8;
    switch (region) {
        case 0:  *r = val; *g = t;   *b = p;   break;
        case 1:  *r = q;   *g = val; *b = p;   break;
        case 2:  *r = p;   *g = val; *b = t;   break;
        case 3:  *r = p;   *g = q;   *b = val; break;
        case 4:  *r = t;   *g = p;   *b = val; break;
        default: *r = val; *g = p;   *b = q;   break;
    }
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

EXPORT(update)
void update(int tick_ms) {
    int hue        = get_param_i32(0);
    int sat        = get_param_i32(1);
    int speed      = get_param_i32(2);
    int max_bright = get_param_i32(3);
    int mode       = get_param_i32(4);   /* 0 = breath, 1 = heartbeat */
    int W = get_width();
    int H = get_height();

    /* Cycles per millisecond. Tuned so speed=30 ~ a calm 2.6s breath.
       Heartbeat runs ~3x faster so the same speed reads like a real pulse. */
    float inc = (float)speed * 0.0000127f;
    if (mode == 1) inc *= 3.0f;

    float p = fracf((float)tick_ms * inc);

    float v = (mode == 1) ? heart_curve(p) : breath_curve(p);

    /* Keep a faint glow at the trough so the lamp never goes fully black */
    const float floor_v = 0.05f;
    v = floor_v + v * (1.0f - floor_v);

    int val = (int)(v * (float)max_bright + 0.5f);
    if (val < 0)   val = 0;
    if (val > 255) val = 255;

    int r, g, b;
    hsv2rgb(hue, sat, val, &r, &g, &b);

    for (int x = 0; x < W; x++) {
        for (int y = 0; y < H; y++) {
            set_pixel(x, y, r, g, b);
        }
    }

    draw();
}
