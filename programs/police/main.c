#include "api.h"

/* ---- Metadata JSON ---- */
static const char META[] =
    "{\"name\":\"Police Siren\","
    "\"desc\":\"Classic police/emergency flasher with alternating left-right strobe\","
    "\"params\":["
        "{\"id\":0,\"name\":\"Preset\",\"type\":\"select\","
         "\"options\":[\"Blue\",\"Blue-White\",\"Red-Blue\",\"Orange\"],"
         "\"default\":2,"
         "\"desc\":\"Color scheme for the flasher\"},"
        "{\"id\":1,\"name\":\"Speed\",\"type\":\"int\","
         "\"min\":1,\"max\":100,\"default\":50,"
         "\"desc\":\"Flash speed\"},"
        "{\"id\":2,\"name\":\"Brightness\",\"type\":\"int\","
         "\"min\":1,\"max\":255,\"default\":255,"
         "\"desc\":\"Overall brightness\"}"
    "]}";

EXPORT(get_meta_ptr)
int get_meta_ptr(void) { return (int)META; }

EXPORT(get_meta_len)
int get_meta_len(void) { return sizeof(META) - 1; }

/* ---- Preset color table ---- */
/* Each preset has two colors: left side and right side */
/* Format: { left_r, left_g, left_b, right_r, right_g, right_b } */
static const uint8_t PRESETS[][6] = {
    {  0,   0, 255,    0,   0, 255},  /* 0 = Blue */
    {  0,   0, 255,  255, 255, 255},  /* 1 = Blue-White */
    {255,   0,   0,    0,   0, 255},  /* 2 = Red-Blue */
    {255, 140,   0,  255, 140,   0},  /* 3 = Orange */
};

/* ---- State ---- */
static uint32_t elapsed;

EXPORT(init)
void init(void) {
    elapsed = 0;
}

EXPORT(update)
void update(int tick_ms) {
    int preset = get_param_i32(0);
    int speed  = get_param_i32(1);
    int bright = get_param_i32(2);
    int W = get_width();
    int H = get_height();

    /* Clamp preset */
    if (preset < 0) preset = 0;
    if (preset > 3) preset = 3;

    /* Use total elapsed time directly (tick_ms is absolute, not delta) */
    elapsed = (uint32_t)tick_ms;

    /* Get colors for this preset */
    int lr = PRESETS[preset][0];
    int lg = PRESETS[preset][1];
    int lb = PRESETS[preset][2];
    int rr = PRESETS[preset][3];
    int rg = PRESETS[preset][4];
    int rb = PRESETS[preset][5];

    /* Apply brightness */
    lr = lr * bright / 255;
    lg = lg * bright / 255;
    lb = lb * bright / 255;
    rr = rr * bright / 255;
    rg = rg * bright / 255;
    rb = rb * bright / 255;

    /* Compute cycle period from speed.
       Speed 1 = slow (800ms cycle), speed 100 = fast (120ms cycle).
       Linear interpolation: period = 800 - (speed - 1) * 680 / 99 */
    int period = 800 - (speed - 1) * 680 / 99;
    if (period < 120) period = 120;
    int half = period / 2;

    /* Phase within the current cycle */
    int phase = (int)(elapsed % (uint32_t)period);

    /* Strobe pattern within each half-cycle:
       We do 2 rapid flashes within the ON half.
       Pattern: ON 60ms, OFF 40ms, ON 60ms, OFF for remainder.
       Total strobe active = 160ms. If half < 160ms, scale down. */
    int strobe_on1_end = 60;
    int strobe_off1_end = 100;
    int strobe_on2_end = 160;

    /* If the half-period is shorter than the strobe pattern, compress it */
    if (half < 160) {
        /* Scale proportionally */
        strobe_on1_end = half * 60 / 160;
        strobe_off1_end = half * 100 / 160;
        strobe_on2_end = half * 160 / 160;
    }

    /* Determine which side is active and whether we're in a strobe-ON moment */
    int left_on = 0;
    int right_on = 0;

    if (phase < half) {
        /* Left side is in strobe mode */
        int t = phase;
        if (t < strobe_on1_end) {
            left_on = 1;  /* First flash */
        } else if (t < strobe_off1_end) {
            left_on = 0;  /* Gap */
        } else if (t < strobe_on2_end) {
            left_on = 1;  /* Second flash */
        } else {
            left_on = 0;  /* Dark until switch */
        }
    } else {
        /* Right side is in strobe mode */
        int t = phase - half;
        if (t < strobe_on1_end) {
            right_on = 1;  /* First flash */
        } else if (t < strobe_off1_end) {
            right_on = 0;  /* Gap */
        } else if (t < strobe_on2_end) {
            right_on = 1;  /* Second flash */
        } else {
            right_on = 0;  /* Dark until switch */
        }
    }

    /* Split the matrix into left and right halves */
    int mid = W / 2;

    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            if (x < mid) {
                /* Left half */
                if (left_on) {
                    set_pixel(x, y, lr, lg, lb);
                } else {
                    set_pixel(x, y, 0, 0, 0);
                }
            } else {
                /* Right half */
                if (right_on) {
                    set_pixel(x, y, rr, rg, rb);
                } else {
                    set_pixel(x, y, 0, 0, 0);
                }
            }
        }
    }

    draw();
}
