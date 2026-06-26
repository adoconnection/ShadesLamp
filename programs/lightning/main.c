#include "api.h"

/* ---- Metadata JSON ---- */
static const char META[] =
    "{\"name\":\"Lightning\","
    "\"desc\":\"Huge electric bolts striking between top and bottom\","
    "\"params\":["
        "{\"id\":0,\"name\":\"Rate\",\"type\":\"int\","
         "\"min\":1,\"max\":100,\"default\":45,"
         "\"desc\":\"How often bolts strike\"},"
        "{\"id\":1,\"name\":\"Jaggedness\",\"type\":\"int\","
         "\"min\":0,\"max\":100,\"default\":55,"
         "\"desc\":\"How jagged the bolts are\"},"
        "{\"id\":2,\"name\":\"Thickness\",\"type\":\"int\","
         "\"min\":1,\"max\":3,\"default\":1,"
         "\"desc\":\"Bolt glow width\"},"
        "{\"id\":3,\"name\":\"Branches\",\"type\":\"int\","
         "\"min\":0,\"max\":100,\"default\":40,"
         "\"desc\":\"Side branch amount\"},"
        "{\"id\":4,\"name\":\"Brightness\",\"type\":\"int\","
         "\"min\":1,\"max\":255,\"default\":235,"
         "\"desc\":\"Overall brightness\"},"
        "{\"id\":5,\"name\":\"Hue\",\"type\":\"int\","
         "\"min\":0,\"max\":255,\"default\":150,"
         "\"desc\":\"Bolt color (150=electric blue)\"}"
    "]}";

EXPORT(get_meta_ptr)
int get_meta_ptr(void) { return (int)META; }

EXPORT(get_meta_len)
int get_meta_len(void) { return sizeof(META) - 1; }

/* ---- PRNG ---- */
static uint32_t rng = 2166136261u;
static uint32_t rng_next(void) {
    uint32_t x = rng; x ^= x << 13; x ^= x >> 17; x ^= x << 5; rng = x; return x;
}
static float rnd(void) { return (float)(rng_next() & 0xFFFF) / 65536.0f; }
static int rrange(int lo, int hi) { if (lo >= hi) return lo; return lo + (int)(rng_next() % (uint32_t)(hi - lo)); }
/* small deterministic LCG for stable per-bolt paths */
static uint32_t lcg(uint32_t* s) { *s = *s * 1664525u + 1013904223u; return *s; }

/* ---- HSV to RGB ---- */
static void hsv2rgb(int h, int s, int v, int *r, int *g, int *b) {
    if (v < 0) v = 0; if (v > 255) v = 255;
    if (s == 0) { *r = *g = *b = v; return; }
    h &= 0xFF;
    int region = h / 43, rem = (h - region * 43) * 6;
    int p = (v * (255 - s)) >> 8;
    int q = (v * (255 - ((s * rem) >> 8))) >> 8;
    int t = (v * (255 - ((s * (255 - rem)) >> 8))) >> 8;
    switch (region) {
        case 0: *r=v; *g=t; *b=p; break;
        case 1: *r=q; *g=v; *b=p; break;
        case 2: *r=p; *g=v; *b=t; break;
        case 3: *r=p; *g=q; *b=v; break;
        case 4: *r=t; *g=p; *b=v; break;
        default:*r=v; *g=p; *b=q; break;
    }
}

/* ---- Framebuffer ---- */
#define MAX_W 64
#define MAX_H 64
static uint8_t fb_r[MAX_W*MAX_H], fb_g[MAX_W*MAX_H], fb_b[MAX_W*MAX_H];
static int curW = 16, curH = 32;

static void fb_add(int x, int y, int r, int g, int b) {
    if (x < 0 || y < 0 || x >= curW || y >= curH) return;
    int i = y * curW + x;
    int nr = fb_r[i] + r, ng = fb_g[i] + g, nb = fb_b[i] + b;
    fb_r[i] = nr > 255 ? 255 : nr;
    fb_g[i] = ng > 255 ? 255 : ng;
    fb_b[i] = nb > 255 ? 255 : nb;
}

/* ---- Bolts ---- */
#define MAX_BOLTS 8
static int      b_active[MAX_BOLTS];
static int      b_x0[MAX_BOLTS];     /* start column */
static uint32_t b_seed[MAX_BOLTS];   /* fixed -> stable jagged shape */
static int      b_age[MAX_BOLTS];
static int      b_life[MAX_BOLTS];

static float flash = 0.0f;           /* ambient flash level */
static int32_t prev_tick;

EXPORT(init)
void init(void) {
    curW = get_width();  if (curW < 1) curW = 1; if (curW > MAX_W) curW = MAX_W;
    curH = get_height(); if (curH < 1) curH = 1; if (curH > MAX_H) curH = MAX_H;
    for (int i = 0; i < MAX_W*MAX_H; i++) { fb_r[i]=fb_g[i]=fb_b[i]=0; }
    for (int i = 0; i < MAX_BOLTS; i++) b_active[i] = 0;
    flash = 0.0f;
    prev_tick = 0;
}

/* draw a glowing point: white-ish core + saturated side glow */
static void glow(int x, int y, int val, int hue, int thick) {
    if (val <= 0) return;
    int r,g,b;
    hsv2rgb(hue, 60, val, &r, &g, &b);           /* bright core (low sat = white-blue) */
    fb_add(x, y, r, g, b);
    for (int d = 1; d <= thick; d++) {
        int gv = val * (thick - d + 1) / (thick + 2);
        if (gv <= 0) continue;
        hsv2rgb(hue, 255, gv, &r, &g, &b);       /* saturated glow */
        fb_add(x - d, y, r, g, b);
        fb_add(x + d, y, r, g, b);
    }
}

static void draw_branch(int x, int y, uint32_t seed, int val, int hue, int thick) {
    uint32_t s = seed | 1u;
    int len = 3 + (int)(s % 4);
    int dir = (s & 2) ? 1 : -1;
    int bx = x;
    for (int k = 0; k < len; k++) {
        int by = y + k;
        if (by >= curH) break;
        bx += dir * (1 + (int)(lcg(&s) % 2));
        if (bx < 0) bx = 0; if (bx >= curW) bx = curW - 1;
        glow(bx, by, val * (len - k) / (len + 1), hue, thick > 1 ? thick - 1 : 1);
    }
}

EXPORT(update)
void update(int tick_ms) {
    int rate   = get_param_i32(0);
    int jag    = get_param_i32(1);
    int thick  = get_param_i32(2);
    int branch = get_param_i32(3);
    int bright = get_param_i32(4);
    int hue    = get_param_i32(5);

    curW = get_width();  if (curW < 1) curW = 1; if (curW > MAX_W) curW = MAX_W;
    curH = get_height(); if (curH < 1) curH = 1; if (curH > MAX_H) curH = MAX_H;
    if (thick < 1) thick = 1; if (thick > 3) thick = 3;

    rng ^= (uint32_t)tick_ms;

    int32_t delta = tick_ms - prev_tick;
    if (delta <= 0 || delta > 200) delta = 33;
    prev_tick = tick_ms;

    /* ---- fade previous frame (afterglow) ---- */
    for (int i = 0; i < curW * curH; i++) {
        fb_r[i] = (uint8_t)(fb_r[i] * 120 / 256);
        fb_g[i] = (uint8_t)(fb_g[i] * 120 / 256);
        fb_b[i] = (uint8_t)(fb_b[i] * 130 / 256);
    }

    /* ---- maybe spawn a new bolt ---- */
    if (rnd() < (float)rate * 0.006f) {
        for (int i = 0; i < MAX_BOLTS; i++) {
            if (!b_active[i]) {
                b_active[i] = 1;
                b_x0[i] = rrange(0, curW);
                b_seed[i] = rng_next() | 1u;
                b_age[i] = 0;
                b_life[i] = rrange(4, 11);
                flash += 0.5f; if (flash > 1.0f) flash = 1.0f;
                break;
            }
        }
    }

    /* ---- ambient electric flash over the whole screen ---- */
    flash *= 0.80f;
    if (flash > 0.02f) {
        int fr, fg, fb;
        hsv2rgb(hue, 255, (int)(flash * (float)bright * 0.12f), &fr, &fg, &fb);
        for (int y = 0; y < curH; y++)
            for (int x = 0; x < curW; x++)
                fb_add(x, y, fr, fg, fb);
    }

    /* ---- draw active bolts ---- */
    int range = 1 + jag * 3 / 100;   /* horizontal step magnitude */
    for (int i = 0; i < MAX_BOLTS; i++) {
        if (!b_active[i]) continue;

        /* envelope: quick flash up, then fade out over life */
        float t = (float)b_age[i] / (float)b_life[i];
        float env = (t < 0.15f) ? (t / 0.15f) : (1.0f - (t - 0.15f) / 0.85f);
        if (env < 0.0f) env = 0.0f;
        float flick = 0.55f + 0.45f * rnd();      /* per-frame crackle */
        int val = (int)((float)bright * env * flick);

        uint32_t s = b_seed[i];                    /* reset -> stable shape each frame */
        int px = b_x0[i], prev = px;
        for (int y = 0; y < curH; y++) {
            int a = prev < px ? prev : px;
            int b = prev < px ? px : prev;
            for (int c = a; c <= b; c++) glow(c, y, val, hue, thick);
            prev = px;
            /* advance jagged path */
            uint32_t rr = lcg(&s);
            px += (int)(rr % (uint32_t)(2 * range + 1)) - range;
            if (px < 0) px = 0; if (px >= curW) px = curW - 1;
            /* maybe fork a branch */
            if (branch > 0) {
                uint32_t br = lcg(&s);
                if ((int)(br % 100) < branch / 3) {
                    draw_branch(px, y, br, val / 2, hue, thick);
                }
            }
        }

        b_age[i]++;
        if (b_age[i] > b_life[i]) b_active[i] = 0;
    }

    /* ---- present ---- */
    for (int y = 0; y < curH; y++)
        for (int x = 0; x < curW; x++) {
            int i = y * curW + x;
            set_pixel(x, y, fb_r[i], fb_g[i], fb_b[i]);
        }
    draw();
}
