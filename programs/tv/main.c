#include "api.h"

/*
 * TV — retro television set.
 * Cycles through several "channels" rendered as low-res TV imagery:
 *   0 Test Card  — SMPTE-style colour bars
 *   1 Cola       — contour bottle on red with a moving white ribbon
 *   2 News       — anchor + scrolling lower-third ticker
 *   3 Ad         — flashy expanding-rings advertisement
 * Over the top: CRT scanlines, signal snow, channel-change static and an
 * occasional vertical-hold roll. A colour-mode switch gives Colour / Sepia / B&W.
 */

static const char META[] =
    "{\"name\":\"TV\","
    "\"desc\":\"Retro television: channels, static and scanlines\","
    "\"params\":["
        "{\"id\":0,\"name\":\"Picture\",\"type\":\"select\","
         "\"options\":[\"Colour\",\"Sepia\",\"B&W\"],\"default\":1,"
         "\"desc\":\"Colour mode\"},"
        "{\"id\":1,\"name\":\"Channel\",\"type\":\"select\","
         "\"options\":[\"Auto\",\"Test Card\",\"Cola\",\"News\",\"Ad\",\"Run\"],\"default\":0,"
         "\"desc\":\"Auto cycles channels, or pick one\"},"
        "{\"id\":2,\"name\":\"Dwell\",\"type\":\"int\","
         "\"min\":2,\"max\":20,\"default\":6,"
         "\"desc\":\"Seconds per channel (Auto)\"},"
        "{\"id\":3,\"name\":\"Brightness\",\"type\":\"int\","
         "\"min\":1,\"max\":255,\"default\":220,"
         "\"desc\":\"Overall brightness\"},"
        "{\"id\":4,\"name\":\"Scanlines\",\"type\":\"int\","
         "\"min\":0,\"max\":100,\"default\":55,"
         "\"desc\":\"CRT scanline strength\"},"
        "{\"id\":5,\"name\":\"Static\",\"type\":\"int\","
         "\"min\":0,\"max\":100,\"default\":35,"
         "\"desc\":\"Signal noise and glitches\"}"
    "]}";

EXPORT(get_meta_ptr)
int get_meta_ptr(void) { return (int)META; }
EXPORT(get_meta_len)
int get_meta_len(void) { return sizeof(META) - 1; }

/* ---- math ---- */
#define TWO_PI 6.28318530f
#define PI     3.14159265f

static float fsin(float x) { return m_sin(x); }
static float fcos(float x) { return m_cos(x); }
static float fabsf2(float x) { return x < 0.0f ? -x : x; }
static float fracf(float x)  { return x - (float)((int)x); }
static int   clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

/* ---- PRNG / per-pixel hash ---- */
static uint32_t hash3(int x, int y, int t) {
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u
               + (uint32_t)t * 362437u;
    h ^= h >> 13; h *= 1274126177u; h ^= h >> 16;
    return h;
}

/* ════════════════════════ scenes (normalised u,v) ═══════════════════════ */
/* u: 0..1 around the cylinder (periodic), v: 0..1 top->bottom. t: seconds. */

static void scene_testcard(float u, float v, int *r, int *g, int *b) {
    static const unsigned char BC[7][3] = {
        {235,235,235},{225,215,40},{40,205,215},{45,195,70},
        {205,60,195},{215,55,55},{60,80,215}
    };
    static const unsigned char BC2[7][3] = {
        {60,80,215},{18,18,18},{205,60,195},{18,18,18},
        {40,205,215},{18,18,18},{235,235,235}
    };
    if (v < 0.70f) {
        int bar = clampi((int)(u * 7.0f), 0, 6);
        *r = BC[bar][0]; *g = BC[bar][1]; *b = BC[bar][2];
    } else if (v < 0.80f) {
        int bar = clampi((int)(u * 7.0f), 0, 6);
        *r = BC2[bar][0]; *g = BC2[bar][1]; *b = BC2[bar][2];
    } else {
        int seg = clampi((int)(u * 6.0f), 0, 5);
        int gray = (seg == 2) ? 40 : (seg == 3) ? 75 : (seg == 4) ? 12 : 26;
        *r = *g = *b = gray;
    }
}

static void scene_cola(float u, float v, float t, int *r, int *g, int *b) {
    float ax = fabsf2(u - 0.5f);
    float hw;                                   /* bottle half-width vs height */
    if      (v < 0.10f) hw = 0.0f;
    else if (v < 0.16f) hw = 0.055f;            /* cap */
    else if (v < 0.26f) hw = 0.05f;             /* neck */
    else if (v < 0.36f) hw = 0.05f + (v-0.26f)/0.10f*0.12f; /* shoulder */
    else if (v < 0.46f) hw = 0.17f;             /* upper body */
    else if (v < 0.52f) hw = 0.17f - (v-0.46f)/0.06f*0.03f; /* contour pinch */
    else if (v < 0.92f) hw = 0.14f + (v-0.52f)/0.40f*0.03f; /* lower body */
    else                hw = 0.0f;

    int isBottle = (hw > 0.001f && ax < hw);
    if (isBottle) {
        *r = 245; *g = 245; *b = 238;           /* white contour bottle */
    } else {
        *r = 200; *g = 14; *b = 30;             /* Coca-Cola red */
    }
    /* iconic dynamic ribbon: white wave across the lower background */
    float wave = 0.82f + 0.07f * fsin(u * TWO_PI + t * 2.0f);
    if (!isBottle && fabsf2(v - wave) < 0.045f) { *r = 250; *g = 250; *b = 245; }
}

static void scene_news(float u, float v, float t, int *r, int *g, int *b) {
    *r = 14; *g = 22; *b = 38;                   /* studio background */
    if (v < 0.10f) { *r = 175; *g = 30; *b = 30; }   /* top banner */

    float dx = u - 0.5f, dy = v - 0.34f;
    float d  = m_hypot(dx, dy);
    if (d < 0.125f) { *r = 205; *g = 160; *b = 120; }      /* head (skin) */
    if (d < 0.135f && v < 0.30f) { *r = 60; *g = 42; *b = 30; } /* hair */

    if (v > 0.46f && v < 0.78f) {                /* shoulders / suit */
        float sw = 0.18f + (v - 0.46f) * 0.7f;
        if (fabsf2(dx) < sw)    { *r = 30; *g = 34; *b = 46; }
        if (fabsf2(dx) < 0.06f && v < 0.60f) { *r = 200; *g = 205; *b = 215; } /* collar */
    }
    if (v >= 0.80f && v < 0.92f) {               /* lower-third ticker */
        *r = 150; *g = 20; *b = 20;
        int seg = (int)((u + t * 0.25f) * 16.0f);
        if (seg & 1) { *r = 230; *g = 230; *b = 225; }
    }
    if (v >= 0.92f) { *r = 20; *g = 20; *b = 24; }
}

static void scene_ad(float u, float v, float t, int *r, int *g, int *b) {
    float dx = u - 0.5f, dy = v - 0.5f;
    float d  = m_hypot(dx, dy);
    int phase = ((int)(t / 0.45f)) & 1;          /* colour blink */
    int c1r,c1g,c1b, c2r,c2g,c2b;
    if (phase) { c1r=255;c1g=210;c1b=40; c2r=220;c2g=30;c2b=40; }
    else       { c1r=220;c1g=30;c1b=40; c2r=255;c2g=210;c2b=40; }

    float ring = fracf(d * 6.0f - t * 2.5f);     /* expanding rings */
    if (ring < 0.5f) { *r=c1r; *g=c1g; *b=c1b; } else { *r=c2r; *g=c2g; *b=c2b; }

    float pr = 0.12f + 0.05f * fsin(t * 6.0f);   /* pulsing centre disk */
    if (d < pr) { *r = 255; *g = 255; *b = 255; }

    int blink = ((int)(t / 0.30f)) & 1;          /* blinking bezel */
    if ((u < 0.06f || u > 0.94f || v < 0.05f || v > 0.95f) && blink) {
        *r = 255; *g = 255; *b = 255;
    }
}

/* ---- running figure: limb segments rebuilt once per frame ---- */
#define MAXSEG 10
static float seg_ax[MAXSEG], seg_ay[MAXSEG], seg_bx[MAXSEG], seg_by[MAXSEG];
static int   seg_n;
static float run_hx, run_hy, run_hr, run_thick;

static float seg_dist(float px, float py, float ax, float ay, float bx, float by) {
    float dx = bx - ax, dy = by - ay;
    float l2 = dx*dx + dy*dy;
    float tt = (l2 > 0.0f) ? ((px-ax)*dx + (py-ay)*dy) / l2 : 0.0f;
    if (tt < 0.0f) tt = 0.0f; if (tt > 1.0f) tt = 1.0f;
    float ex = px - (ax + tt*dx), ey = py - (ay + tt*dy);
    return m_hypot(ex, ey);
}
static void addseg(float ax, float ay, float bx, float by) {
    if (seg_n >= MAXSEG) return;
    seg_ax[seg_n]=ax; seg_ay[seg_n]=ay; seg_bx[seg_n]=bx; seg_by[seg_n]=by; seg_n++;
}

/* Build the runner pose for time t on a w x h screen (py grows downward). */
static void runner_prepare(float t, int w, int h) {
    float fh = (float)h, fw = (float)w;
    float ph  = t * 9.0f;                         /* stride cadence */
    float bob = 0.012f * fh * fsin(ph * 2.0f);    /* vertical bounce */
    float cx  = fw * 0.5f;
    float lean = 0.03f * fh;

    float neck = 0.27f * fh + bob;
    float hipx = cx,           hipy = 0.52f * fh + bob;
    float shx  = cx + lean,    shy  = neck;

    run_hr    = 0.075f * fh;
    run_hx    = cx + lean * 1.2f;
    run_hy    = 0.15f * fh + bob;
    run_thick = 0.05f * fh; if (run_thick < 1.2f) run_thick = 1.2f;

    seg_n = 0;
    addseg(shx, shy, hipx, hipy);                 /* torso */

    /* legs (opposite phases) */
    float Lt = 0.17f*fh, Ls = 0.17f*fh;
    for (int k = 0; k < 2; k++) {
        float p = ph + (k ? PI : 0.0f);
        float thigh = 0.85f * fsin(p);
        float bend  = 0.45f + 0.75f * (0.5f + 0.5f * fsin(p + 1.2f));
        float kx = hipx + Lt*fsin(thigh),  ky = hipy + Lt*fcos(thigh);
        float shin = thigh - bend;
        float fx = kx + Ls*fsin(shin),     fy = ky + Ls*fcos(shin);
        addseg(hipx, hipy, kx, ky);
        addseg(kx, ky, fx, fy);
    }
    /* arms (opposite phase to legs) */
    float La = 0.105f*fh, Lf = 0.095f*fh;
    for (int k = 0; k < 2; k++) {
        float p = ph + (k ? 0.0f : PI);
        float up = 0.55f * fsin(p);
        float ex = shx + La*fsin(up),  ey = shy + La*fcos(up);
        float fore = up + 0.9f;
        float hx = ex + Lf*fsin(fore), hy = ey + Lf*fcos(fore);
        addseg(shx, shy, ex, ey);
        addseg(ex, ey, hx, hy);
    }
}

static void scene_runner(float px, float py, float t, int w, int h, int *r, int *g, int *b) {
    *r = 12; *g = 16; *b = 26;                     /* studio dark */

    /* scrolling ground to imply forward motion */
    float gy = 0.90f * (float)h;
    if (py > gy && py < gy + 0.05f * (float)h) {
        int seg = (int)((px / (float)w + t * 0.6f) * 10.0f);
        if (seg & 1) { *r = 120; *g = 90; *b = 50; } else { *r = 45; *g = 33; *b = 18; }
    }

    /* figure: head disc + limb segments */
    float hdx = px - run_hx, hdy = py - run_hy;
    int onFig = (hdx*hdx + hdy*hdy < run_hr*run_hr);
    if (!onFig) {
        float md = 1e9f;
        for (int i = 0; i < seg_n; i++) {
            float d = seg_dist(px, py, seg_ax[i], seg_ay[i], seg_bx[i], seg_by[i]);
            if (d < md) md = d;
        }
        if (md < run_thick) onFig = 1;
    }
    if (onFig) { *r = 235; *g = 235; *b = 228; }
}

/* ---- colour mode ---- */
static void apply_mode(int mode, int *r, int *g, int *b) {
    if (mode == 0) return;                        /* colour */
    int lum = (*r * 77 + *g * 150 + *b * 29) >> 8;
    if (mode == 1) {                              /* sepia / amber */
        *r = clampi(lum + (lum >> 4), 0, 255);
        *g = (lum * 200) >> 8;
        *b = (lum * 110) >> 8;
    } else {                                      /* warm phosphor B&W */
        *r = lum;
        *g = (lum * 245) >> 8;
        *b = (lum * 230) >> 8;
    }
}

EXPORT(init)
void init(void) { }

EXPORT(update)
void update(int tick_ms) {
    int mode    = get_param_i32(0);
    int sel     = get_param_i32(1);
    int dwell   = get_param_i32(2);
    int bright  = get_param_i32(3);
    int scan    = get_param_i32(4);
    int noise   = get_param_i32(5);
    if (dwell < 2) dwell = 2;

    int w = get_width();
    int h = get_height();
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    float fw = (float)w;
    float fh = (h > 1) ? (float)(h - 1) : 1.0f;
    float t  = (float)tick_ms / 1000.0f;

    /* ── channel selection + transition snow ── */
    int channel, transition = 0;
    const int NUM = 5;
    if (sel == 0) {
        int period = dwell * 1000;
        int idx = tick_ms / period;
        channel = ((idx % NUM) + NUM) % NUM;
        if (tick_ms - idx * period < 360) transition = 1;
    } else {
        channel = clampi(sel - 1, 0, NUM - 1);
    }

    /* ── vertical-hold roll glitch ── */
    float roll = 0.0f;
    if (noise > 12) {
        int cyc = tick_ms % 4200;
        if (cyc < 320) roll = (float)cyc / 320.0f;
    }

    int grainP = 2 + noise * 40 / 100;           /* speck probability /1000 */

    if (!transition && channel == 4) runner_prepare(t, w, h);

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            float u  = (float)x / fw;
            /* y=0 is the BOTTOM of the lamp, so flip: v=0 = top of the picture */
            float v  = 1.0f - (float)y / fh;
            if (v >= 1.0f) v = 0.9999f;
            float vr = v + roll;
            vr -= (float)((int)vr);              /* wrap into 0..1 */

            int r, g, b;
            if (transition) {                    /* full snow between channels */
                int n = hash3(x, y, tick_ms) & 255;
                r = g = b = n;
            } else {
                switch (channel) {
                    case 0: scene_testcard(u, vr, &r, &g, &b); break;
                    case 1: scene_cola(u, vr, t, &r, &g, &b); break;
                    case 2: scene_news(u, vr, t, &r, &g, &b); break;
                    case 3: scene_ad(u, vr, t, &r, &g, &b); break;
                    default: scene_runner(u * fw, vr * (float)h, t, w, h, &r, &g, &b); break;
                }
                apply_mode(mode, &r, &g, &b);
            }

            /* CRT scanlines on odd physical rows */
            if (scan > 0 && (y & 1)) {
                int m = 256 - (scan * 140 / 100);
                r = (r * m) >> 8; g = (g * m) >> 8; b = (b * m) >> 8;
            }

            /* signal grain: occasional white specks */
            uint32_t hsh = hash3(x * 7 + 1, y * 3 + 2, tick_ms);
            if ((int)(hsh % 1000u) < grainP) {
                int sp = 200 + (int)(hsh & 55u);
                r = sp; g = sp; b = sp;
            }

            /* top/bottom vignette (screen bezel) */
            if (v < 0.05f || v > 0.95f) { r = (r * 130) >> 8; g = (g * 130) >> 8; b = (b * 130) >> 8; }

            /* master brightness */
            r = r * bright / 255; g = g * bright / 255; b = b * bright / 255;

            set_pixel(x, y, clampi(r,0,255), clampi(g,0,255), clampi(b,0,255));
        }
    }
    draw();
}
