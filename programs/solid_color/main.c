#include "api.h"

/* ---- Metadata JSON ---- */
static const char META[] =
    "{\"name\":\"Solid Color\","
    "\"desc\":\"Static color with RGB control or presets\","
    "\"params\":["
        "{\"id\":0,\"name\":\"Preset\",\"type\":\"select\","
         "\"options\":[\"Custom\",\"Red\",\"Green\",\"Blue\","
         "\"White\",\"Warm White\",\"Purple\",\"Cyan\","
         "\"Orange\",\"Yellow\",\"Pink\"],"
         "\"default\":0,"
         "\"desc\":\"Color preset (Custom = use RGB sliders)\"},"
        "{\"id\":1,\"name\":\"Red\",\"type\":\"int\","
         "\"min\":0,\"max\":255,\"default\":255,"
         "\"desc\":\"Red channel (Custom mode)\"},"
        "{\"id\":2,\"name\":\"Green\",\"type\":\"int\","
         "\"min\":0,\"max\":255,\"default\":0,"
         "\"desc\":\"Green channel (Custom mode)\"},"
        "{\"id\":3,\"name\":\"Blue\",\"type\":\"int\","
         "\"min\":0,\"max\":255,\"default\":0,"
         "\"desc\":\"Blue channel (Custom mode)\"},"
        "{\"id\":4,\"name\":\"Brightness\",\"type\":\"int\","
         "\"min\":1,\"max\":255,\"default\":255,"
         "\"desc\":\"Overall brightness\"}"
    "]}";

EXPORT(get_meta_ptr)
int get_meta_ptr(void) {
    return (int)META;
}

EXPORT(get_meta_len)
int get_meta_len(void) {
    return sizeof(META) - 1;
}

/* Preset color table: {R, G, B} */
static const uint8_t PRESETS[][3] = {
    {  0,   0,   0},  /* 0 = Custom (unused, reads RGB params) */
    {255,   0,   0},  /* 1 = Red */
    {  0, 255,   0},  /* 2 = Green */
    {  0,   0, 255},  /* 3 = Blue */
    {255, 255, 255},  /* 4 = White */
    {255, 180, 100},  /* 5 = Warm White */
    {128,   0, 255},  /* 6 = Purple */
    {  0, 255, 255},  /* 7 = Cyan */
    {255, 100,   0},  /* 8 = Orange */
    {255, 255,   0},  /* 9 = Yellow */
    {255,  50, 120},  /* 10 = Pink */
};

#define NUM_PRESETS 11

static int last_preset = -1;

EXPORT(init)
void init(void) {
    last_preset = -1;
}

EXPORT(update)
void update(int tick_ms) {
    int preset     = get_param_i32(0);
    int brightness = get_param_i32(4);

    int r, g, b;

    if (preset > 0 && preset < NUM_PRESETS) {
        if (preset != last_preset) {
            /* Preset just changed — push preset RGB values to sliders */
            set_param_i32(1, PRESETS[preset][0]);
            set_param_i32(2, PRESETS[preset][1]);
            set_param_i32(3, PRESETS[preset][2]);
        } else {
            /* Preset active — if user changed any RGB, jump to Custom */
            r = get_param_i32(1);
            g = get_param_i32(2);
            b = get_param_i32(3);
            if (r != PRESETS[preset][0] ||
                g != PRESETS[preset][1] ||
                b != PRESETS[preset][2]) {
                set_param_i32(0, 0);
                preset = 0;
            }
        }
    }

    if (preset > 0 && preset < NUM_PRESETS) {
        r = PRESETS[preset][0];
        g = PRESETS[preset][1];
        b = PRESETS[preset][2];
    } else {
        r = get_param_i32(1);
        g = get_param_i32(2);
        b = get_param_i32(3);
    }

    last_preset = preset;

    /* Apply brightness */
    r = r * brightness / 255;
    g = g * brightness / 255;
    b = b * brightness / 255;

    int w = get_width();
    int h = get_height();

    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            set_pixel(x, y, r, g, b);

    draw();
}
