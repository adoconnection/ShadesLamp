#include "api.h"

/*
 * Flowers — Flowers sprout from the bottom, stems grow upward,
 * buds bloom and eventually wilt. Y=0 is the bottom.
 * Types: Tulip, Daisy, Rose, Mix.
 *
 * Rendering uses the framebuffer fast-path: pixels are written straight
 * into FB and the host copies it once per draw(). The only per-frame host
 * calls are m_fill (clear) and draw(). Colors are derived in integer math
 * from a per-flower "pure hue" (m_hsv at s=255,v=255, cached at spawn).
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
         "\"desc\":\"Growth and bloom speed\"},"
        "{\"id\":4,\"name\":\"Sky hue\",\"type\":\"int\","
         "\"min\":0,\"max\":255,\"default\":150,"
         "\"desc\":\"Background sky colour (hue)\"},"
        "{\"id\":5,\"name\":\"Sky light\",\"type\":\"int\","
         "\"min\":0,\"max\":100,\"default\":0,"
         "\"desc\":\"Background sky intensity (0 = off)\"}"
    "]}";

EXPORT(get_meta_ptr)
int get_meta_ptr(void) { return (int)META; }

EXPORT(get_meta_len)
int get_meta_len(void) { return sizeof(META) - 1; }

/* ---- Framebuffer (row-major (y*W+x)*3, RGB) ---- */
#define MAX_W 64
#define MAX_H 64
static uint8_t FB[MAX_W * MAX_H * 3];

EXPORT(get_framebuffer)
int get_framebuffer(void) { return (int)FB; }

/* Current frame dimensions (set at the top of update) */
static int FBW = 1, FBH = 1;

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
static float fl_timer[MAX_FLOWERS];
static int   fl_leaf_side[MAX_FLOWERS];
static float fl_fade[MAX_FLOWERS];      /* fade-out factor for wilting (1.0→0.0) */
static float fl_speed_k[MAX_FLOWERS];   /* per-flower life-speed factor 0.75-1.25 */
/* Pure hue colors (m_hsv at s=255,v=255), cached at spawn */
static int   fl_pure_head[MAX_FLOWERS];
static int   fl_pure_stem[MAX_FLOWERS];
static int   fl_pure_leaf[MAX_FLOWERS];

/* ---- Timing ---- */
static int32_t prev_tick;

/* Global gate between spawns: keeps flowers desynchronized so a new one
   sprouts every SPAWN_GAP_MS instead of a synchronized burst. */
#define SPAWN_GAP_MS 800.0f
static float spawn_gate;

/* ---- Shaded pixel write ----
 * channel(h,s,v) = v * (65025 - s*(255 - pure_ch)) / 65025 — the standard
 * HSV formula re-expressed через кэшированный pure-цвет, so no m_hsv calls
 * are needed per pixel. s==0 degenerates to white (r=g=b=v). */
static void px(int x, int y, int pure, int s, int v) {
    if (x < 0 || x >= FBW || y < 0 || y >= FBH) return;
    uint8_t *p = &FB[(y * FBW + x) * 3];
    int r = v * (65025 - s * (255 - ((pure >> 16) & 255))) / 65025;
    int g = v * (65025 - s * (255 - ((pure >> 8) & 255))) / 65025;
    int b = v * (65025 - s * (255 - (pure & 255))) / 65025;
    /* Screen-blend over what's already there (the sky): the dimmer the
       flower pixel, the more background shows through — wilting flowers
       dissolve into the sky instead of leaving dark silhouettes.
       On a black background this is an exact overwrite. */
    int fmax = r > g ? (r > b ? r : b) : (g > b ? g : b);
    int keep = 255 - fmax;
    p[0] = (uint8_t)(r + p[0] * keep / 255);
    p[1] = (uint8_t)(g + p[1] * keep / 255);
    p[2] = (uint8_t)(b + p[2] * keep / 255);
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
        return 40; /* yellow center; petals are white (sat 0) */
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

    fl_leaf_side[i] = random_range(0, 2);
    fl_fade[i] = 1.0f;
    fl_speed_k[i] = (float)random_range(75, 126) / 100.0f;

    /* Cache pure colors — the only m_hsv calls in the whole program */
    int stem_hue = 75 + random_range(0, 20);  /* green 75-95 hue variation */
    fl_pure_head[i] = m_hsv(pick_hue(fl_type[i]), 255, 255);
    fl_pure_stem[i] = m_hsv(stem_hue, 255, 255);
    fl_pure_leaf[i] = m_hsv(stem_hue - 5, 255, 255);
}

/* ---- Render a stem ---- */
static void render_stem(int i, float fade) {
    int x = fl_x[i];
    int stem_h = (int)fl_stem_y[i];
    int pure = fl_pure_stem[i];

    /* Green stem with slight brightness gradient (darker at bottom) */
    for (int y = 0; y < stem_h && y < FBH; y++) {
        float t = (float)y / (float)(stem_h > 1 ? stem_h : 1);
        int green_v = (int)((140.0f + 60.0f * t) * fade);
        px(x, y, pure, 220, green_v);
    }

    /* Leaf at mid-height — bright and visible */
    int leaf_y = stem_h / 2;
    if (stem_h > 3 && leaf_y > 0 && leaf_y < FBH) {
        int leaf_x = fl_leaf_side[i] == 0 ? x - 1 : x + 1;
        int leaf_v = (int)(200.0f * fade);
        px(leaf_x, leaf_y, fl_pure_leaf[i], 200, leaf_v);
        /* Second leaf pixel above for taller stems */
        if (stem_h > 6) {
            px(leaf_x, leaf_y + 1, fl_pure_leaf[i], 200, leaf_v);
        }
    }
}

/* ---- Flower head shapes ----
 * Each head is a table of pixels around the stem top. A pixel appears when
 * bloom_f passes its threshold (thr, percent) and only if size >= minsize.
 * sat==0 means white (hue-independent), used for daisy petals. */
typedef signed char int8_t;

typedef struct {
    int8_t  dx, dy;    /* offset from stem top (dy>0 is up) */
    uint8_t sat, val;  /* HSV saturation & peak brightness */
    uint8_t thr;       /* bloom threshold, percent 0-100 */
    uint8_t minsize;   /* drawn when size >= minsize */
} HeadPx;

/* Tulip: bud tip → cup forms below → petals open wide at top */
static const HeadPx TULIP_PX[] = {
    { 0,  0, 230, 210,  0, 1},
    {-1, -1, 230, 170, 15, 2}, { 1, -1, 230, 170, 15, 2},
    {-1,  0, 230, 200, 35, 3}, { 1,  0, 230, 200, 35, 3},
    { 0, -1, 230, 180, 10, 4},
    {-2, -1, 220, 140, 40, 4}, { 2, -1, 220, 140, 40, 4},
    {-2,  0, 220, 160, 55, 4}, { 2,  0, 220, 160, 55, 4},
    {-1, -2, 220, 140, 12, 5}, { 0, -2, 230, 150,  8, 5}, { 1, -2, 220, 140, 12, 5},
    {-2, -2, 210, 120, 25, 5}, { 2, -2, 210, 120, 25, 5},
    {-3,  0, 200, 120, 70, 5}, { 3,  0, 200, 120, 70, 5},
    {-3, -1, 200, 100, 60, 5}, { 3, -1, 200, 100, 60, 5},
};

/* Daisy: yellow center bud → white petals: top first, then sides, then ring */
static const HeadPx DAISY_PX[] = {
    { 0,  0, 240, 240,  0, 1},
    { 0,  1,   0, 230, 10, 1}, { 0, -1,   0, 230, 20, 1},
    {-1,  0,   0, 230, 25, 1}, { 1,  0,   0, 230, 25, 1},
    {-1,  1,   0, 210, 35, 2}, { 1,  1,   0, 210, 35, 2},
    {-1, -1,   0, 210, 40, 2}, { 1, -1,   0, 210, 40, 2},
    { 0,  2,   0, 180, 50, 3}, { 0, -2,   0, 180, 55, 3},
    {-2,  0,   0, 180, 55, 3}, { 2,  0,   0, 180, 55, 3},
    {-2,  1,   0, 160, 62, 4}, { 2,  1,   0, 160, 62, 4},
    {-2, -1,   0, 160, 65, 4}, { 2, -1,   0, 160, 65, 4},
    {-1,  2,   0, 160, 62, 4}, { 1,  2,   0, 160, 62, 4},
    {-1, -2,   0, 160, 65, 4}, { 1, -2,   0, 160, 65, 4},
    { 0,  3,   0, 130, 75, 5}, { 0, -3,   0, 130, 78, 5},
    {-3,  0,   0, 130, 78, 5}, { 3,  0,   0, 130, 78, 5},
    {-2,  2,   0, 130, 75, 5}, { 2,  2,   0, 130, 75, 5},
    {-2, -2,   0, 130, 78, 5}, { 2, -2,   0, 130, 78, 5},
};

/* Rose: tight bud center → inner ring unfurls → outer rings open */
static const HeadPx ROSE_PX[] = {
    { 0,  0, 240, 220,  0, 1},
    { 1,  0, 240, 200, 20, 2}, { 0, -1, 240, 200, 25, 2}, { 1, -1, 240, 190, 30, 2},
    {-1,  0, 240, 180, 30, 3}, { 0,  1, 240, 180, 35, 3},
    {-1,  1, 230, 150, 42, 3}, { 1,  1, 230, 150, 42, 3}, {-1, -1, 230, 150, 38, 3},
    {-2,  0, 220, 130, 50, 4}, { 2,  0, 220, 130, 50, 4},
    { 0,  2, 220, 130, 55, 4}, { 0, -2, 220, 130, 55, 4},
    {-2,  1, 210, 110, 58, 4}, { 2,  1, 210, 110, 58, 4},
    {-2, -1, 210, 110, 58, 4}, { 2, -1, 210, 110, 58, 4},
    {-1,  2, 210, 110, 60, 4}, { 1,  2, 210, 110, 60, 4},
    {-1, -2, 210, 110, 60, 4}, { 1, -2, 210, 110, 60, 4},
    {-3,  0, 200,  90, 68, 5}, { 3,  0, 200,  90, 68, 5},
    { 0,  3, 200,  90, 70, 5}, { 0, -3, 200,  90, 70, 5},
    {-3,  1, 200,  80, 72, 5}, { 3,  1, 200,  80, 72, 5},
    {-3, -1, 200,  80, 72, 5}, { 3, -1, 200,  80, 72, 5},
    {-1,  3, 200,  80, 74, 5}, { 1,  3, 200,  80, 74, 5},
    {-1, -3, 200,  80, 74, 5}, { 1, -3, 200,  80, 74, 5},
    {-2,  2, 200,  80, 72, 5}, { 2,  2, 200,  80, 72, 5},
    {-2, -2, 200,  80, 72, 5}, { 2, -2, 200,  80, 72, 5},
};

static const HeadPx *HEAD_TAB[3] = { TULIP_PX, DAISY_PX, ROSE_PX };
static const int HEAD_N[3] = {
    (int)(sizeof(TULIP_PX) / sizeof(TULIP_PX[0])),
    (int)(sizeof(DAISY_PX) / sizeof(DAISY_PX[0])),
    (int)(sizeof(ROSE_PX)  / sizeof(ROSE_PX[0])),
};

/* ---- Progressive bloom pixel ---- */
static void bloom_px(int x, int y, int pure, int sat, int max_val,
                     float bloom_f, float threshold) {
    float a = (bloom_f - threshold) / (1.0f - threshold);
    if (a <= 0.0f) return;
    if (a > 1.0f) a = 1.0f;
    /* Smooth ease-in */
    a = a * a * (3.0f - 2.0f * a);
    int val = (int)((float)max_val * a);
    if (val < 3) return;
    px(x, y, pure, sat, val);
}

/* ---- Render flower head ---- */
static void render_head(int i, int size, float bloom_f) {
    const HeadPx *tab = HEAD_TAB[fl_type[i]];
    int n = HEAD_N[fl_type[i]];
    int x = fl_x[i], top = (int)fl_stem_y[i], pure = fl_pure_head[i];

    for (int k = 0; k < n; k++) {
        const HeadPx *e = &tab[k];
        if (size < e->minsize) continue;
        bloom_px(x + e->dx, top + e->dy, pure, e->sat, e->val,
                 bloom_f, (float)e->thr * 0.01f);
    }
}

/* ---- Init ---- */
EXPORT(init)
void init(void) {
    rng_state = 48271;
    prev_tick = 0;
    spawn_gate = 0.0f;
    for (int i = 0; i < MAX_FLOWERS; i++) {
        fl_phase[i] = PHASE_INACTIVE;
        /* Stagger initial sprouts across several seconds so the garden
           starts as a sequence, not a synchronized wave */
        fl_timer[i] = (float)(100 + i * 700 + random_range(0, 500));
    }
}

/* ---- Update ---- */
EXPORT(update)
void update(int tick_ms) {
    int flower_param = get_param_i32(0);  /* 0=Tulip, 1=Daisy, 2=Rose, 3=Mix */
    int count        = get_param_i32(1);  /* 1-10 */
    int size         = get_param_i32(2);  /* 1-5 */
    int speed_param  = get_param_i32(3);  /* 1-100 */
    int sky_hue      = get_param_i32(4);  /* 0-255 */
    int sky_light    = get_param_i32(5);  /* 0-100, 0 = off */

    int W = get_width();
    int H = get_height();
    if (W < 1) W = 1;
    if (H < 1) H = 1;
    if (W > MAX_W) W = MAX_W;
    if (H > MAX_H) H = MAX_H;
    FBW = W;
    FBH = H;
    if (count > MAX_FLOWERS) count = MAX_FLOWERS;
    if (count < 1) count = 1;
    if (size < 1) size = 1;
    if (size > 5) size = 5;
    sky_hue &= 255;
    if (sky_light < 0) sky_light = 0;
    if (sky_light > 100) sky_light = 100;

    rng_state ^= (uint32_t)tick_ms;
    if (rng_state == 0) rng_state = 48271;  /* xorshift32 must never be 0 */

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

    spawn_gate -= (float)delta_ms * speed_mult;

    /* ---- Update flowers ---- */
    for (int i = 0; i < count; i++) {
        switch (fl_phase[i]) {

        case PHASE_INACTIVE:
            fl_timer[i] -= (float)delta_ms * speed_mult;
            /* Spawn only when the global gate is open, so new flowers
               appear one by one and something is always in motion */
            if (fl_timer[i] <= 0.0f && spawn_gate <= 0.0f) {
                spawn_flower(i, W, H, flower_param);
                spawn_gate = SPAWN_GAP_MS;
            }
            break;

        case PHASE_GROWING:
            fl_stem_y[i] += grow_speed * fl_speed_k[i] * dt;
            if (fl_stem_y[i] >= (float)fl_target_h[i]) {
                fl_stem_y[i] = (float)fl_target_h[i];
                fl_phase[i] = PHASE_BLOOMING;
                fl_bloom[i] = 0.0f;
            }
            break;

        case PHASE_BLOOMING:
            fl_bloom[i] += bloom_speed * fl_speed_k[i] * dt;
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
            fl_fade[i] -= 0.3f * speed_mult * fl_speed_k[i] * dt;
            if (fl_fade[i] <= 0.0f) {
                fl_fade[i] = 0.0f;
                fl_phase[i] = PHASE_INACTIVE;
                fl_timer[i] = (float)random_range(500, 2500);
            }
            break;
        }
    }

    /* Deactivate excess flowers if count was reduced */
    for (int i = count; i < MAX_FLOWERS; i++) {
        fl_phase[i] = PHASE_INACTIVE;
    }

    /* ---- Render ---- */
    if (sky_light > 0) {
        /* Sky: vertical gradient, a touch brighter towards the top
           (same style as Snow). One m_hsv call, rows shaded in-wasm. */
        int pure = m_hsv(sky_hue, 255, 255);
        int val_top = sky_light * 140 / 100;               /* 0..140 */
        float inv_h = (H > 1) ? 1.0f / (float)(H - 1) : 0.0f;
        for (int y = 0; y < H; y++) {
            float k = 0.65f + 0.35f * (float)y * inv_h;    /* y=0 is bottom */
            int v = (int)((float)val_top * k + 0.5f);
            uint8_t r = (uint8_t)(v * (65025 - 205 * (255 - ((pure >> 16) & 255))) / 65025);
            uint8_t g = (uint8_t)(v * (65025 - 205 * (255 - ((pure >> 8) & 255))) / 65025);
            uint8_t b = (uint8_t)(v * (65025 - 205 * (255 - (pure & 255))) / 65025);
            uint8_t *row = &FB[y * W * 3];
            for (int x = 0; x < W; x++) {
                row[x * 3]     = r;
                row[x * 3 + 1] = g;
                row[x * 3 + 2] = b;
            }
        }
    } else {
        m_fill(FB, W * H, 0x000000);
    }

    for (int i = 0; i < count; i++) {
        if (fl_phase[i] == PHASE_INACTIVE) continue;

        float fade = fl_fade[i];

        render_stem(i, fade);

        /* Draw flower head if blooming/full/wilting */
        if (fl_phase[i] >= PHASE_BLOOMING) {
            /* During wilting, bloom stays at 1.0 but fade dims everything */
            float bf = fl_bloom[i] * fade;
            if (bf > 0.01f) {
                render_head(i, size, bf);
            }
        }
    }

    draw();
}
