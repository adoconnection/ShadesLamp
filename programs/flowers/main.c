#include "api.h"

/*
 * Flowers — Flowers sprout from the bottom, stems grow upward,
 * buds bloom and eventually wilt. Y=0 is the bottom.
 * Types: Tulip, Daisy, Rose, Mix.
 */

/* ---- Metadata JSON ---- */
static const char META[] =
    "{\"name\":\"Flowers\","
    "\"desc\":\"Flowers sprout, grow stems, bloom and wilt in a cycle\","
    "\"params\":["
        "{\"id\":0,\"name\":\"Flower\",\"type\":\"select\","
         "\"options\":[\"Tulip\",\"Daisy\",\"Rose\",\"Mix\"],"
         "\"default\":3,"
         "\"desc\":\"Flower type or mix of all\"},"
        "{\"id\":1,\"name\":\"Count\",\"type\":\"int\","
         "\"min\":1,\"max\":10,\"default\":5,"
         "\"desc\":\"Number of simultaneous flowers\"},"
        "{\"id\":2,\"name\":\"Size\",\"type\":\"int\","
         "\"min\":1,\"max\":5,\"default\":2,"
         "\"desc\":\"Flower head size\"},"
        "{\"id\":3,\"name\":\"Speed\",\"type\":\"int\","
         "\"min\":1,\"max\":100,\"default\":40,"
         "\"desc\":\"Growth and bloom speed\"}"
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

static float random_float(void) {
    return (float)(rng_next() & 0xFFFF) / 65536.0f;
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

/* ---- Constants ---- */
#define MAX_FLOWERS 10

#define PHASE_INACTIVE 0
#define PHASE_GROWING  1
#define PHASE_BLOOMING 2
#define PHASE_FULL     3
#define PHASE_WILTING  4

#define TYPE_TULIP 0
#define TYPE_DAISY 1
#define TYPE_ROSE  2

/* ---- Flower state (parallel arrays) ---- */
static int   fl_x[MAX_FLOWERS];
static int   fl_phase[MAX_FLOWERS];
static float fl_stem_y[MAX_FLOWERS];
static int   fl_target_h[MAX_FLOWERS];
static float fl_bloom[MAX_FLOWERS];
static int   fl_type[MAX_FLOWERS];
static int   fl_hue[MAX_FLOWERS];
static float fl_timer[MAX_FLOWERS];
static int   fl_leaf_side[MAX_FLOWERS];
static int   fl_stem_hue[MAX_FLOWERS];  /* slight green variation */
static float fl_fade[MAX_FLOWERS];      /* fade-out factor for wilting (1.0→0.0) */

/* ---- Timing ---- */
static int32_t prev_tick;

/* ---- Clamp helper ---- */
static int clamp(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

/* ---- Choose hue for flower type ---- */
static int pick_hue(int type) {
    switch (type) {
    case TYPE_TULIP: {
        /* red, pink, yellow, purple */
        int choice = random_range(0, 4);
        if (choice == 0) return 0;         /* red */
        if (choice == 1) return 220;       /* pink */
        if (choice == 2) return 40;        /* yellow */
        return 192;                        /* purple */
    }
    case TYPE_DAISY:
        return 0; /* white petals, hue unused for petals */
    case TYPE_ROSE: {
        /* red to pink range */
        int choice = random_range(0, 3);
        if (choice == 0) return 0;    /* red */
        if (choice == 1) return 245;  /* deep pink */
        return 225;                   /* pink */
    }
    default:
        return 0;
    }
}

/* ---- Spawn a flower ---- */
static void spawn_flower(int i, int W, int H, int flower_param) {
    fl_phase[i] = PHASE_GROWING;
    fl_stem_y[i] = 0.0f;
    fl_bloom[i] = 0.0f;
    fl_timer[i] = 0.0f;

    /* Pick X avoiding too-close neighbors */
    int attempts = 0;
    int x;
    do {
        x = random_range(1, W - 1);
        int ok = 1;
        for (int j = 0; j < MAX_FLOWERS; j++) {
            if (j == i || fl_phase[j] == PHASE_INACTIVE) continue;
            int diff = x - fl_x[j];
            if (diff < 0) diff = -diff;
            if (diff < 2) { ok = 0; break; }
        }
        if (ok) break;
        attempts++;
    } while (attempts < 20);
    fl_x[i] = x;

    /* Target height: 40-80% of H */
    fl_target_h[i] = H * 40 / 100 + random_range(0, H * 40 / 100 + 1);
    if (fl_target_h[i] < 3) fl_target_h[i] = 3;

    /* Flower type */
    if (flower_param == 3) {
        /* Mix */
        fl_type[i] = random_range(0, 3);
    } else {
        fl_type[i] = flower_param;
    }

    fl_hue[i] = pick_hue(fl_type[i]);
    fl_leaf_side[i] = random_range(0, 2);
    fl_stem_hue[i] = 75 + random_range(0, 20);  /* green 75-95 hue variation */
    fl_fade[i] = 1.0f;
}

/* ---- Set pixel with bounds check ---- */
static void safe_pixel(int x, int y, int r, int g, int b, int W, int H) {
    if (x >= 0 && x < W && y >= 0 && y < H)
        set_pixel(x, y, r, g, b);
}

/* ---- Render a stem ---- */
static void render_stem(int i, int W, int H, float fade) {
    int x = fl_x[i];
    int stem_h = (int)fl_stem_y[i];
    int sr, sg, sb;
    int stem_hue = fl_stem_hue[i];

    /* Green stem with slight brightness gradient (darker at bottom) */
    for (int y = 0; y < stem_h && y < H; y++) {
        float t = (float)y / (float)(stem_h > 1 ? stem_h : 1);
        int green_v = (int)((140.0f + 60.0f * t) * fade);
        hsv_to_rgb(stem_hue, 220, green_v, &sr, &sg, &sb);
        safe_pixel(x, y, sr, sg, sb, W, H);
    }

    /* Leaf at mid-height — bright and visible */
    int leaf_y = stem_h / 2;
    if (stem_h > 3 && leaf_y > 0 && leaf_y < H) {
        int leaf_x = fl_leaf_side[i] == 0 ? x - 1 : x + 1;
        int lr, lg, lb;
        int leaf_hue = stem_hue - 5; /* slightly different green */
        hsv_to_rgb(leaf_hue, 200, (int)(200.0f * fade), &lr, &lg, &lb);
        safe_pixel(leaf_x, leaf_y, lr, lg, lb, W, H);
        /* Second leaf pixel above for taller stems */
        if (stem_h > 6) {
            safe_pixel(leaf_x, leaf_y + 1, lr, lg, lb, W, H);
        }
    }
}

/* ---- Progressive bloom helper ---- */
/* Each pixel has a threshold — it appears when bloom_f passes it */
static void bloom_pixel(int x, int y, int hue, int sat, int max_val,
                         float bloom_f, float threshold, int W, int H) {
    float a = (bloom_f - threshold) / (1.0f - threshold);
    if (a <= 0.0f) return;
    if (a > 1.0f) a = 1.0f;
    /* Smooth ease-in */
    a = a * a * (3.0f - 2.0f * a);
    int val = (int)((float)max_val * a);
    if (val < 3) return;
    int r, g, b;
    hsv_to_rgb(hue, sat, val, &r, &g, &b);
    safe_pixel(x, y, r, g, b, W, H);
}

/* ---- Render tulip head ---- */
/* Bud tip → cup forms below → petals open wide at top */
static void render_tulip(int i, int W, int H, int size, float bf) {
    int x = fl_x[i], top = (int)fl_stem_y[i], hue = fl_hue[i];

    /* Bud tip */
    bloom_pixel(x, top, hue, 230, 210, bf, 0.0f, W, H);

    if (size >= 2) {
        /* Cup sides form below tip */
        bloom_pixel(x - 1, top - 1, hue, 230, 170, bf, 0.15f, W, H);
        bloom_pixel(x + 1, top - 1, hue, 230, 170, bf, 0.15f, W, H);
    }
    if (size >= 3) {
        /* Petals open at top level */
        bloom_pixel(x - 1, top, hue, 230, 200, bf, 0.35f, W, H);
        bloom_pixel(x + 1, top, hue, 230, 200, bf, 0.35f, W, H);
    }
    if (size >= 4) {
        bloom_pixel(x, top - 1, hue, 230, 180, bf, 0.10f, W, H);
        bloom_pixel(x - 2, top - 1, hue, 220, 140, bf, 0.40f, W, H);
        bloom_pixel(x + 2, top - 1, hue, 220, 140, bf, 0.40f, W, H);
        bloom_pixel(x - 2, top, hue, 220, 160, bf, 0.55f, W, H);
        bloom_pixel(x + 2, top, hue, 220, 160, bf, 0.55f, W, H);
    }
    if (size >= 5) {
        bloom_pixel(x - 1, top - 2, hue, 220, 140, bf, 0.12f, W, H);
        bloom_pixel(x, top - 2, hue, 230, 150, bf, 0.08f, W, H);
        bloom_pixel(x + 1, top - 2, hue, 220, 140, bf, 0.12f, W, H);
        bloom_pixel(x - 2, top - 2, hue, 210, 120, bf, 0.25f, W, H);
        bloom_pixel(x + 2, top - 2, hue, 210, 120, bf, 0.25f, W, H);
        bloom_pixel(x - 3, top, hue, 200, 120, bf, 0.70f, W, H);
        bloom_pixel(x + 3, top, hue, 200, 120, bf, 0.70f, W, H);
        bloom_pixel(x - 3, top - 1, hue, 200, 100, bf, 0.60f, W, H);
        bloom_pixel(x + 3, top - 1, hue, 200, 100, bf, 0.60f, W, H);
    }
}

/* ---- Render daisy head ---- */
/* Center bud → top petal → cross → diagonals → outer ring */
static void render_daisy(int i, int W, int H, int size, float bf) {
    int x = fl_x[i], top = (int)fl_stem_y[i];

    /* Yellow center (bud) */
    bloom_pixel(x, top, 40, 240, 240, bf, 0.0f, W, H);

    /* White petals open progressively: top first, then sides */
    if (size >= 1) {
        bloom_pixel(x, top + 1, 0, 0, 230, bf, 0.10f, W, H);
        bloom_pixel(x, top - 1, 230, 0, 230, bf, 0.20f, W, H);
        bloom_pixel(x - 1, top, 0, 0, 230, bf, 0.25f, W, H);
        bloom_pixel(x + 1, top, 0, 0, 230, bf, 0.25f, W, H);
    }
    if (size >= 2) {
        bloom_pixel(x - 1, top + 1, 0, 0, 210, bf, 0.35f, W, H);
        bloom_pixel(x + 1, top + 1, 0, 0, 210, bf, 0.35f, W, H);
        bloom_pixel(x - 1, top - 1, 0, 0, 210, bf, 0.40f, W, H);
        bloom_pixel(x + 1, top - 1, 0, 0, 210, bf, 0.40f, W, H);
    }
    if (size >= 3) {
        bloom_pixel(x, top + 2, 0, 0, 180, bf, 0.50f, W, H);
        bloom_pixel(x, top - 2, 0, 0, 180, bf, 0.55f, W, H);
        bloom_pixel(x - 2, top, 0, 0, 180, bf, 0.55f, W, H);
        bloom_pixel(x + 2, top, 0, 0, 180, bf, 0.55f, W, H);
    }
    if (size >= 4) {
        bloom_pixel(x - 2, top + 1, 0, 0, 160, bf, 0.62f, W, H);
        bloom_pixel(x + 2, top + 1, 0, 0, 160, bf, 0.62f, W, H);
        bloom_pixel(x - 2, top - 1, 0, 0, 160, bf, 0.65f, W, H);
        bloom_pixel(x + 2, top - 1, 0, 0, 160, bf, 0.65f, W, H);
        bloom_pixel(x - 1, top + 2, 0, 0, 160, bf, 0.62f, W, H);
        bloom_pixel(x + 1, top + 2, 0, 0, 160, bf, 0.62f, W, H);
        bloom_pixel(x - 1, top - 2, 0, 0, 160, bf, 0.65f, W, H);
        bloom_pixel(x + 1, top - 2, 0, 0, 160, bf, 0.65f, W, H);
    }
    if (size >= 5) {
        bloom_pixel(x, top + 3, 0, 0, 130, bf, 0.75f, W, H);
        bloom_pixel(x, top - 3, 0, 0, 130, bf, 0.78f, W, H);
        bloom_pixel(x - 3, top, 0, 0, 130, bf, 0.78f, W, H);
        bloom_pixel(x + 3, top, 0, 0, 130, bf, 0.78f, W, H);
        bloom_pixel(x - 2, top + 2, 0, 0, 130, bf, 0.75f, W, H);
        bloom_pixel(x + 2, top + 2, 0, 0, 130, bf, 0.75f, W, H);
        bloom_pixel(x - 2, top - 2, 0, 0, 130, bf, 0.78f, W, H);
        bloom_pixel(x + 2, top - 2, 0, 0, 130, bf, 0.78f, W, H);
    }
}

/* ---- Render rose head ---- */
/* Tight bud center → inner ring unfurls → outer rings open */
static void render_rose(int i, int W, int H, int size, float bf) {
    int x = fl_x[i], top = (int)fl_stem_y[i], hue = fl_hue[i];

    /* Bright center (bud) */
    bloom_pixel(x, top, hue, 240, 220, bf, 0.0f, W, H);

    if (size >= 2) {
        bloom_pixel(x + 1, top, hue, 240, 200, bf, 0.20f, W, H);
        bloom_pixel(x, top - 1, hue, 240, 200, bf, 0.25f, W, H);
        bloom_pixel(x + 1, top - 1, hue, 240, 190, bf, 0.30f, W, H);
    }
    if (size >= 3) {
        bloom_pixel(x - 1, top, hue, 240, 180, bf, 0.30f, W, H);
        bloom_pixel(x, top + 1, hue, 240, 180, bf, 0.35f, W, H);
        bloom_pixel(x - 1, top + 1, hue, 230, 150, bf, 0.42f, W, H);
        bloom_pixel(x + 1, top + 1, hue, 230, 150, bf, 0.42f, W, H);
        bloom_pixel(x - 1, top - 1, hue, 230, 150, bf, 0.38f, W, H);
    }
    if (size >= 4) {
        bloom_pixel(x - 2, top, hue, 220, 130, bf, 0.50f, W, H);
        bloom_pixel(x + 2, top, hue, 220, 130, bf, 0.50f, W, H);
        bloom_pixel(x, top + 2, hue, 220, 130, bf, 0.55f, W, H);
        bloom_pixel(x, top - 2, hue, 220, 130, bf, 0.55f, W, H);
        bloom_pixel(x - 2, top + 1, hue, 210, 110, bf, 0.58f, W, H);
        bloom_pixel(x + 2, top + 1, hue, 210, 110, bf, 0.58f, W, H);
        bloom_pixel(x - 2, top - 1, hue, 210, 110, bf, 0.58f, W, H);
        bloom_pixel(x + 2, top - 1, hue, 210, 110, bf, 0.58f, W, H);
        bloom_pixel(x - 1, top + 2, hue, 210, 110, bf, 0.60f, W, H);
        bloom_pixel(x + 1, top + 2, hue, 210, 110, bf, 0.60f, W, H);
        bloom_pixel(x - 1, top - 2, hue, 210, 110, bf, 0.60f, W, H);
        bloom_pixel(x + 1, top - 2, hue, 210, 110, bf, 0.60f, W, H);
    }
    if (size >= 5) {
        bloom_pixel(x - 3, top, hue, 200, 90, bf, 0.68f, W, H);
        bloom_pixel(x + 3, top, hue, 200, 90, bf, 0.68f, W, H);
        bloom_pixel(x, top + 3, hue, 200, 90, bf, 0.70f, W, H);
        bloom_pixel(x, top - 3, hue, 200, 90, bf, 0.70f, W, H);
        bloom_pixel(x - 3, top + 1, hue, 200, 80, bf, 0.72f, W, H);
        bloom_pixel(x + 3, top + 1, hue, 200, 80, bf, 0.72f, W, H);
        bloom_pixel(x - 3, top - 1, hue, 200, 80, bf, 0.72f, W, H);
        bloom_pixel(x + 3, top - 1, hue, 200, 80, bf, 0.72f, W, H);
        bloom_pixel(x - 1, top + 3, hue, 200, 80, bf, 0.74f, W, H);
        bloom_pixel(x + 1, top + 3, hue, 200, 80, bf, 0.74f, W, H);
        bloom_pixel(x - 1, top - 3, hue, 200, 80, bf, 0.74f, W, H);
        bloom_pixel(x + 1, top - 3, hue, 200, 80, bf, 0.74f, W, H);
        bloom_pixel(x - 2, top + 2, hue, 200, 80, bf, 0.72f, W, H);
        bloom_pixel(x + 2, top + 2, hue, 200, 80, bf, 0.72f, W, H);
        bloom_pixel(x - 2, top - 2, hue, 200, 80, bf, 0.72f, W, H);
        bloom_pixel(x + 2, top - 2, hue, 200, 80, bf, 0.72f, W, H);
    }
}

/* ---- Render flower head (dispatch by type) ---- */
static void render_head(int i, int W, int H, int size, float bloom_f) {
    switch (fl_type[i]) {
    case TYPE_TULIP: render_tulip(i, W, H, size, bloom_f); break;
    case TYPE_DAISY: render_daisy(i, W, H, size, bloom_f); break;
    case TYPE_ROSE:  render_rose(i, W, H, size, bloom_f);  break;
    }
}

/* ---- Init ---- */
EXPORT(init)
void init(void) {
    rng_state = 48271;
    prev_tick = 0;
    for (int i = 0; i < MAX_FLOWERS; i++) {
        fl_phase[i] = PHASE_INACTIVE;
        fl_timer[i] = (float)random_range(100, 1500);
    }
}

/* ---- Update ---- */
EXPORT(update)
void update(int tick_ms) {
    int flower_param = get_param_i32(0);  /* 0=Tulip, 1=Daisy, 2=Rose, 3=Mix */
    int count        = get_param_i32(1);  /* 1-10 */
    int size         = get_param_i32(2);  /* 1-3 */
    int speed_param  = get_param_i32(3);  /* 1-100 */

    int W = get_width();
    int H = get_height();
    if (W < 1) W = 1;
    if (H < 1) H = 1;
    if (count > MAX_FLOWERS) count = MAX_FLOWERS;
    if (count < 1) count = 1;
    if (size < 1) size = 1;
    if (size > 5) size = 5;

    rng_state ^= (uint32_t)tick_ms;

    /* Delta time */
    int32_t delta_ms = tick_ms - prev_tick;
    if (delta_ms <= 0 || delta_ms > 200) delta_ms = 33;
    prev_tick = tick_ms;
    float dt = (float)delta_ms / 1000.0f;

    /* Speed multiplier: param 40 = 1.0x, param 1 = 0.25x, param 100 = 2.5x */
    float speed_mult = (float)speed_param / 40.0f;

    /* Growth speed: pixels per second */
    float grow_speed = (float)H * 0.4f * speed_mult;
    /* Bloom speed: 0→1 per second */
    float bloom_speed = 0.5f * speed_mult;

    /* ---- Update flowers ---- */
    for (int i = 0; i < count; i++) {
        switch (fl_phase[i]) {

        case PHASE_INACTIVE:
            fl_timer[i] -= (float)delta_ms * speed_mult;
            if (fl_timer[i] <= 0.0f) {
                spawn_flower(i, W, H, flower_param);
            }
            break;

        case PHASE_GROWING:
            fl_stem_y[i] += grow_speed * dt;
            if (fl_stem_y[i] >= (float)fl_target_h[i]) {
                fl_stem_y[i] = (float)fl_target_h[i];
                fl_phase[i] = PHASE_BLOOMING;
                fl_bloom[i] = 0.0f;
            }
            break;

        case PHASE_BLOOMING:
            fl_bloom[i] += bloom_speed * dt;
            if (fl_bloom[i] >= 1.0f) {
                fl_bloom[i] = 1.0f;
                fl_phase[i] = PHASE_FULL;
                /* Stay in full bloom for 3-5 seconds (adjusted by speed) */
                fl_timer[i] = (float)random_range(3000, 5000) / speed_mult;
            }
            break;

        case PHASE_FULL:
            fl_timer[i] -= (float)delta_ms;
            if (fl_timer[i] <= 0.0f) {
                fl_phase[i] = PHASE_WILTING;
                fl_fade[i] = 1.0f;
            }
            break;

        case PHASE_WILTING:
            /* Whole flower fades out (stem + head together) */
            fl_fade[i] -= 0.3f * speed_mult * dt;
            if (fl_fade[i] <= 0.0f) {
                fl_fade[i] = 0.0f;
                fl_phase[i] = PHASE_INACTIVE;
                fl_timer[i] = (float)random_range(500, 2000);
            }
            break;
        }
    }

    /* Deactivate excess flowers if count was reduced */
    for (int i = count; i < MAX_FLOWERS; i++) {
        fl_phase[i] = PHASE_INACTIVE;
    }

    /* ---- Render ---- */
    /* Clear display */
    for (int x = 0; x < W; x++)
        for (int y = 0; y < H; y++)
            set_pixel(x, y, 0, 0, 0);

    /* Draw flowers */
    for (int i = 0; i < count; i++) {
        if (fl_phase[i] == PHASE_INACTIVE) continue;

        float fade = fl_fade[i];

        /* Draw stem */
        render_stem(i, W, H, fade);

        /* Draw flower head if blooming/full/wilting */
        if (fl_phase[i] >= PHASE_BLOOMING) {
            /* During wilting, bloom stays at 1.0 but fade dims everything */
            float bf = fl_bloom[i] * fade;
            if (bf > 0.01f) {
                render_head(i, W, H, size, bf);
            }
        }
    }

    draw();
}
