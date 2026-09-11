#include "api.h"

/*
 * Bonfire — physically-inspired fire.
 *
 *  - 16-bit heat grid advected semi-Lagrangian (bilinear sub-pixel sampling,
 *    so motion is smooth, not cell-stepped).
 *  - Two scrolling fbm fields: one bends the flow sideways (turbulence),
 *    the other modulates cooling so tongues pinch off and die separately.
 *  - Coal bed at the bottom driven by a slow 1-D noise + random crackles.
 *  - Black-body palette (sRGB key points, gamma-encoded to linear LED output).
 *  - 1/f-ish global flicker, glowing sparks carried by the same turbulence.
 *
 * Frame is split into NOINLINE stages (wasm3 has no tail calls — keep
 * function bodies small so the native stack does not overflow).
 */

typedef unsigned short uint16_t;
#define NOINLINE __attribute__((noinline))

/* ---- Metadata ---- */
static const char META[] =
    "{\"name\":\"Bonfire\","
    "\"desc\":\"Realistic fire: advected heat, turbulent tongues, glowing coals and drifting sparks\","
    "\"params\":["
        "{\"id\":0,\"name\":\"Height\",\"type\":\"int\","
         "\"min\":15,\"max\":100,\"default\":65,"
         "\"desc\":\"Flame height, % of the lamp\"},"
        "{\"id\":1,\"name\":\"Turbulence\",\"type\":\"int\","
         "\"min\":0,\"max\":100,\"default\":50,"
         "\"desc\":\"How much the tongues bend and break apart\"},"
        "{\"id\":2,\"name\":\"Speed\",\"type\":\"int\","
         "\"min\":1,\"max\":100,\"default\":50,"
         "\"desc\":\"How fast the flames rise\"},"
        "{\"id\":3,\"name\":\"Sparks\",\"type\":\"int\","
         "\"min\":0,\"max\":40,\"default\":8,"
         "\"desc\":\"Glowing sparks flying up from the coals\"},"
        "{\"id\":4,\"name\":\"Brightness\",\"type\":\"int\","
         "\"min\":1,\"max\":255,\"default\":220,"
         "\"desc\":\"Overall brightness\"},"
        "{\"id\":5,\"name\":\"Palette\",\"type\":\"select\","
         "\"options\":[\"Wood\",\"Ember\",\"Gas\",\"Copper\",\"Violet\"],"
         "\"default\":0,"
         "\"desc\":\"What is burning\"},"
        "{\"id\":6,\"name\":\"Flicker\",\"type\":\"int\","
         "\"min\":0,\"max\":100,\"default\":40,"
         "\"desc\":\"Global brightness flicker\"}"
    "]}";

EXPORT(get_meta_ptr) int get_meta_ptr(void) { return (int)META; }
EXPORT(get_meta_len) int get_meta_len(void) { return sizeof(META) - 1; }

/* ---- Buffers ---- */
#define MAX_W 64
#define MAX_H 64
#define SEAM  6                 /* columns cross-faded so noise wraps on the cylinder */
#define NW    (MAX_W + SEAM)    /* noise buffer capacity per row */
static int g_ns;                /* noise row stride this frame = W + SEAM */

static uint8_t  FB[MAX_W * MAX_H * 3];
static uint16_t HEAT_A[MAX_W * MAX_H];
static uint16_t HEAT_B[MAX_W * MAX_H];
static uint16_t *cur = HEAT_A, *nxt = HEAT_B;
static uint8_t  NZ1[NW * MAX_H];   /* lateral turbulence */
static uint8_t  NZ2[NW * MAX_H];   /* cooling modulation */
static uint8_t  NB[NW];            /* coal bed noise */
static uint8_t  LUT0[256 * 3];     /* palette, linear LED values */
static uint8_t  LUT[256 * 3];      /* palette scaled by brightness*flicker */
static int      rowT[MAX_H];       /* per-row lateral gain */
static int      rowCool[MAX_H];    /* per-row cooling (heat units / frame) */
static int      rowCarve[MAX_H];   /* per-row tongue carving (palette idx per 256 noise) */

EXPORT(get_framebuffer) int get_framebuffer(void) { return (int)FB; }

/* ---- PRNG ---- */
static uint32_t rng = 0x9E3779B9u;
static uint32_t rng_next(void) {
    uint32_t x = rng; x ^= x << 13; x ^= x >> 17; x ^= x << 5; rng = x; return x;
}
static float rnd(void) { return (float)(rng_next() & 0xFFFF) * (1.0f / 65536.0f); }

/* ---- Per-frame globals ---- */
static int   g_W, g_H;
static float g_dt;
static float g_rise;        /* px / s */
static int   g_riseq;       /* px / frame, 8.8 */
static int   g_kv;          /* vertical speed variation gain, 8.8 */
static int   g_turb;
static int   g_sparks;
static int   g_pal = -1;
static int   g_lastW, g_lastH;
static int   prev_tick;
static float oy1, oy2, ox1, ox2, tB;
static float g_flick;       /* smoothed random flicker */
static float g_scale;       /* brightness * flicker, 0..1.4 */

/* crackle (log pop) */
static float ck_timer = 1.5f;
static float ck_left;
static int   ck_x;

/* ---- Sparks ---- */
#define MAX_SP 48
static float spx[MAX_SP], spy[MAX_SP], svx[MAX_SP], svy[MAX_SP];
static float sage[MAX_SP], slife[MAX_SP];
static uint8_t son[MAX_SP];

/* ---- Palettes: {t, r, g, b} in sRGB, t 0..255 ---- */
#define KP 8
static const uint8_t PAL[5][KP][4] = {
    { /* Wood */
      {  0,   0,   0,   0}, { 34,  28,   0,   0}, { 68, 150,  12,   0}, { 98, 250,  90,   0},
      {140, 255, 160,  20}, {185, 255, 215,  60}, {225, 255, 240, 140}, {255, 255, 255, 225} },
    { /* Ember */
      {  0,   0,   0,   0}, { 36,  40,   0,   0}, { 80, 160,   8,   0}, {125, 230,  40,   0},
      {170, 255,  90,   0}, {210, 255, 140,  10}, {238, 255, 180,  40}, {255, 255, 215,  90} },
    { /* Gas */
      {  0,   0,   0,   0}, { 36,   0,   6,  40}, { 80,   4,  40, 160}, {125,  20,  90, 230},
      {170,  80, 150, 255}, {210, 170, 205, 255}, {238, 225, 235, 255}, {255, 255, 250, 235} },
    { /* Copper */
      {  0,   0,   0,   0}, { 36,   0,  14,   4}, { 80,   0,  90,  30}, {125,   0, 160,  60},
      {170,  60, 220, 120}, {210, 150, 250, 190}, {238, 210, 255, 225}, {255, 245, 255, 245} },
    { /* Violet */
      {  0,   0,   0,   0}, { 36,  20,   0,  30}, { 80,  90,   0, 140}, {125, 150,   0, 220},
      {170, 210,  60, 255}, {210, 240, 140, 255}, {238, 250, 200, 255}, {255, 255, 235, 255} },
};

static int gamma_enc(int c) {
    /* sRGB-ish value -> linear LED duty */
    float f = (float)c * (1.0f / 255.0f);
    int v = (int)(m_pow(f, 2.2f) * 255.0f + 0.5f);
    if (v > 255) v = 255;
    if (v < 0) v = 0;
    return v;
}

NOINLINE static void build_palette(int p) {
    const uint8_t (*k)[4] = PAL[p];
    int seg = 0;
    for (int i = 0; i < 256; i++) {
        while (seg < KP - 2 && i >= k[seg + 1][0]) seg++;
        int t0 = k[seg][0], t1 = k[seg + 1][0];
        int span = t1 - t0; if (span <= 0) span = 1;
        int f = (i - t0) * 256 / span; if (f > 256) f = 256; if (f < 0) f = 0;
        for (int c = 0; c < 3; c++) {
            int a = k[seg][c + 1], b = k[seg + 1][c + 1];
            int v = a + (((b - a) * f) >> 8);
            LUT0[i * 3 + c] = (uint8_t)gamma_enc(v);
        }
    }
}

NOINLINE static void scale_lut(int s256) {
    for (int i = 0; i < 256 * 3; i++) {
        int v = (LUT0[i] * s256) >> 8;
        LUT[i] = v > 255 ? 255 : (uint8_t)v;
    }
}

/* Cross-fade the first SEAM columns with the columns past W so the field is
 * continuous across the cylinder seam (x = W-1 -> x = 0). */
NOINLINE static void seam_blend(uint8_t *f, int W, int H) {
    for (int y = 0; y < H; y++) {
        uint8_t *row = f + y * g_ns;
        for (int x = 0; x < SEAM; x++) {
            int w = (x + 1) * 256 / (SEAM + 1);
            row[x] = (uint8_t)((row[x] * w + row[W + x] * (256 - w)) >> 8);
        }
    }
}

NOINLINE static void fill_noise(void) {
    int W = g_W, H = g_H;
    int cell1 = H / 5; if (cell1 < 6) cell1 = 6;
    int cell2 = H / 8; if (cell2 < 4) cell2 = 4;
    int s1 = 256 / cell1, s2 = 256 / cell2, sB = 256 / 8;

    /* turbulence field rides up with the flow, slowly drifts sideways */
    oy1 -= g_rise * 0.7f * (float)s1 * g_dt;
    ox1 += 1.5f * (float)s1 * g_dt;
    /* tongue field almost rides with the fluid (slight slip so it evolves) */
    oy2 -= g_rise * 0.85f * (float)s2 * g_dt;
    ox2 -= 0.8f * (float)s2 * g_dt;
    /* coal bed evolves slowly in time */
    tB  += 1.0f * (float)sB * g_dt;
    if (oy1 < -8.0e6f) oy1 += 8.0e6f;
    if (oy2 < -8.0e6f) oy2 += 8.0e6f;
    if (ox1 >  8.0e6f) ox1 -= 8.0e6f;
    if (ox2 < -8.0e6f) ox2 += 8.0e6f;
    if (tB  >  8.0e6f) tB  -= 8.0e6f;

    m_noise_fill(NZ1, W + SEAM, H, s1, (int)ox1, (int)oy1, 2);
    seam_blend(NZ1, W, H);
    m_noise_fill(NZ2, W + SEAM, H, s2, (int)ox2, (int)oy2, 2);
    seam_blend(NZ2, W, H);
    m_noise_fill(NB, W + SEAM, 1, sB, 90000, (int)tB, 2);
    seam_blend(NB, W, 1);
}

NOINLINE static void prepare_rows(int height_pct) {
    int W = g_W, H = g_H;
    float Hf = (float)H * (float)height_pct * 0.01f;
    if (Hf < 3.0f) Hf = 3.0f;
    /* heat lost per frame so that the mean tongue dies at Hf:
       cool(y) = c0 * (0.7 + 0.6 * y / Hf) — nearly linear so tongue height
       depends on the parcel's initial heat (flat-top otherwise) */
    float c0 = 65536.0f * 0.55f * (float)g_riseq / (256.0f * Hf);  /* heat outlives Hf: tips fade via palette + carving */
    /* lateral gain: px/frame (8.8) per noise unit (-128..127), grows with height */
    float kt = (float)g_turb * 0.01f * 48.0f * g_dt * 256.0f / 128.0f;
    float carve = 60.0f + 1.0f * (float)g_turb;
    for (int y = 0; y < H; y++) {
        float fy = (float)y;
        float u = fy / Hf;
        rowCool[y] = (int)(c0 * (u <= 1.0f ? 0.7f + 0.6f * u : 1.3f + 2.0f * (u - 1.0f)));
        rowT[y]    = (int)(kt * (0.45f + 0.9f * fy / (float)H) * 256.0f);
        float cy = fy / Hf; if (cy > 1.5f) cy = 1.5f;
        rowCarve[y] = (int)(carve * cy);
    }
    (void)W;
}

/* Semi-Lagrangian step: nxt = advect(cur) - cooling.
 * Rows well above the highest hot cell of the previous frame cannot receive
 * heat this frame, so they are just cleared (saves ~1/3 of the work). */
static int g_top = 0;

NOINLINE static void clear_row(uint16_t *out, int W) {
    for (int x = 0; x < W; x++) out[x] = 0;
}

NOINLINE static void advect(void) {
    int W = g_W, H = g_H;
    int riseq = g_riseq, kv = g_kv;
    int limit = g_top + 2 + (riseq >> 8) + (kv >> 9);
    int newTop = 0;
    for (int y = 0; y < H; y++) {
        uint16_t *out = nxt + y * W;
        if (y > limit) { clear_row(out, W); continue; }
        const uint8_t *n1 = NZ1 + y * g_ns;
        const uint8_t *n2 = NZ2 + y * g_ns;
        int rT = rowT[y], rC = rowCool[y];
        int yq = y << 8;
        int any = 0;
        for (int x = 0; x < W; x++) {
            int a = n1[x] - 128;
            int b = n2[x] - 128;
            int sxq = (x << 8) - ((a * rT) >> 8);
            int syq = yq - riseq + ((b * kv) >> 8);
            int ix = sxq >> 8, fx = sxq & 255;
            if (ix < 0) ix += W; else if (ix >= W) ix -= W;
            int ix1 = ix + 1; if (ix1 == W) ix1 = 0;
            int iy = syq >> 8, fy = syq & 255;
            if (iy < 0) { iy = 0; fy = 0; }
            int iy1 = iy + 1; if (iy1 >= H) iy1 = H - 1;
            const uint16_t *r0 = cur + iy * W;
            const uint16_t *r1 = cur + iy1 * W;
            int h00 = r0[ix], h10 = r0[ix1], h01 = r1[ix], h11 = r1[ix1];
            int top = h00 + (((h10 - h00) * fx) >> 8);
            int bot = h01 + (((h11 - h01) * fx) >> 8);
            int h = top + (((bot - top) * fy) >> 8);
            h -= (rC * (256 + b)) >> 8;              /* cooling x0.5 .. x1.5 */
            if (h < 0) h = 0;
            any |= h;
            out[x] = (uint16_t)h;
        }
        if (any) newTop = y;
    }
    g_top = newTop;
}

/* Coal bed: bottom row is a Dirichlet source (written into nxt after advect) */
NOINLINE static void coal_bed(void) {
    int W = g_W;
    uint16_t *row = nxt;
    for (int x = 0; x < W; x++) {
        /* fbm clusters around 128 — stretch contrast so coals have hot spots */
        int c = (NB[x] - 90) * 5 / 2;
        if (c < 0) c = 0; else if (c > 255) c = 255;
        int h = 34000 + c * 123;                 /* 34000 .. 65365 */
        if (ck_left > 0.0f) {
            int d = x - ck_x; if (d < 0) d = -d; if (d > W - d) d = W - d;
            if (d <= 2) h += (int)(ck_left * 60000.0f) >> d;
        }
        if (h > 65535) h = 65535;
        row[x] = (uint16_t)h;
    }
}

/* Palette lookup; the tongue field is subtracted with a weight that grows
 * with height, so the base is a solid sheet and the tips break into tongues. */
NOINLINE static void render(void) {
    int W = g_W, H = g_H;
    uint8_t *p = FB;
    for (int y = 0; y < H; y++) {
        const uint16_t *h = cur + y * W;
        const uint8_t *n2 = NZ2 + y * g_ns;
        int rc = rowCarve[y];
        for (int x = 0; x < W; x++) {
            int idx = (h[x] >> 8) + (((n2[x] - 128) * rc) >> 8);
            if (idx < 0) idx = 0; else if (idx > 255) idx = 255;
            const uint8_t *c = LUT + idx * 3;
            p[0] = c[0]; p[1] = c[1]; p[2] = c[2];
            p += 3;
        }
    }
}

NOINLINE static void spawn_spark(float x, float y, float boost) {
    for (int i = 0; i < MAX_SP; i++) {
        if (son[i]) continue;
        son[i] = 1;
        spx[i] = x; spy[i] = y;
        svx[i] = (rnd() - 0.5f) * 8.0f * (1.0f + boost);
        svy[i] = g_rise * (1.1f + 0.9f * rnd()) * (1.0f + 0.5f * boost);
        sage[i] = 0.0f;
        slife[i] = 0.5f + rnd() * 1.1f;
        return;
    }
}

NOINLINE static void sparks(void) {
    int W = g_W, H = g_H;
    float dt = g_dt;
    /* spawn from the hottest coals */
    float rate = (float)g_sparks * 1.3f * dt;
    while (rate > 0.0f) {
        if (rnd() < rate) {
            int x = (int)(rnd() * (float)W);
            if (NB[x] > 110) spawn_spark((float)x + rnd(), 0.5f + rnd() * 1.5f, 0.0f);
        }
        rate -= 1.0f;
    }
    float tg = (float)g_turb * 0.01f;
    for (int i = 0; i < MAX_SP; i++) {
        if (!son[i]) continue;
        sage[i] += dt;
        if (sage[i] >= slife[i]) { son[i] = 0; continue; }
        int ix = (int)spx[i], iy = (int)spy[i];
        if (ix < 0) ix = 0; if (ix >= W) ix = W - 1;
        if (iy < 0) iy = 0; if (iy >= H) iy = H - 1;
        float a = (float)(NZ1[iy * g_ns + ix] - 128) * (1.0f / 128.0f);
        svx[i] += (a * 60.0f * tg + (rnd() - 0.5f) * 40.0f) * dt;
        svx[i] *= 1.0f - 1.8f * dt;
        svy[i] += (rnd() - 0.5f) * 10.0f * dt;
        spx[i] += svx[i] * dt;
        spy[i] += svy[i] * dt;
        if (spx[i] < 0.0f) spx[i] += (float)W;
        if (spx[i] >= (float)W) spx[i] -= (float)W;
        if (spy[i] >= (float)H + 1.0f) { son[i] = 0; continue; }
        float u = sage[i] / slife[i];
        float fade = 1.0f - u * u * u;
        int t = (int)(240.0f - 130.0f * u);
        const uint8_t *c = LUT + t * 3;
        int r = (int)(c[0] * fade), g = (int)(c[1] * fade), b = (int)(c[2] * fade);
        m_blend(FB, W, H, spx[i], spy[i], (r << 16) | (g << 8) | b);
        /* seam copy so a spark crossing x=0 does not blink */
        if (spx[i] < 1.0f) m_blend(FB, W, H, spx[i] + (float)W, spy[i], (r << 16) | (g << 8) | b);
        else if (spx[i] > (float)W - 1.0f) m_blend(FB, W, H, spx[i] - (float)W, spy[i], (r << 16) | (g << 8) | b);
    }
}

NOINLINE static void crackle(void) {
    float dt = g_dt;
    ck_timer -= dt;
    if (ck_left > 0.0f) ck_left -= dt * 4.0f;
    if (ck_timer <= 0.0f) {
        ck_timer = 0.8f + rnd() * 3.5f;
        ck_x = (int)(rnd() * (float)g_W);
        ck_left = 0.6f + rnd() * 0.4f;
        int n = 2 + (int)(rnd() * 5.0f);
        if (g_sparks == 0) n = 0;
        for (int i = 0; i < n; i++)
            spawn_spark((float)ck_x + (rnd() - 0.5f) * 5.0f, 0.5f + rnd() * 2.0f, 0.3f + rnd() * 1.4f);
    }
}

NOINLINE static void flicker(int tick_ms, int amount) {
    float t = (float)tick_ms * 0.001f;
    g_flick += (rnd() - 0.5f) * 0.5f;
    g_flick *= 0.82f;
    float f = 0.55f * m_sin(t * 23.0f) + 0.30f * m_sin(t * 41.3f) + 0.15f * m_sin(t * 67.1f);
    f = (f + g_flick) * 0.12f * (float)amount * 0.01f;
    g_scale = 1.0f + f;
}

NOINLINE static void reset_state(void) {
    for (int i = 0; i < MAX_W * MAX_H; i++) { HEAT_A[i] = 0; HEAT_B[i] = 0; }
    for (int i = 0; i < MAX_SP; i++) son[i] = 0;
    g_flick = 0.0f;
}

EXPORT(init)
void init(void) {
    reset_state();
    rng = 0x9E3779B9u;
    prev_tick = 0;
    oy1 = 0.0f; oy2 = 0.0f; ox1 = 0.0f; ox2 = 40000.0f; tB = 0.0f;
    g_pal = -1;
    g_lastW = g_lastH = 0;
    ck_timer = 1.5f; ck_left = 0.0f;
}

EXPORT(update)
void update(int tick_ms) {
    int W = get_width(), H = get_height();
    if (W > MAX_W) W = MAX_W;
    if (H > MAX_H) H = MAX_H;
    if (W != g_lastW || H != g_lastH) { reset_state(); g_lastW = W; g_lastH = H; }
    g_W = W; g_H = H; g_ns = W + SEAM;

    int delta = tick_ms - prev_tick;
    if (delta <= 0 || delta > 100) delta = 33;
    prev_tick = tick_ms;
    g_dt = (float)delta * 0.001f;
    rng ^= (uint32_t)tick_ms;

    int height = get_param_i32(0);
    g_turb     = get_param_i32(1);
    int speed  = get_param_i32(2);
    g_sparks   = get_param_i32(3);
    int bright = get_param_i32(4);
    int pal    = get_param_i32(5);
    int flick  = get_param_i32(6);
    if (pal < 0 || pal > 4) pal = 0;
    if (pal != g_pal) { g_pal = pal; build_palette(pal); }

    g_rise  = (float)H * (0.3f + 0.9f * (float)speed * 0.01f);   /* px / s */
    g_riseq = (int)(g_rise * g_dt * 256.0f);
    if (g_riseq < 1) g_riseq = 1;
    g_kv    = (g_riseq * 90) >> 8;                                 /* +-35 % */

    flicker(tick_ms, flick);
    scale_lut((int)(g_scale * (float)bright));

    fill_noise();
    prepare_rows(height);
    advect();
    coal_bed();
    { uint16_t *t = cur; cur = nxt; nxt = t; }

    render();
    crackle();
    sparks();
    draw();
}
