#include "api.h"

/* ---- Metadata JSON ---- */
static const char META[] =
    "{\"name\":\"Fireflies\","
    "\"desc\":\"Random floating bright dots that drift and fade\","
    "\"params\":["
        "{\"id\":0,\"name\":\"Count\",\"type\":\"int\","
         "\"min\":1,\"max\":20,\"default\":8,"
         "\"desc\":\"Number of fireflies\"},"
        "{\"id\":1,\"name\":\"Speed\",\"type\":\"int\","
         "\"min\":1,\"max\":10,\"default\":5,"
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
static uint32_t rng_state = 92731;

static uint32_t rng_next(void) {
    uint32_t x = rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng_state = x;
    return x;
}

static int random8(void) {
    return (int)(rng_next() & 0xFF);
}

static int random_range(int lo, int hi) {
    if (lo >= hi) return lo;
    return lo + (int)(rng_next() % (uint32_t)(hi - lo));
}

/* ---- HSV to RGB ---- */
static void hsv_to_rgb(int h, int s, int v, int *r, int *g, int *b) {
    if (s == 0) {
        *r = *g = *b = v;
        return;
    }
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

/* ---- Firefly state ---- */
#define MAX_FLIES 20

/* Position in 10ths of a pixel for smooth subpixel movement */
static float fly_x[MAX_FLIES];
static float fly_y[MAX_FLIES];
static float fly_vx[MAX_FLIES];
static float fly_vy[MAX_FLIES];
static int   fly_hue[MAX_FLIES];
static int   fly_brightness[MAX_FLIES]; /* individual brightness for fade in/out */
static int   fly_phase[MAX_FLIES];      /* 0=fading in, 1=alive, 2=fading out */
static int   fly_life[MAX_FLIES];       /* ticks remaining in current phase */

static int step_counter;

EXPORT(init)
void init(void) {
    int W = get_width();
    int H = get_height();

    step_counter = 0;

    for (int i = 0; i < MAX_FLIES; i++) {
        fly_x[i] = (float)random_range(0, W * 10) / 10.0f;
        fly_y[i] = (float)random_range(0, H * 10) / 10.0f;
        fly_vx[i] = (float)random_range(-10, 11) / 10.0f;
        fly_vy[i] = (float)random_range(-10, 11) / 10.0f;
        fly_hue[i] = random8();
        fly_brightness[i] = 0;
        fly_phase[i] = 0;  /* start fading in */
        fly_life[i] = random_range(10, 40);
    }
}

static float f_abs(float x) {
    return x < 0.0f ? -x : x;
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
    if (count > MAX_FLIES) count = MAX_FLIES;
    if (count < 1) count = 1;

    rng_state ^= (uint32_t)tick_ms;

    float speed_factor = (float)speed / 5.0f;

    step_counter++;

    /* Clear screen */
    for (int x = 0; x < W; x++)
        for (int y = 0; y < H; y++)
            set_pixel(x, y, 0, 0, 0);

    for (int i = 0; i < count; i++) {
        /* Periodically adjust velocity (every ~20 frames) for organic drift */
        if ((step_counter % 20) == (i % 20)) {
            fly_vx[i] += (float)random_range(-3, 4) / 10.0f;
            fly_vy[i] += (float)random_range(-3, 4) / 10.0f;
            /* Clamp velocity */
            if (fly_vx[i] > 2.0f) fly_vx[i] = 2.0f;
            if (fly_vx[i] < -2.0f) fly_vx[i] = -2.0f;
            if (fly_vy[i] > 2.0f) fly_vy[i] = 2.0f;
            if (fly_vy[i] < -2.0f) fly_vy[i] = -2.0f;
        }

        /* Move */
        fly_x[i] += fly_vx[i] * speed_factor * 0.1f;
        fly_y[i] += fly_vy[i] * speed_factor * 0.1f;

        /* Wrap horizontally (cylinder) */
        if (fly_x[i] < 0.0f) fly_x[i] += (float)W;
        if (fly_x[i] >= (float)W) fly_x[i] -= (float)W;

        /* Bounce vertically */
        if (fly_y[i] < 0.0f) {
            fly_y[i] = -fly_y[i];
            fly_vy[i] = f_abs(fly_vy[i]);
        }
        if (fly_y[i] >= (float)(H - 1)) {
            fly_y[i] = (float)(H - 1) * 2.0f - fly_y[i];
            fly_vy[i] = -f_abs(fly_vy[i]);
        }

        /* Phase/lifecycle management */
        fly_life[i]--;
        if (fly_life[i] <= 0) {
            fly_phase[i]++;
            if (fly_phase[i] > 2) {
                /* Respawn */
                fly_phase[i] = 0;
                fly_x[i] = (float)random_range(0, W);
                fly_y[i] = (float)random_range(0, H);
                fly_vx[i] = (float)random_range(-10, 11) / 10.0f;
                fly_vy[i] = (float)random_range(-10, 11) / 10.0f;
                fly_hue[i] = random8();
            }
            fly_life[i] = random_range(15, 60);
        }

        /* Calculate brightness based on phase */
        switch (fly_phase[i]) {
            case 0: { /* Fading in */
                int max_life = 30;
                int elapsed = max_life - fly_life[i];
                if (elapsed < 0) elapsed = 0;
                fly_brightness[i] = bright * elapsed / max_life;
                if (fly_brightness[i] > bright) fly_brightness[i] = bright;
                break;
            }
            case 1: /* Alive - full brightness */
                fly_brightness[i] = bright;
                break;
            case 2: { /* Fading out */
                fly_brightness[i] = bright * fly_life[i] / 30;
                if (fly_brightness[i] < 0) fly_brightness[i] = 0;
                break;
            }
        }

        /* Draw */
        int px = (int)fly_x[i];
        int py = (int)fly_y[i];
        if (px < 0) px = 0;
        if (px >= W) px = W - 1;
        if (py < 0) py = 0;
        if (py >= H) py = H - 1;

        int r, g, b;
        /* Warm yellow-green hues typical for fireflies */
        int hue = (fly_hue[i] % 64) + 32; /* restrict to yellow-green range (32-96) */
        hsv_to_rgb(hue, 255, fly_brightness[i], &r, &g, &b);
        set_pixel(px, py, r, g, b);

        /* Draw soft glow around the firefly (dimmer neighbors) */
        int glow = fly_brightness[i] / 4;
        if (glow > 5) {
            int gr, gg, gb;
            hsv_to_rgb(hue, 200, glow, &gr, &gg, &gb);
            /* 4 neighbors */
            int nx, ny;
            nx = (px - 1 + W) % W;
            set_pixel(nx, py, gr, gg, gb);
            nx = (px + 1) % W;
            set_pixel(nx, py, gr, gg, gb);
            ny = py - 1;
            if (ny >= 0) set_pixel(px, ny, gr, gg, gb);
            ny = py + 1;
            if (ny < H) set_pixel(px, ny, gr, gg, gb);
        }
    }

    draw();
}
