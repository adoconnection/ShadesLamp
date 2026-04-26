#include "api.h"

/*
 * Rotating Cube - Wireframe 3D cube rotating on all three axes,
 * projected onto the 2D cylindrical LED matrix.
 * Uses Bresenham line drawing with horizontal wrapping.
 */

/* ---- Metadata JSON ---- */
static const char META[] =
    "{\"name\":\"Rotating Cube\","
    "\"desc\":\"Wireframe 3D cube rotating on all axes with perspective projection\","
    "\"params\":["
        "{\"id\":0,\"name\":\"Speed\",\"type\":\"int\","
         "\"min\":1,\"max\":100,\"default\":40,"
         "\"desc\":\"Rotation speed\"},"
        "{\"id\":1,\"name\":\"Brightness\",\"type\":\"int\","
         "\"min\":1,\"max\":255,\"default\":220,"
         "\"desc\":\"Edge brightness\"},"
        "{\"id\":2,\"name\":\"Hue\",\"type\":\"int\","
         "\"min\":0,\"max\":255,\"default\":160,"
         "\"desc\":\"Edge color hue\"},"
        "{\"id\":3,\"name\":\"Size\",\"type\":\"int\","
         "\"min\":10,\"max\":100,\"default\":60,"
         "\"desc\":\"Cube size as percentage of screen\"}"
    "]}";

EXPORT(get_meta_ptr)
int get_meta_ptr(void) { return (int)META; }

EXPORT(get_meta_len)
int get_meta_len(void) { return sizeof(META) - 1; }

/* ---- Math helpers ---- */
#define PI       3.14159265f
#define TWO_PI   6.28318530f
#define HALF_PI  1.57079632f

static float fsin(float x) {
    while (x < 0.0f) x += TWO_PI;
    while (x >= TWO_PI) x -= TWO_PI;
    float sign = 1.0f;
    if (x > PI) { x -= PI; sign = -1.0f; }
    float num = 16.0f * x * (PI - x);
    float den = 5.0f * PI * PI - 4.0f * x * (PI - x);
    if (den == 0.0f) return 0.0f;
    return sign * num / den;
}

static float fcos(float x) { return fsin(x + HALF_PI); }

static int iabs(int x) { return x < 0 ? -x : x; }

/* ---- HSV to RGB ---- */
static void hsv_to_rgb(int h, int s, int v, int *r, int *g, int *b) {
    h = h & 255;
    if (s == 0) { *r = v; *g = v; *b = v; return; }
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

/* ---- Framebuffer ---- */
#define MAX_W 64
#define MAX_H 64

static uint8_t fb_r[MAX_W * MAX_H];
static uint8_t fb_g[MAX_W * MAX_H];
static uint8_t fb_b[MAX_W * MAX_H];

static int cur_w, cur_h;

static void fb_clear(void) {
    int total = cur_w * cur_h;
    for (int i = 0; i < total; i++) {
        fb_r[i] = 0;
        fb_g[i] = 0;
        fb_b[i] = 0;
    }
}

/* Additive blend pixel into framebuffer with horizontal wrapping */
static void fb_plot(int x, int y, int r, int g, int b) {
    /* Horizontal wrap for cylinder */
    x = ((x % cur_w) + cur_w) % cur_w;
    /* Vertical clamp */
    if (y < 0 || y >= cur_h) return;

    int idx = x * cur_h + y;
    int nr = (int)fb_r[idx] + r;
    int ng = (int)fb_g[idx] + g;
    int nb = (int)fb_b[idx] + b;
    fb_r[idx] = (uint8_t)(nr > 255 ? 255 : nr);
    fb_g[idx] = (uint8_t)(ng > 255 ? 255 : ng);
    fb_b[idx] = (uint8_t)(nb > 255 ? 255 : nb);
}

/* ---- Bresenham line drawing with cylinder wrap ---- */
static void draw_line(int x0, int y0, int x1, int y1, int r, int g, int b) {
    int dx = x1 - x0;
    int dy = y1 - y0;

    /* For horizontal wrapping: choose the shorter path around the cylinder */
    if (dx > cur_w / 2) dx -= cur_w;
    else if (dx < -cur_w / 2) dx += cur_w;

    int sx = dx > 0 ? 1 : -1;
    int sy = dy > 0 ? 1 : -1;
    int adx = iabs(dx);
    int ady = iabs(dy);

    int x = x0;
    int y = y0;

    if (adx >= ady) {
        int err = adx / 2;
        for (int i = 0; i <= adx; i++) {
            fb_plot(x, y, r, g, b);
            err -= ady;
            if (err < 0) {
                y += sy;
                err += adx;
            }
            x += sx;
        }
    } else {
        int err = ady / 2;
        for (int i = 0; i <= ady; i++) {
            fb_plot(x, y, r, g, b);
            err -= adx;
            if (err < 0) {
                x += sx;
                err += ady;
            }
            y += sy;
        }
    }
}

/* ---- 3D Cube definition ---- */

/* Unit cube vertices centered at origin: (-1,-1,-1) to (1,1,1) */
static const float cube_verts[8][3] = {
    {-1.0f, -1.0f, -1.0f},
    { 1.0f, -1.0f, -1.0f},
    { 1.0f,  1.0f, -1.0f},
    {-1.0f,  1.0f, -1.0f},
    {-1.0f, -1.0f,  1.0f},
    { 1.0f, -1.0f,  1.0f},
    { 1.0f,  1.0f,  1.0f},
    {-1.0f,  1.0f,  1.0f}
};

/* 12 edges as vertex index pairs */
static const int cube_edges[12][2] = {
    {0,1}, {1,2}, {2,3}, {3,0},  /* back face */
    {4,5}, {5,6}, {6,7}, {7,4},  /* front face */
    {0,4}, {1,5}, {2,6}, {3,7}   /* connecting edges */
};

/* Transformed (projected) screen coordinates of each vertex */
static int proj_x[8];
static int proj_y[8];
/* Depth values for edge brightness modulation */
static float vert_z[8];

EXPORT(init)
void init(void) {
    cur_w = get_width();
    cur_h = get_height();
    if (cur_w > MAX_W) cur_w = MAX_W;
    if (cur_h > MAX_H) cur_h = MAX_H;
    if (cur_w < 1) cur_w = 1;
    if (cur_h < 1) cur_h = 1;
    fb_clear();
}

EXPORT(update)
void update(int tick_ms) {
    int speed  = get_param_i32(0);
    int bright = get_param_i32(1);
    int hue    = get_param_i32(2);
    int size   = get_param_i32(3);

    cur_w = get_width();
    cur_h = get_height();
    if (cur_w > MAX_W) cur_w = MAX_W;
    if (cur_h > MAX_H) cur_h = MAX_H;
    if (cur_w < 1) cur_w = 1;
    if (cur_h < 1) cur_h = 1;

    /* Clamp params */
    if (speed < 1) speed = 1;
    if (speed > 100) speed = 100;
    if (bright < 1) bright = 1;
    if (bright > 255) bright = 255;
    if (size < 10) size = 10;
    if (size > 100) size = 100;

    /* Time-based rotation angles (different speeds per axis for interesting tumble) */
    float t = (float)tick_ms * (float)speed * 0.00003f;
    float angle_x = t * 0.7f;
    float angle_y = t * 1.0f;
    float angle_z = t * 0.5f;

    /* Precompute sin/cos for each rotation axis */
    float sx = fsin(angle_x), cx = fcos(angle_x);
    float sy = fsin(angle_y), cy = fcos(angle_y);
    float sz = fsin(angle_z), cz = fcos(angle_z);

    /* Scale factor based on size param and screen dimensions */
    float min_dim = (float)(cur_w < cur_h ? cur_w : cur_h);
    float scale = min_dim * (float)size / 200.0f;

    /* Screen center */
    float center_x = (float)cur_w / 2.0f;
    float center_y = (float)cur_h / 2.0f;

    /* Perspective parameters */
    float cam_dist = 5.0f; /* Distance from camera to origin (in cube units) */

    /* Transform each vertex */
    for (int i = 0; i < 8; i++) {
        float vx = cube_verts[i][0];
        float vy = cube_verts[i][1];
        float vz = cube_verts[i][2];

        /* Rotate around X axis */
        float y1 = vy * cx - vz * sx;
        float z1 = vy * sx + vz * cx;
        float x1 = vx;

        /* Rotate around Y axis */
        float x2 = x1 * cy + z1 * sy;
        float z2 = -x1 * sy + z1 * cy;
        float y2 = y1;

        /* Rotate around Z axis */
        float x3 = x2 * cz - y2 * sz;
        float y3 = x2 * sz + y2 * cz;
        float z3 = z2;

        /* Store depth for brightness modulation */
        vert_z[i] = z3;

        /* Perspective projection */
        float denom = cam_dist + z3;
        if (denom < 0.5f) denom = 0.5f;
        float persp = cam_dist / denom;

        float px = x3 * persp * scale + center_x;
        float py = y3 * persp * scale + center_y;

        proj_x[i] = (int)(px + 0.5f);
        proj_y[i] = (int)(py + 0.5f);
    }

    /* Clear framebuffer */
    fb_clear();

    /* Draw each edge */
    for (int e = 0; e < 12; e++) {
        int v0 = cube_edges[e][0];
        int v1 = cube_edges[e][1];

        /* Edge brightness based on average depth: closer = brighter */
        float avg_z = (vert_z[v0] + vert_z[v1]) * 0.5f;
        /* z ranges from roughly -1.4 to +1.4. Map to brightness multiplier. */
        /* Front (z > 0) should be brighter, back (z < 0) dimmer */
        float depth_factor = 0.4f + 0.6f * (avg_z + 1.5f) / 3.0f;
        if (depth_factor < 0.2f) depth_factor = 0.2f;
        if (depth_factor > 1.0f) depth_factor = 1.0f;

        int edge_bright = (int)((float)bright * depth_factor);
        if (edge_bright < 1) edge_bright = 1;
        if (edge_bright > 255) edge_bright = 255;

        /* Color: use hue param, with slight variation per edge for visual interest */
        int edge_hue = (hue + e * 5) & 255;
        int r, g, b;
        hsv_to_rgb(edge_hue, 255, edge_bright, &r, &g, &b);

        draw_line(proj_x[v0], proj_y[v0], proj_x[v1], proj_y[v1], r, g, b);
    }

    /* Draw vertex dots (slightly brighter) for definition */
    for (int i = 0; i < 8; i++) {
        float depth_factor = 0.5f + 0.5f * (vert_z[i] + 1.5f) / 3.0f;
        if (depth_factor > 1.0f) depth_factor = 1.0f;
        int dot_bright = (int)((float)bright * depth_factor);
        if (dot_bright > 255) dot_bright = 255;

        /* Vertices are white-ish (low saturation) */
        int r, g, b;
        hsv_to_rgb(hue, 120, dot_bright, &r, &g, &b);
        fb_plot(proj_x[i], proj_y[i], r, g, b);
    }

    /* Render framebuffer to display */
    for (int x = 0; x < cur_w; x++) {
        for (int y = 0; y < cur_h; y++) {
            int idx = x * cur_h + y;
            set_pixel(x, y, fb_r[idx], fb_g[idx], fb_b[idx]);
        }
    }

    draw();
}
