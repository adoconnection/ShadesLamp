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
         "\"min\":1,\"max\":3,\"default\":2,"
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
}

/* ---- Set pixel with bounds check ---- */
static void safe_pixel(int x, int y, int r, int g, int b, int W, int H) {
    if (x >= 0 && x < W && y >= 0 && y < H)
        set_pixel(x, y, r, g, b);
}

/* ---- Render a stem ---- */
static void render_stem(int i, int W, int H, float wilt_factor) {
    int x = fl_x[i];
    int stem_h = (int)fl_stem_y[i];
    int sr, sg, sb;

    /* Green stem */
    int green_v = (int)(180.0f * wilt_factor);
    hsv_to_rgb(85, 220, green_v, &sr, &sg, &sb);

    for (int y = 0; y < stem_h && y < H; y++) {
        safe_pixel(x, y, sr, sg, sb, W, H);
    }

    /* Leaf at mid-height */
    int leaf_y = stem_h / 2;
    if (stem_h > 3 && leaf_y > 0 && leaf_y < H) {
        int leaf_x = fl_leaf_side[i] == 0 ? x - 1 : x + 1;
        int lr, lg, lb;
        hsv_to_rgb(80, 200, (int)(140.0f * wilt_factor), &lr, &lg, &lb);
        safe_pixel(leaf_x, leaf_y, lr, lg, lb, W, H);
    }
}

/* ---- Render tulip head ---- */
static void render_tulip(int i, int W, int H, int size, float bloom_f) {
    int x = fl_x[i];
    int top = (int)fl_stem_y[i];
    int r, g, b;
    int val = (int)(200.0f * bloom_f);
    if (val < 10) val = 10;
    hsv_to_rgb(fl_hue[i], 230, val, &r, &g, &b);

    if (size == 1) {
        /* 1 pixel on top */
        safe_pixel(x, top, r, g, b, W, H);
    } else if (size == 2) {
        /* Cup shape: center top + 2 sides one below */
        safe_pixel(x, top, r, g, b, W, H);
        int darker = (int)(150.0f * bloom_f);
        if (darker < 10) darker = 10;
        int r2, g2, b2;
        hsv_to_rgb(fl_hue[i], 230, darker, &r2, &g2, &b2);
        safe_pixel(x - 1, top - 1, r2, g2, b2, W, H);
        safe_pixel(x + 1, top - 1, r2, g2, b2, W, H);
    } else {
        /* size 3: 3 on top + 2 sides below */
        safe_pixel(x, top, r, g, b, W, H);
        safe_pixel(x - 1, top, r, g, b, W, H);
        safe_pixel(x + 1, top, r, g, b, W, H);
        int darker = (int)(140.0f * bloom_f);
        if (darker < 10) darker = 10;
        int r2, g2, b2;
        hsv_to_rgb(fl_hue[i], 230, darker, &r2, &g2, &b2);
        safe_pixel(x - 1, top - 1, r2, g2, b2, W, H);
        safe_pixel(x + 1, top - 1, r2, g2, b2, W, H);
    }
}

/* ---- Render daisy head ---- */
static void render_daisy(int i, int W, int H, int size, float bloom_f) {
    int x = fl_x[i];
    int top = (int)fl_stem_y[i];
    int val_white = (int)(220.0f * bloom_f);
    if (val_white < 10) val_white = 10;
    int val_yellow = (int)(230.0f * bloom_f);
    if (val_yellow < 10) val_yellow = 10;

    /* Yellow center */
    int cr, cg, cb;
    hsv_to_rgb(40, 240, val_yellow, &cr, &cg, &cb);
    safe_pixel(x, top, cr, cg, cb, W, H);

    /* White petals */
    int pr, pg, pb;
    hsv_to_rgb(0, 0, val_white, &pr, &pg, &pb);

    if (size >= 1) {
        /* Cross: 4 cardinal petals */
        safe_pixel(x, top + 1, pr, pg, pb, W, H);
        safe_pixel(x, top - 1, pr, pg, pb, W, H);
        safe_pixel(x - 1, top, pr, pg, pb, W, H);
        safe_pixel(x + 1, top, pr, pg, pb, W, H);
    }
    if (size >= 2) {
        /* Diagonals */
        safe_pixel(x - 1, top + 1, pr, pg, pb, W, H);
        safe_pixel(x + 1, top + 1, pr, pg, pb, W, H);
        safe_pixel(x - 1, top - 1, pr, pg, pb, W, H);
        safe_pixel(x + 1, top - 1, pr, pg, pb, W, H);
    }
    if (size >= 3) {
        /* Second ring of petals */
        int dimmer = (int)(160.0f * bloom_f);
        if (dimmer < 10) dimmer = 10;
        int dr, dg, db;
        hsv_to_rgb(0, 0, dimmer, &dr, &dg, &db);
        safe_pixel(x, top + 2, dr, dg, db, W, H);
        safe_pixel(x, top - 2, dr, dg, db, W, H);
        safe_pixel(x - 2, top, dr, dg, db, W, H);
        safe_pixel(x + 2, top, dr, dg, db, W, H);
    }
}

/* ---- Render rose head ---- */
static void render_rose(int i, int W, int H, int size, float bloom_f) {
    int x = fl_x[i];
    int top = (int)fl_stem_y[i];
    int val = (int)(210.0f * bloom_f);
    if (val < 10) val = 10;
    int r, g, b;
    hsv_to_rgb(fl_hue[i], 240, val, &r, &g, &b);

    if (size == 1) {
        safe_pixel(x, top, r, g, b, W, H);
    } else if (size == 2) {
        /* 2x2 block */
        safe_pixel(x, top, r, g, b, W, H);
        safe_pixel(x + 1, top, r, g, b, W, H);
        safe_pixel(x, top - 1, r, g, b, W, H);
        safe_pixel(x + 1, top - 1, r, g, b, W, H);
    } else {
        /* 3x3 with gradient: bright center, dimmer edges */
        safe_pixel(x, top, r, g, b, W, H);
        /* Middle ring */
        int mid_v = (int)(170.0f * bloom_f);
        if (mid_v < 10) mid_v = 10;
        int mr, mg, mb;
        hsv_to_rgb(fl_hue[i], 240, mid_v, &mr, &mg, &mb);
        safe_pixel(x - 1, top, mr, mg, mb, W, H);
        safe_pixel(x + 1, top, mr, mg, mb, W, H);
        safe_pixel(x, top + 1, mr, mg, mb, W, H);
        safe_pixel(x, top - 1, mr, mg, mb, W, H);
        /* Corner ring */
        int edge_v = (int)(120.0f * bloom_f);
        if (edge_v < 10) edge_v = 10;
        int er, eg, eb;
        hsv_to_rgb(fl_hue[i], 220, edge_v, &er, &eg, &eb);
        safe_pixel(x - 1, top + 1, er, eg, eb, W, H);
        safe_pixel(x + 1, top + 1, er, eg, eb, W, H);
        safe_pixel(x - 1, top - 1, er, eg, eb, W, H);
        safe_pixel(x + 1, top - 1, er, eg, eb, W, H);
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
    if (size > 3) size = 3;

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
            }
            break;

        case PHASE_WILTING:
            fl_bloom[i] -= bloom_speed * 0.7f * dt;
            if (fl_bloom[i] <= 0.0f) {
                fl_bloom[i] = 0.0f;
                /* Shrink stem */
                fl_stem_y[i] -= grow_speed * 0.8f * dt;
                if (fl_stem_y[i] <= 0.0f) {
                    fl_stem_y[i] = 0.0f;
                    fl_phase[i] = PHASE_INACTIVE;
                    fl_timer[i] = (float)random_range(500, 2000);
                }
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

        /* Wilt factor for stem color (1.0 = alive, fades during wilt) */
        float wilt = 1.0f;
        if (fl_phase[i] == PHASE_WILTING) {
            if (fl_bloom[i] > 0.0f) {
                wilt = 0.5f + 0.5f * fl_bloom[i];
            } else {
                /* Stem shrinking phase */
                float frac = fl_stem_y[i] / (float)fl_target_h[i];
                wilt = 0.3f + 0.5f * frac;
            }
        }

        /* Draw stem */
        render_stem(i, W, H, wilt);

        /* Draw flower head if blooming/full/wilting */
        if (fl_phase[i] >= PHASE_BLOOMING) {
            float bf = fl_bloom[i];
            if (bf > 0.01f) {
                render_head(i, W, H, size, bf);
            }
        }
    }

    draw();
}
