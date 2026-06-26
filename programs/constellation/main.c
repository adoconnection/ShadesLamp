#include "api.h"

/*
 * Constellation — drifting particles that link up with a glowing line when
 * they come within Link Distance of each other; the closer they are, the
 * brighter the line, and well-connected particles shine brighter too.
 * Rendered additively. X wraps around the cylinder (links take the shortest
 * path around it); particles bounce off the top/bottom. Y=0 is the bottom.
 */

static const char META[] =
    "{\"name\":\"Constellation\","
    "\"desc\":\"Drifting particles link with glowing lines when they get close\","
    "\"params\":["
        "{\"id\":0,\"name\":\"Size\",\"type\":\"select\","
         "\"options\":[\"Small\",\"Big\"],\"default\":1,"
         "\"desc\":\"Particle size\"},"
        "{\"id\":1,\"name\":\"Count\",\"type\":\"int\","
         "\"min\":3,\"max\":40,\"default\":18,"
         "\"desc\":\"Number of particles\"},"
        "{\"id\":2,\"name\":\"Brightness\",\"type\":\"int\","
         "\"min\":1,\"max\":255,\"default\":200,"
         "\"desc\":\"Overall brightness\"},"
        "{\"id\":3,\"name\":\"Speed\",\"type\":\"int\","
         "\"min\":1,\"max\":100,\"default\":30,"
         "\"desc\":\"Drift speed\"},"
        "{\"id\":4,\"name\":\"Link Distance\",\"type\":\"int\","
         "\"min\":2,\"max\":24,\"default\":9,"
         "\"desc\":\"Max distance to draw a link\"},"
        "{\"id\":5,\"name\":\"Link Glow\",\"type\":\"int\","
         "\"min\":0,\"max\":100,\"default\":0,"
         "\"desc\":\"0 = subtle, 100 = stars & links at full brightness\"},"
        "{\"id\":6,\"name\":\"Colour\",\"type\":\"select\","
         "\"options\":[\"Ice\",\"Neon\",\"Rainbow\",\"Mono\"],\"default\":0,"
         "\"desc\":\"Colour palette\"}"
    "]}";

EXPORT(get_meta_ptr)
int get_meta_ptr(void) { return (int)META; }
EXPORT(get_meta_len)
int get_meta_len(void) { return sizeof(META) - 1; }

/* ---- PRNG ---- */
static uint32_t rng = 24681;
static uint32_t rng_next(void) { uint32_t x=rng; x^=x<<13; x^=x>>17; x^=x<<5; rng=x; return x; }
static float frand(void) { return (float)(rng_next() & 0xFFFF) / 65536.0f; }

/* ---- math ---- */
#define TWO_PI 6.28318530f
static float fabsf2(float x){ return x<0?-x:x; }

/* ---- HSV ---- */
static void hsv(int h,int s,int v,int*r,int*g,int*b){
    int c=m_hsv(h&0xFF,s,v); *r=(c>>16)&255; *g=(c>>8)&255; *b=c&255;
}

/* ---- state ---- */
#define MAX_P 40
#define MAX_W 64
#define MAX_H 64
static float p_x[MAX_P], p_y[MAX_P], p_vx[MAX_P], p_vy[MAX_P];
static uint8_t p_hue[MAX_P];
static float glow[MAX_P];
static int uf_parent[MAX_P], csize[MAX_P];
static uint8_t acc[MAX_W*MAX_H*3];

/* min stars in a connected group for it to glow white */
#define WHITE_GROUP 4

static int uf_find(int x) {
    while (uf_parent[x] != x) { uf_parent[x] = uf_parent[uf_parent[x]]; x = uf_parent[x]; }
    return x;
}
static void uf_union(int a, int b) {
    int ra = uf_find(a), rb = uf_find(b);
    if (ra != rb) uf_parent[ra] = rb;
}
static int cur_w, cur_h;
static int32_t prev_tick;
static int started;

static void addpix(int x, int y, int r, int g, int b) {
    while (x < 0) x += cur_w; while (x >= cur_w) x -= cur_w;   /* wrap x */
    if (y < 0 || y >= cur_h) return;
    int i = (y*cur_w + x)*3;
    int v;
    v = acc[i]   + r; acc[i]   = v > 255 ? 255 : v;
    v = acc[i+1] + g; acc[i+1] = v > 255 ? 255 : v;
    v = acc[i+2] + b; acc[i+2] = v > 255 ? 255 : v;
}

EXPORT(init)
void init(void) {
    rng = 24681;
    prev_tick = 0;
    started = 0;
}

static void spawn(int i, int W, int H) {
    p_x[i] = frand() * (float)W;
    p_y[i] = frand() * (float)H;
    float a = frand() * TWO_PI;
    p_vx[i] = m_cos(a); p_vy[i] = m_sin(a);
    p_hue[i] = (uint8_t)(rng_next() & 0xFF);
}

/* particle colour for the current palette */
static void particle_color(int palette, int i, int *r, int *g, int *b) {
    switch (palette) {
        case 1: hsv(196 + (p_hue[i] % 46), 220, 255, r, g, b); break;   /* Neon */
        case 2: hsv(p_hue[i], 230, 255, r, g, b); break;                /* Rainbow */
        case 3: *r = *g = *b = 255; break;                              /* Mono */
        default: hsv(128 + (p_hue[i] % 40), 210, 255, r, g, b); break;  /* Ice */
    }
}

EXPORT(update)
void update(int tick_ms) {
    int size    = get_param_i32(0);   /* 0 small, 1 big */
    int count   = get_param_i32(1);
    int bright  = get_param_i32(2);
    int speed   = get_param_i32(3);
    int linkd   = get_param_i32(4);
    int linkglow= get_param_i32(5);
    int palette = get_param_i32(6);
    if (linkglow < 0) linkglow = 0; if (linkglow > 100) linkglow = 100;
    float gboost = (float)linkglow / 100.0f;   /* 0 = as before, 1 = full */

    int W = get_width();
    int H = get_height();
    if (W > MAX_W) W = MAX_W; if (W < 1) W = 1;
    if (H > MAX_H) H = MAX_H; if (H < 1) H = 1;
    if (count > MAX_P) count = MAX_P; if (count < 1) count = 1;
    if (linkd < 1) linkd = 1;
    cur_w = W; cur_h = H;

    rng ^= (uint32_t)tick_ms;

    if (!started) {
        for (int i = 0; i < MAX_P; i++) spawn(i, W, H);
        started = 1;
    }

    int32_t delta = tick_ms - prev_tick;
    if (delta < 0 || delta > 200) delta = 33;
    prev_tick = tick_ms;
    float dt = (float)delta / 1000.0f;

    float pxs = 1.0f + (float)speed / 100.0f * 14.0f;   /* px per second */
    float linkf = (float)linkd;

    /* move particles: wrap horizontally, bounce vertically */
    for (int i = 0; i < count; i++) {
        p_x[i] += p_vx[i] * pxs * dt;
        p_y[i] += p_vy[i] * pxs * dt;
        while (p_x[i] < 0.0f)        p_x[i] += (float)W;
        while (p_x[i] >= (float)W)   p_x[i] -= (float)W;
        if (p_y[i] < 0.0f)            { p_y[i] = 0.0f;            p_vy[i] = -p_vy[i]; }
        if (p_y[i] > (float)(H-1))    { p_y[i] = (float)(H-1);    p_vy[i] = -p_vy[i]; }
        glow[i] = 0.0f;
    }

    /* clear accumulator */
    for (int i = 0; i < W*H*3; i++) acc[i] = 0;

    /* pass 1: union close pairs into groups and feed the glow of both ends */
    for (int i = 0; i < count; i++) uf_parent[i] = i;
    for (int i = 0; i < count; i++) {
        for (int j = i + 1; j < count; j++) {
            float ddx = p_x[j] - p_x[i];
            if (ddx >  (float)W * 0.5f) ddx -= (float)W;
            if (ddx < -(float)W * 0.5f) ddx += (float)W;
            float ddy = p_y[j] - p_y[i];
            if (ddx*ddx + ddy*ddy >= linkf*linkf) continue;
            float strength = 1.0f - m_hypot(ddx, ddy) / linkf;
            glow[i] += strength; glow[j] += strength;
            uf_union(i, j);
        }
    }

    /* group sizes: a group with WHITE_GROUP+ stars glows white */
    for (int i = 0; i < count; i++) csize[i] = 0;
    for (int i = 0; i < count; i++) csize[uf_find(i)]++;

    /* pass 2: draw the links (white when part of a big constellation) */
    for (int i = 0; i < count; i++) {
        int ir, ig, ib; particle_color(palette, i, &ir, &ig, &ib);
        int big = csize[uf_find(i)] >= WHITE_GROUP;
        for (int j = i + 1; j < count; j++) {
            float ddx = p_x[j] - p_x[i];
            if (ddx >  (float)W * 0.5f) ddx -= (float)W;
            if (ddx < -(float)W * 0.5f) ddx += (float)W;
            float ddy = p_y[j] - p_y[i];
            float dist = m_hypot(ddx, ddy);
            if (dist >= linkf) continue;

            float strength = 1.0f - dist / linkf;
            /* Link Glow lifts the line toward full brightness (1.0 at gboost=1) */
            float lstr = strength * strength;
            lstr = lstr + (1.0f - lstr) * gboost;
            float ls = lstr * (float)bright / 255.0f * (0.8f + 0.2f * gboost);
            int lr, lg, lb;
            if (big) {
                lr = lg = lb = (int)(255.0f * ls);
            } else {
                int jr, jg, jb; particle_color(palette, j, &jr, &jg, &jb);
                lr = (int)((ir + jr) * 0.5f * ls);
                lg = (int)((ig + jg) * 0.5f * ls);
                lb = (int)((ib + jb) * 0.5f * ls);
            }
            int steps = (int)(fabsf2(ddx) > fabsf2(ddy) ? fabsf2(ddx) : fabsf2(ddy));
            if (steps < 1) steps = 1;
            for (int k = 0; k <= steps; k++) {
                float t = (float)k / (float)steps;
                addpix((int)(p_x[i] + ddx * t + 0.5f), (int)(p_y[i] + ddy * t + 0.5f), lr, lg, lb);
            }
        }
    }

    /* particles: white & bright when in a 4+ constellation, otherwise palette */
    for (int i = 0; i < count; i++) {
        int big = csize[uf_find(i)] >= WHITE_GROUP;
        int r, g, b;
        if (big) { r = g = b = 255; } else particle_color(palette, i, &r, &g, &b);
        float pb = 0.45f + glow[i] * 0.30f;
        if (pb > 1.0f) pb = 1.0f;
        pb = pb + (1.0f - pb) * gboost;       /* Link Glow lifts stars to full */
        if (big && pb < 0.85f) pb = 0.85f;   /* make the white glow prominent */
        float s = pb * (float)bright / 255.0f;
        int cx = (int)(p_x[i] + 0.5f);
        int cy = (int)(p_y[i] + 0.5f);
        int cr = (int)(r * s), cg = (int)(g * s), cb = (int)(b * s);
        addpix(cx, cy, cr, cg, cb);
        /* big particles get a soft halo; small ones are a single pixel */
        if (size != 0) {
            int hr = cr*2/5, hg = cg*2/5, hb = cb*2/5;
            addpix(cx+1, cy, hr, hg, hb);
            addpix(cx-1, cy, hr, hg, hb);
            addpix(cx, cy+1, hr, hg, hb);
            addpix(cx, cy-1, hr, hg, hb);
        }
    }

    /* blit accumulator to the display */
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            int i = (y*W + x)*3;
            set_pixel(x, y, acc[i], acc[i+1], acc[i+2]);
        }

    draw();
}
