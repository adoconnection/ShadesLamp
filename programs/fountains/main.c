#include "api.h"

/*
 * Color Fountains — Multiple overlapping fountain waves.
 * Each wave launches all columns simultaneously to different heights.
 * New waves render on top of previous ones.
 * Asymmetric arc: fast explosive rise (30%), slow graceful fall (70%).
 * Y=0 is the bottom of the display.
 */

/* ---- Metadata JSON ---- */
static const char META[] =
    "{\"name\":\"Color Fountains\","
    "\"desc\":\"Overlapping fountain waves with dynamic rise and fade\","
    "\"params\":["
        "{\"id\":0,\"name\":\"Brightness\",\"type\":\"int\","
         "\"min\":1,\"max\":255,\"default\":200,"
         "\"desc\":\"Brightness of the fountain columns\"},"
        "{\"id\":1,\"name\":\"Fade\",\"type\":\"int\","
         "\"min\":1,\"max\":10,\"default\":10,"
         "\"desc\":\"Fade speed: 1=slow glow, 10=fast sharp fade\"},"
        "{\"id\":2,\"name\":\"Waves\",\"type\":\"int\","
         "\"min\":1,\"max\":5,\"default\":4,"
         "\"desc\":\"Number of overlapping fountain waves\"},"
        "{\"id\":3,\"name\":\"Wave Delay\",\"type\":\"int\","
         "\"min\":50,\"max\":2000,\"default\":500,"
         "\"desc\":\"Delay between individual waves within a group (ms)\"},"
        "{\"id\":4,\"name\":\"Group Delay\",\"type\":\"int\","
         "\"min\":500,\"max\":10000,\"default\":3000,"
         "\"desc\":\"Delay between groups of waves (ms)\"},"
        "{\"id\":5,\"name\":\"Randomness\",\"type\":\"int\","
         "\"min\":0,\"max\":100,\"default\":20,"
         "\"desc\":\"Random variation applied to wave and group delays (%)\"}"
    "]}";

EXPORT(get_meta_ptr)
int get_meta_ptr(void) { return (int)META; }

EXPORT(get_meta_len)
int get_meta_len(void) { return sizeof(META) - 1; }

/* ---- PRNG (xorshift32) ---- */
static uint32_t rng_state = 73291;

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

/* ---- HSV to RGB (hue 0-255, sat 0-255, val 0-255) ---- */
static void hsv_to_rgb(int h, int s, int v, int *r, int *g, int *b) {
    if (s == 0) {
        *r = *g = *b = v;
        return;
    }
    h = h & 0xFF;
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

/* ---- Multi-wave state ---- */
#define MAX_WAVES 5
#define MAX_W 64

static float wave_target[MAX_WAVES][MAX_W];  /* peak height per column */
static int   wave_hue[MAX_WAVES][MAX_W];     /* hue per column */
static float wave_phase[MAX_WAVES];           /* 0.0 -> 1.0 */
static int   wave_active[MAX_WAVES];          /* 1 = animating */
static int   wave_seq[MAX_WAVES];             /* launch sequence for render order */

static int32_t prev_tick;
static int32_t launch_timer;      /* countdown to next wave launch */
static int     next_hue;          /* hue for the next wave */
static int     seq_counter;       /* monotonically increasing launch counter */
static int     waves_in_group;    /* how many waves launched in current group */

/* Asymmetric arc: peak at 30% of cycle for explosive rise */
#define T_PEAK 0.3f

EXPORT(init)
void init(void) {
    rng_state = 73291;
    for (int w = 0; w < MAX_WAVES; w++)
        wave_active[w] = 0;
    prev_tick = 0;
    launch_timer = 0;
    next_hue = 0;
    seq_counter = 0;
    waves_in_group = 0;
}

/* Launch a single wave (all columns simultaneously) */
static void launch_wave(int w, int W, int H, int base_hue) {
    wave_active[w] = 1;
    wave_phase[w] = 0.0f;
    wave_seq[w] = seq_counter++;
    for (int x = 0; x < W; x++) {
        wave_target[w][x] = (float)H * (0.3f + (float)random_range(0, 71) / 100.0f);
        wave_hue[w][x] = (base_hue + random_range(-12, 13)) & 0xFF;
    }
}

EXPORT(update)
void update(int tick_ms) {
    int bright      = get_param_i32(0);   /* 1-255 */
    int fade_speed  = get_param_i32(1);   /* 1-10  */
    int num_waves   = get_param_i32(2);   /* 1-5   */
    int wave_delay  = get_param_i32(3);   /* 50-2000 ms */
    int group_delay = get_param_i32(4);   /* 500-10000 ms */
    int randomness  = get_param_i32(5);   /* 0-100 % */

    int W = get_width();
    int H = get_height();
    if (W > MAX_W) W = MAX_W;
    if (W < 1) W = 1;
    if (H < 1) H = 1;
    if (num_waves > MAX_WAVES) num_waves = MAX_WAVES;
    if (num_waves < 1) num_waves = 1;

    rng_state ^= (uint32_t)tick_ms;

    /* Compute delta time */
    int32_t delta_ms = tick_ms - prev_tick;
    if (delta_ms <= 0 || delta_ms > 200) delta_ms = 33;
    prev_tick = tick_ms;
    float dt = (float)delta_ms / 1000.0f;

    /* ---- Launch logic: find a free slot when timer fires ---- */
    launch_timer -= delta_ms;
    if (launch_timer <= 0) {
        int slot = -1;
        for (int w = 0; w < num_waves; w++) {
            if (!wave_active[w]) { slot = w; break; }
        }
        if (slot >= 0) {
            launch_wave(slot, W, H, next_hue);
            /* Next wave gets a noticeably different hue */
            next_hue = (next_hue + random_range(35, 65)) & 0xFF;
            waves_in_group++;

            /* Pick delay: wave delay within group, group delay after full group */
            int base_delay;
            if (waves_in_group >= num_waves) {
                base_delay = group_delay;
                waves_in_group = 0;
            } else {
                base_delay = wave_delay;
            }

            /* Apply randomness: actual = base +/- (base * randomness / 100) */
            int variation = base_delay * randomness / 100;
            int jitter = (variation > 0)
                ? random_range(-variation, variation + 1)
                : 0;
            launch_timer = base_delay + jitter;
            if (launch_timer < 1) launch_timer = 1;
        }
        /* If no slot free, retry next frame (launch_timer stays <= 0) */
    }

    /* ---- Clear display ---- */
    for (int x = 0; x < W; x++)
        for (int y = 0; y < H; y++)
            set_pixel(x, y, 0, 0, 0);

    /* Animation speed */
    float phase_speed = 0.45f;

    /* ---- Update phases ---- */
    for (int w = 0; w < num_waves; w++) {
        if (!wave_active[w]) continue;
        wave_phase[w] += phase_speed * dt;
        if (wave_phase[w] >= 1.0f)
            wave_active[w] = 0;
    }

    /* ---- Build render order: oldest (lowest seq) first, newest last ---- */
    int order[MAX_WAVES];
    int count = 0;
    for (int w = 0; w < num_waves; w++) {
        if (wave_active[w]) order[count++] = w;
    }
    for (int i = 0; i < count - 1; i++) {
        for (int j = i + 1; j < count; j++) {
            if (wave_seq[order[j]] < wave_seq[order[i]]) {
                int tmp = order[i]; order[i] = order[j]; order[j] = tmp;
            }
        }
    }

    /* ---- Render waves in order (newest overwrites oldest) ---- */
    for (int idx = 0; idx < count; idx++) {
        int w = order[idx];
        float t = wave_phase[w];

        /* Asymmetric arc: remap t so peak is at T_PEAK of cycle.
         * Rise [0, T_PEAK] -> parabola phase [0, 0.5]  (fast)
         * Fall [T_PEAK, 1] -> parabola phase [0.5, 1.0] (slow) */
        float arc_phase;
        if (t < T_PEAK) {
            arc_phase = 0.5f * t / T_PEAK;
        } else {
            arc_phase = 0.5f + 0.5f * (t - T_PEAK) / (1.0f - T_PEAK);
        }
        float y_factor = 4.0f * arc_phase * (1.0f - arc_phase);

        /* Fade multiplier: full during rise, fades during fall */
        float bmul = 1.0f;
        if (t > T_PEAK) {
            float fall_progress = (t - T_PEAK) / (1.0f - T_PEAK);  /* 0->1 */
            float raw = 1.0f - fall_progress;                       /* 1->0 */
            bmul = raw;
            int extra = fade_speed / 3;
            for (int i = 0; i < extra; i++) bmul *= raw;
        }

        /* Render this wave's columns */
        for (int x = 0; x < W; x++) {
            float cur_y = wave_target[w][x] * y_factor;
            int head_y = (int)cur_y;
            if (head_y >= H) head_y = H - 1;
            if (head_y < 0) continue;

            int hue = wave_hue[w][x];

            for (int y = 0; y <= head_y; y++) {
                /* Gradient: brighter near head, min 15% at base */
                float grad;
                if (head_y > 0)
                    grad = 0.15f + 0.85f * (float)y / (float)head_y;
                else
                    grad = 1.0f;

                int val = (int)((float)bright * bmul * grad);
                if (val > 255) val = 255;
                if (val < 1) continue;

                int sat = (y == head_y) ? 140 : 220;
                int r, g, b;
                hsv_to_rgb(hue, sat, val, &r, &g, &b);
                set_pixel(x, y, r, g, b);
            }
        }
    }

    draw();
}
