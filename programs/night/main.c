#include "api.h"

/* Night — a calm starry sky on black.
 *
 * Stars drift slowly around the cylinder (parallax: brighter = nearer =
 * faster) and twinkle; occasional meteors streak down with an anti-aliased
 * trail. Optional crescent moon with a soft halo, and an optional plane
 * crossing with red/green nav lights and a white strobe.
 *
 * Everything is rendered additively with sub-pixel anti-aliasing (m_blend
 * splats, m_line trails, analytic discs) into the framebuffer fast-path,
 * so all motion is smooth on the low-res matrix. y=0 is the bottom row.
 */

/* ---- Metadata JSON ---- */
static const char META[] =
    "{\"name\":\"Night\","
    "\"desc\":\"Drifting starry sky with meteors, moon and a passing plane\","
    "\"params\":["
        "{\"id\":0,\"name\":\"Speed\",\"type\":\"int\","
         "\"min\":1,\"max\":10,\"default\":3,"
         "\"desc\":\"How fast the sky drifts\"},"
        "{\"id\":1,\"name\":\"Stars\",\"type\":\"int\","
         "\"min\":1,\"max\":30,\"default\":15,"
         "\"desc\":\"Star density\"},"
        "{\"id\":2,\"name\":\"Brightness\",\"type\":\"int\","
         "\"min\":1,\"max\":255,\"default\":200,"
         "\"desc\":\"Overall brightness\"},"
        "{\"id\":3,\"name\":\"Meteors\",\"type\":\"int\","
         "\"min\":0,\"max\":10,\"default\":3,"
         "\"desc\":\"Meteor frequency (0 = none)\"},"
        "{\"id\":4,\"name\":\"Moon\",\"type\":\"select\","
         "\"options\":[\"Off\",\"On\"],\"default\":1,"
         "\"desc\":\"Crescent moon with a soft halo\"},"
        "{\"id\":5,\"name\":\"Plane\",\"type\":\"select\","
         "\"options\":[\"Off\",\"On\"],\"default\":1,"
         "\"desc\":\"A plane passes with red/green nav lights\"}"
    "]}";

EXPORT(get_meta_ptr)
int get_meta_ptr(void) { return (int)META; }

EXPORT(get_meta_len)
int get_meta_len(void) { return sizeof(META) - 1; }

/* ---- PRNG (xorshift32) ---- */
static uint32_t rng_state = 20260703;

static uint32_t rng_next(void) {
    uint32_t x = rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng_state = x;
    return x;
}

static float random_float(void) {
    return (float)(rng_next() & 0xFFFF) / 65536.0f;
}

/* ---- Framebuffer fast-path ---- */
#define MAX_W 64
#define MAX_H 64
static uint8_t FB[MAX_W * MAX_H * 3];

EXPORT(get_framebuffer)
int get_framebuffer(void) { return (int)FB; }

#define TWO_PI 6.2831853f

static uint8_t qadd8(uint8_t a, int add) {
    int s = (int)a + add;
    return (s > 255) ? 255 : (uint8_t)s;
}

static int ifloor(float v) {
    int i = (int)v;
    return ((float)i > v) ? i - 1 : i;
}

static int pack3(float r, float g, float b) {
    int ir = (int)r, ig = (int)g, ib = (int)b;
    if (ir > 255) ir = 255; if (ir < 0) ir = 0;
    if (ig > 255) ig = 255; if (ig < 0) ig = 0;
    if (ib > 255) ib = 255; if (ib < 0) ib = 0;
    return (ir << 16) | (ig << 8) | ib;
}

/* AA point splat with cylinder-seam mirroring. */
static void splat(int W, int H, float fx, float fy, int rgb) {
    if (fx < 0.0f) fx += (float)W;
    else if (fx >= (float)W) fx -= (float)W;
    m_blend(FB, W, H, fx, fy, rgb);
    if (fx > (float)(W - 1))                 /* the 2x2 footprint crosses the seam */
        m_blend(FB, W, H, fx - (float)W, fy, rgb);
}

/* ---- Stars ---- */
#define MAX_STARS 128

static uint8_t st_state[MAX_STARS];   /* 0 off, 1 alive, 2 dying */
static float   st_x[MAX_STARS];
static float   st_y[MAX_STARS];
static float   st_depth[MAX_STARS];   /* 0.35..1 — brightness + drift speed */
static float   st_lvl[MAX_STARS];     /* spawn/despawn fade 0..1 */
static float   st_twph[MAX_STARS];    /* twinkle phase */
static float   st_twfr[MAX_STARS];    /* twinkle speed, rad/s */
static float   st_twam[MAX_STARS];    /* twinkle amplitude 0..1 */
static uint8_t st_tint[MAX_STARS];    /* 0 cool, 1 white, 2 warm */

/* ---- Meteors ---- */
#define MAX_MET 3
static uint8_t met_on[MAX_MET];
static float   met_x[MAX_MET], met_y[MAX_MET];
static float   met_vx[MAX_MET], met_vy[MAX_MET];
static float   met_life[MAX_MET], met_max[MAX_MET];

/* ---- Moon / plane ---- */
static float moon_x = -1.0f;          /* set on first update (needs W) */

static uint8_t pl_on;
static float   pl_x, pl_y, pl_v, pl_dist, pl_len, pl_strobe;
static float   pl_dir;

static int32_t prev_tick;

EXPORT(init)
void init(void) {
    rng_state = 20260703;
    prev_tick = 0;
    moon_x = -1.0f;
    pl_on = 0;
    for (int i = 0; i < MAX_STARS; i++) st_state[i] = 0;
    for (int i = 0; i < MAX_MET; i++) met_on[i] = 0;
}

static void star_colour(int tint, float b, float* r, float* g, float* bl) {
    if (tint == 0)      { *r = b * 0.78f; *g = b * 0.86f; *bl = b;         }  /* cool  */
    else if (tint == 1) { *r = b * 0.92f; *g = b * 0.92f; *bl = b * 0.96f; }  /* white */
    else                { *r = b;         *g = b * 0.88f; *bl = b * 0.72f; }  /* warm  */
}

/* Crescent moon with a 1-px AA rim and a faint halo; cylinder-wrapped. */
static void draw_moon(int W, int H, float mx, float my, float R, float b) {
    float halo = R * 2.3f;
    int x0 = ifloor(mx - halo), x1 = ifloor(mx + halo) + 1;
    int y0 = ifloor(my - halo), y1 = ifloor(my + halo) + 1;
    /* the "bite" that carves the crescent, offset to the upper-right */
    float bx = mx + R * 0.55f, by = my + R * 0.28f, Rb = R * 0.92f;
    for (int y = y0; y <= y1; y++) {
        if (y < 0 || y >= H) continue;
        float dy = (float)y - my;
        for (int x = x0; x <= x1; x++) {
            float dx = (float)x - mx;
            float d = __builtin_sqrtf(dx * dx + dy * dy);
            /* solid disc with a 1-px anti-aliased rim */
            float c = R - d + 0.5f;
            if (c > 1.0f) c = 1.0f;
            float glow = 1.0f - d / halo;
            if (c <= 0.0f && glow <= 0.0f) continue;
            if (c > 0.0f) {
                float dbx = (float)x - bx, dby = (float)y - by;
                float db = __builtin_sqrtf(dbx * dbx + dby * dby);
                float cb = Rb - db + 0.5f;
                if (cb > 1.0f) cb = 1.0f;
                if (cb > 0.0f) c *= (1.0f - cb * 0.94f);
            }
            float v = 0.0f;
            if (c > 0.0f) v += c;
            if (glow > 0.0f) v += glow * glow * 0.10f;
            if (v <= 0.003f) continue;
            int xx = x;
            if (xx < 0) xx += W;
            else if (xx >= W) xx -= W;
            int idx = (y * W + xx) * 3;
            float m = b * v;
            FB[idx]     = qadd8(FB[idx],     (int)(m * 0.94f));
            FB[idx + 1] = qadd8(FB[idx + 1], (int)(m * 0.92f));
            FB[idx + 2] = qadd8(FB[idx + 2], (int)(m * 0.84f));
        }
    }
}

/* One meteor trail segment, mirrored across the seam when needed. */
static void met_segment(int W, int H, float x0, float y0, float x1, float y1, int rgb) {
    m_line(FB, W, H, x0, y0, x1, y1, rgb);
    float lo = (x0 < x1) ? x0 : x1;
    float hi = (x0 > x1) ? x0 : x1;
    if (lo < 1.0f)              m_line(FB, W, H, x0 + (float)W, y0, x1 + (float)W, y1, rgb);
    if (hi > (float)(W - 2))    m_line(FB, W, H, x0 - (float)W, y0, x1 - (float)W, y1, rgb);
}

EXPORT(update)
void update(int tick_ms) {
    int speed   = get_param_i32(0);   /* 1-10  */
    int density = get_param_i32(1);   /* 1-30  */
    int bright  = get_param_i32(2);   /* 1-255 */
    int meteors = get_param_i32(3);   /* 0-10  */
    int moon_on = get_param_i32(4);   /* 0/1   */
    int plane_on = get_param_i32(5);  /* 0/1   */

    int W = get_width();
    int H = get_height();
    if (W > MAX_W) W = MAX_W;
    if (H > MAX_H) H = MAX_H;
    if (W < 2) W = 2;
    if (H < 2) H = 2;

    int dms = tick_ms - prev_tick;
    if (dms <= 0 || dms > 200) dms = 33;
    prev_tick = tick_ms;
    float dt = (float)dms * 0.001f;
    float t = (float)tick_ms * 0.001f;

    rng_state ^= (uint32_t)tick_ms;

    m_fill(FB, W * H, 0);                       /* black sky */

    float drift = (float)speed * 0.22f;         /* px/s at depth 1 */
    float B = (float)bright;

    /* ---- Stars: hold a density-scaled count, fading in/out one per frame ---- */
    int target = density * W * H / 128;
    if (target > MAX_STARS) target = MAX_STARS;
    if (target < 1) target = 1;
    int alive = 0;
    for (int i = 0; i < MAX_STARS; i++) alive += (st_state[i] == 1);
    if (alive < target) {
        for (int i = 0; i < MAX_STARS; i++) {
            if (st_state[i]) continue;
            float d = random_float();
            st_state[i] = 1;
            st_x[i] = random_float() * (float)W;
            st_y[i] = random_float() * (float)H;
            st_depth[i] = 0.35f + 0.65f * d * d;   /* many dim, few bright */
            st_lvl[i] = 0.0f;
            st_twph[i] = random_float() * TWO_PI;
            st_twfr[i] = 0.4f + random_float() * 1.6f;
            st_twam[i] = 0.20f + random_float() * 0.35f;
            st_tint[i] = (uint8_t)(rng_next() % 3);
            break;
        }
    } else if (alive > target) {
        for (int i = 0; i < MAX_STARS; i++) {
            if (st_state[i] == 1) { st_state[i] = 2; break; }
        }
    }

    for (int i = 0; i < MAX_STARS; i++) {
        if (!st_state[i]) continue;
        if (st_state[i] == 1) {
            st_lvl[i] += dt * 0.8f;
            if (st_lvl[i] > 1.0f) st_lvl[i] = 1.0f;
        } else {
            st_lvl[i] -= dt * 0.8f;
            if (st_lvl[i] <= 0.0f) { st_state[i] = 0; continue; }
        }

        float depth = st_depth[i];
        st_x[i] -= drift * (0.3f + 0.7f * depth) * dt;   /* sky rotation + parallax */
        if (st_x[i] < 0.0f) st_x[i] += (float)W;

        float tw = 1.0f - st_twam[i] * (0.5f + 0.5f * m_sin(t * st_twfr[i] + st_twph[i]));
        float b = B * (0.16f + 0.84f * depth) * st_lvl[i] * tw;

        float r, g, bl;
        star_colour(st_tint[i], b, &r, &g, &bl);
        int c = pack3(r, g, bl);
        splat(W, H, st_x[i], st_y[i], c);
        if (depth > 0.88f) {                    /* the brightest few get a soft glow */
            int cg = pack3(r * 0.22f, g * 0.22f, bl * 0.22f);
            splat(W, H, st_x[i] - 0.7f, st_y[i], cg);
            splat(W, H, st_x[i] + 0.7f, st_y[i], cg);
            splat(W, H, st_x[i], st_y[i] - 0.7f, cg);
            splat(W, H, st_x[i], st_y[i] + 0.7f, cg);
        }
    }

    /* ---- Meteors ---- */
    if (meteors > 0) {
        float rate = (float)meteors * 0.05f;    /* spawns/s; 3 -> ~every 7 s */
        if (random_float() < rate * dt) {
            for (int i = 0; i < MAX_MET; i++) {
                if (met_on[i]) continue;
                float vm = 14.0f + random_float() * 8.0f;
                float side = (rng_next() & 1) ? 1.0f : -1.0f;
                met_on[i] = 1;
                met_x[i] = random_float() * (float)W;
                met_y[i] = (float)H * (0.75f + random_float() * 0.3f);
                met_vx[i] = side * vm * (0.4f + random_float() * 0.4f);
                met_vy[i] = -vm * (0.8f + random_float() * 0.4f);
                met_life[i] = 0.0f;
                met_max[i] = 0.9f + random_float() * 0.5f;
                break;
            }
        }
    }
    for (int i = 0; i < MAX_MET; i++) {
        if (!met_on[i]) continue;
        met_life[i] += dt;
        met_x[i] += met_vx[i] * dt;
        met_y[i] += met_vy[i] * dt;
        if (met_life[i] >= met_max[i] || met_y[i] < -3.0f) { met_on[i] = 0; continue; }
        if (met_x[i] < 0.0f) met_x[i] += (float)W;
        else if (met_x[i] >= (float)W) met_x[i] -= (float)W;

        float u = met_life[i] / met_max[i];
        float e = met_life[i] * 6.0f;           /* quick flare-in, linear burn-out */
        if (e > 1.0f) e = 1.0f;
        e *= (1.0f - u);
        float b = B * e;

        float sp = __builtin_sqrtf(met_vx[i] * met_vx[i] + met_vy[i] * met_vy[i]);
        float ux = met_vx[i] / sp, uy = met_vy[i] / sp;
        float trail = 3.6f;
        float hx = met_x[i], hy = met_y[i];
        /* three trail segments with falling brightness, then a bright AA head */
        float f[4] = { 0.0f, 0.33f, 0.66f, 1.0f };
        float seg_b[3] = { 0.55f, 0.28f, 0.11f };
        for (int s = 0; s < 3; s++) {
            int c = pack3(b * seg_b[s] * 0.86f, b * seg_b[s] * 0.93f, b * seg_b[s]);
            met_segment(W, H,
                        hx - ux * trail * f[s],     hy - uy * trail * f[s],
                        hx - ux * trail * f[s + 1], hy - uy * trail * f[s + 1], c);
        }
        splat(W, H, hx, hy, pack3(b * 0.9f, b * 0.95f, b));
    }

    /* ---- Moon: slow crescent, drifting with the far sky ---- */
    if (moon_on) {
        if (moon_x < 0.0f) moon_x = random_float() * (float)W;
        moon_x -= drift * 0.22f * dt;
        if (moon_x < 0.0f) moon_x += (float)W;
        float R = 1.6f + (float)((W < H) ? W : H) * 0.03f;
        draw_moon(W, H, moon_x, (float)H * 0.80f, R, B * 0.85f);
    }

    /* ---- Plane: red/green nav lights + white strobe, occasional pass ---- */
    if (plane_on && !pl_on) {
        if (random_float() < dt / 16.0f) {      /* on average every ~16 s */
            pl_on = 1;
            pl_dir = (rng_next() & 1) ? 1.0f : -1.0f;
            pl_x = random_float() * (float)W;
            pl_y = (float)H * (0.55f + random_float() * 0.35f);
            pl_v = 1.6f + random_float() * 1.2f;
            pl_len = (float)W * 1.35f;
            pl_dist = 0.0f;
            pl_strobe = 0.0f;
        }
    }
    if (pl_on) {
        if (!plane_on) pl_on = 0;               /* param switched off mid-flight */
        else {
            pl_x += pl_dir * pl_v * dt;
            if (pl_x < 0.0f) pl_x += (float)W;
            else if (pl_x >= (float)W) pl_x -= (float)W;
            pl_dist += pl_v * dt;
            pl_strobe += dt;
            if (pl_strobe >= 1.3f) pl_strobe -= 1.3f;
            if (pl_dist >= pl_len) pl_on = 0;
            else {
                float e = pl_dist / 1.5f;       /* soft appear/vanish */
                float e2 = (pl_len - pl_dist) / 1.5f;
                if (e2 < e) e = e2;
                if (e > 1.0f) e = 1.0f;
                float lb = B * 0.60f * e;
                /* green leads, red trails (nav lights), faint fuselage dot */
                splat(W, H, pl_x + pl_dir * 0.8f, pl_y, pack3(lb * 0.10f, lb, lb * 0.16f));
                splat(W, H, pl_x - pl_dir * 0.8f, pl_y, pack3(lb, lb * 0.08f, lb * 0.06f));
                splat(W, H, pl_x, pl_y, pack3(lb * 0.14f, lb * 0.14f, lb * 0.15f));
                if (pl_strobe < 0.09f)          /* white strobe blink */
                    splat(W, H, pl_x, pl_y, pack3(B * e, B * e, B * e));
            }
        }
    }

    draw();
}
