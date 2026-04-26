#include "api.h"

/* ---- Metadata JSON ---- */
static const char META[] =
    "{\"name\":\"Breathing\","
    "\"desc\":\"Smooth pulsing glow — the entire lamp breathes in and out\","
    "\"params\":["
        "{\"id\":0,\"name\":\"Hue\",\"type\":\"int\","
         "\"min\":0,\"max\":255,\"default\":140,"
         "\"desc\":\"Color hue (140=cyan)\"},"
        "{\"id\":1,\"name\":\"Saturation\",\"type\":\"int\","
         "\"min\":0,\"max\":255,\"default\":255,"
         "\"desc\":\"Color saturation (0=white pulse)\"},"
        "{\"id\":2,\"name\":\"Speed\",\"type\":\"int\","
         "\"min\":1,\"max\":100,\"default\":30,"
         "\"desc\":\"Breathing rate\"},"
        "{\"id\":3,\"name\":\"Max Brightness\",\"type\":\"int\","
         "\"min\":1,\"max\":255,\"default\":200,"
         "\"desc\":\"Peak brightness\"}"
    "]}";

EXPORT(get_meta_ptr)
int get_meta_ptr(void) { return (int)META; }

EXPORT(get_meta_len)
int get_meta_len(void) { return sizeof(META) - 1; }

/* ---- Sine approximation (Bhaskara I) ---- */
#define TWO_PI 6.28318530f
#define PI     3.14159265f

static float fsin(float x) {
    while (x < 0.0f)    x += TWO_PI;
    while (x >= TWO_PI) x -= TWO_PI;
    float sign = 1.0f;
    if (x > PI) { x -= PI; sign = -1.0f; }
    float num = 16.0f * x * (PI - x);
    float den = 5.0f * PI * PI - 4.0f * x * (PI - x);
    if (den == 0.0f) return 0.0f;
    return sign * num / den;
}

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
    int W = get_width();
    int H = get_height();

    /* Breathing cycle:
       speed 1-100 maps to a period.
       speed=1  -> very slow (~10s cycle)
       speed=30 -> ~3s cycle (comfortable breathing)
       speed=100 -> fast (~0.6s cycle)

       Period = 1 / (speed * 0.01) seconds roughly.
       We use: angular_freq = speed * 0.001 (radians per ms)
       At speed=30: freq = 0.03 rad/ms -> period = TWO_PI / 0.03 = ~209ms * TWO_PI ~ 2.1s

       Let's use: phase = tick_ms * speed * 0.001
       speed=1:   period = TWO_PI / 0.001 = 6283ms ~ 6.3s
       speed=30:  period = TWO_PI / 0.030 = 209ms  (too fast)

       Better mapping: phase = tick_ms * speed * 0.0002
       speed=1:   period = TWO_PI / 0.0002 = 31415ms ~ 31s
       speed=30:  period = TWO_PI / 0.006  = 1047ms  ~ 1s
       speed=100: period = TWO_PI / 0.020  = 314ms   (very fast)

       Even better: phase = tick_ms * speed * 0.00008
       speed=1:   period ~ 78.5s (very slow)
       speed=30:  period ~ 2.6s  (nice breathing)
       speed=50:  period ~ 1.6s
       speed=100: period ~ 0.8s  (quick)
    */
    float phase = (float)tick_ms * (float)speed * 0.00008f;

    /* sin goes -1..1, map to 0..1 for brightness */
    float breath = fsin(phase);
    /* Map from -1..1 to 0..1 */
    breath = (breath + 1.0f) * 0.5f;

    /* Apply a gentle power curve for more natural breathing feel:
       slow rise, pause at peak, slow fall, pause at bottom */
    /* Using smoothstep-like: 3x^2 - 2x^3 */
    breath = breath * breath * (3.0f - 2.0f * breath);

    /* Scale to brightness range */
    int val = (int)(breath * (float)max_bright);
    if (val < 0)   val = 0;
    if (val > 255) val = 255;

    /* Convert HSV to RGB once for the whole frame */
    int r, g, b;
    hsv2rgb(hue, sat, val, &r, &g, &b);

    /* Fill all pixels with the same color */
    for (int x = 0; x < W; x++) {
        for (int y = 0; y < H; y++) {
            set_pixel(x, y, r, g, b);
        }
    }

    draw();
}
