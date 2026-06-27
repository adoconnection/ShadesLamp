#include "api.h"

/*
 * Rotating Cube - Wireframe 3D cube rotating on all three axes,
 * projected onto the 2D cylindrical LED matrix.
 * Uses native anti-aliased line drawing (Xiaolin Wu, m_line) for the edges
 * and m_blend for vertex dots, with cylinder-seam wrapping via shifted copies.
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

/* ---- Framebuffer (interleaved RGB, row-major (y*W + x)*3) ---- */
#define MAX_W 64
#define MAX_H 64

static uint8_t FB[MAX_W * MAX_H * 3];

EXPORT(get_framebuffer)
int get_framebuffer(void) { return (int)FB; }

static int cur_w, cur_h;

/* ---- Draw an AA line with cylinder-seam wrapping ----
 * m_line does NOT wrap, so we draw the primary segment plus copies
 * shifted by +/- W. Off-screen portions are clipped by the host. */
static void draw_wrapped_line(float x0, float y0, float x1, float y1, int rgb) {
    float w = (float)cur_w;
    m_line(FB, cur_w, cur_h, x0,     y0, x1,     y1, rgb);
    m_line(FB, cur_w, cur_h, x0 - w, y0, x1 - w, y1, rgb);
    m_line(FB, cur_w, cur_h, x0 + w, y0, x1 + w, y1, rgb);
}

/* ---- AA point with cylinder-seam wrapping ---- */
static void draw_wrapped_point(float x, float y, int rgb) {
    float w = (float)cur_w;
    m_blend(FB, cur_w, cur_h, x,     y, rgb);
    m_blend(FB, cur_w, cur_h, x - w, y, rgb);
    m_blend(FB, cur_w, cur_h, x + w, y, rgb);
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

/* Transformed (projected) screen coordinates of each vertex (sub-pixel float) */
static float proj_x[8];
static float proj_y[8];
/* Depth values for edge brightness modulation */
static float vert_z[8];

static void clamp_dims(void) {
    cur_w = get_width();
    cur_h = get_height();
    if (cur_w > MAX_W) cur_w = MAX_W;
    if (cur_h > MAX_H) cur_h = MAX_H;
    if (cur_w < 1) cur_w = 1;
    if (cur_h < 1) cur_h = 1;
}

EXPORT(init)
void init(void) {
    clamp_dims();
}

EXPORT(update)
void update(int tick_ms) {
    int speed  = get_param_i32(0);
    int bright = get_param_i32(1);
    int hue    = get_param_i32(2);
    int size   = get_param_i32(3);

    clamp_dims();

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
    float sx = m_sin(angle_x), cx = m_cos(angle_x);
    float sy = m_sin(angle_y), cy = m_cos(angle_y);
    float sz = m_sin(angle_z), cz = m_cos(angle_z);

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

        /* Keep sub-pixel float positions for smooth AA motion */
        proj_x[i] = x3 * persp * scale + center_x;
        proj_y[i] = y3 * persp * scale + center_y;
    }

    /* Clear framebuffer */
    int total = cur_w * cur_h * 3;
    for (int i = 0; i < total; i++) FB[i] = 0;

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
        int rgb = m_hsv(edge_hue, 255, edge_bright);

        /* Pick the shorter path around the cylinder for the horizontal delta */
        float x0 = proj_x[v0];
        float x1 = proj_x[v1];
        float dx = x1 - x0;
        if (dx > cur_w / 2) x1 -= (float)cur_w;
        else if (dx < -cur_w / 2) x1 += (float)cur_w;

        draw_wrapped_line(x0, proj_y[v0], x1, proj_y[v1], rgb);
    }

    /* Draw vertex dots (slightly brighter) for definition */
    for (int i = 0; i < 8; i++) {
        float depth_factor = 0.5f + 0.5f * (vert_z[i] + 1.5f) / 3.0f;
        if (depth_factor > 1.0f) depth_factor = 1.0f;
        int dot_bright = (int)((float)bright * depth_factor);
        if (dot_bright > 255) dot_bright = 255;

        /* Vertices are white-ish (low saturation) */
        int rgb = m_hsv(hue, 120, dot_bright);
        draw_wrapped_point(proj_x[i], proj_y[i], rgb);
    }

    draw();
}
