#include "api.h"

/*
 * Morphing Shapes - Smooth transformation between geometric shapes:
 * circle -> square -> triangle -> star -> circle...
 * Drawn on a cylindrical LED matrix with optional fill.
 */

/* ---- Metadata JSON ---- */
static const char META[] =
    "{\"name\":\"Morphing Shapes\","
    "\"desc\":\"Smooth morphing between circle, square, triangle, and star\","
    "\"params\":["
        "{\"id\":0,\"name\":\"Speed\",\"type\":\"int\","
         "\"min\":1,\"max\":100,\"default\":30,"
         "\"desc\":\"Morph transition speed\"},"
        "{\"id\":1,\"name\":\"Brightness\",\"type\":\"int\","
         "\"min\":1,\"max\":255,\"default\":220,"
         "\"desc\":\"Overall brightness\"},"
        "{\"id\":2,\"name\":\"Hue\",\"type\":\"int\","
         "\"min\":0,\"max\":255,\"default\":0,"
         "\"desc\":\"Color hue (0 = rainbow cycle)\"},"
        "{\"id\":3,\"name\":\"Fill\",\"type\":\"int\","
         "\"min\":0,\"max\":1,\"default\":1,"
         "\"options\":[\"Off\",\"On\"],"
         "\"desc\":\"Fill shape interior\"}"
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

static float fabs_f(float x) { return x < 0.0f ? -x : x; }

static int iabs(int x) { return x < 0 ? -x : x; }

static float fsqrt(float x) {
    if (x <= 0.0f) return 0.0f;
    float guess = x * 0.5f;
    guess = 0.5f * (guess + x / guess);
    guess = 0.5f * (guess + x / guess);
    guess = 0.5f * (guess + x / guess);
    guess = 0.5f * (guess + x / guess);
    return guess;
}

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

/* Plot pixel with horizontal wrapping (cylinder) and additive blend */
static void fb_plot(int x, int y, int r, int g, int b) {
    x = ((x % cur_w) + cur_w) % cur_w;
    if (y < 0 || y >= cur_h) return;
    int idx = x * cur_h + y;
    int nr = (int)fb_r[idx] + r;
    int ng = (int)fb_g[idx] + g;
    int nb = (int)fb_b[idx] + b;
    fb_r[idx] = (uint8_t)(nr > 255 ? 255 : nr);
    fb_g[idx] = (uint8_t)(ng > 255 ? 255 : ng);
    fb_b[idx] = (uint8_t)(nb > 255 ? 255 : nb);
}

/* Overwrite pixel (for fill) */
static void fb_set(int x, int y, int r, int g, int b) {
    x = ((x % cur_w) + cur_w) % cur_w;
    if (y < 0 || y >= cur_h) return;
    int idx = x * cur_h + y;
    /* Max blend: keep the brighter of existing or new */
    if (r > (int)fb_r[idx]) fb_r[idx] = (uint8_t)r;
    if (g > (int)fb_g[idx]) fb_g[idx] = (uint8_t)g;
    if (b > (int)fb_b[idx]) fb_b[idx] = (uint8_t)b;
}

/* ---- Bresenham line drawing with cylinder wrap ---- */
static void draw_line(int x0, int y0, int x1, int y1, int r, int g, int b) {
    int dx = x1 - x0;
    int dy = y1 - y0;

    /* Cylinder wrap: choose shorter horizontal path */
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

/* ---- Shape definitions ----
 * Each shape is defined as N_VERTS points along its perimeter.
 * All shapes use the same number of vertices so we can interpolate 1:1.
 * Coordinates are normalized: centered at (0,0), radius ~1.0
 */
#define N_VERTS 64   /* enough points for smooth curves */
#define N_SHAPES 4   /* circle, square, triangle, star */

/* Pre-computed shape vertices: [shape][vertex][x/y] */
static float shape_vx[N_SHAPES][N_VERTS];
static float shape_vy[N_SHAPES][N_VERTS];

/* Interpolated (current) shape vertices */
static float cur_vx[N_VERTS];
static float cur_vy[N_VERTS];

/* Generate circle vertices */
static void gen_circle(int shape_idx) {
    for (int i = 0; i < N_VERTS; i++) {
        float angle = TWO_PI * (float)i / (float)N_VERTS;
        shape_vx[shape_idx][i] = fcos(angle);
        shape_vy[shape_idx][i] = fsin(angle);
    }
}

/* Generate regular polygon with n_sides, distributing N_VERTS evenly along edges */
static void gen_polygon(int shape_idx, int n_sides) {
    /* Compute polygon corner positions */
    float corner_x[8]; /* max 8 corners */
    float corner_y[8];

    /* Start with top vertex (angle = PI/2 for upward orientation) */
    float start_angle = HALF_PI;
    for (int c = 0; c < n_sides; c++) {
        float angle = start_angle + TWO_PI * (float)c / (float)n_sides;
        corner_x[c] = fcos(angle);
        corner_y[c] = fsin(angle);
    }

    /* Distribute N_VERTS evenly along the edges */
    int verts_per_edge = N_VERTS / n_sides;
    int leftover = N_VERTS - verts_per_edge * n_sides;
    int vi = 0;

    for (int c = 0; c < n_sides; c++) {
        int next = (c + 1) % n_sides;
        int n_on_edge = verts_per_edge + (c < leftover ? 1 : 0);
        for (int j = 0; j < n_on_edge; j++) {
            float t = (float)j / (float)n_on_edge;
            shape_vx[shape_idx][vi] = corner_x[c] + t * (corner_x[next] - corner_x[c]);
            shape_vy[shape_idx][vi] = corner_y[c] + t * (corner_y[next] - corner_y[c]);
            vi++;
        }
    }
    /* Safety: fill any remaining (shouldn't happen) */
    while (vi < N_VERTS) {
        shape_vx[shape_idx][vi] = shape_vx[shape_idx][vi - 1];
        shape_vy[shape_idx][vi] = shape_vy[shape_idx][vi - 1];
        vi++;
    }
}

/* Generate star (5-pointed) */
static void gen_star(int shape_idx) {
    /* 5-pointed star: alternating outer and inner vertices (10 corners) */
    int n_points = 5;
    int n_corners = n_points * 2;
    float corner_x[10];
    float corner_y[10];

    float outer_r = 1.0f;
    float inner_r = 0.4f;  /* inner radius for star indentations */
    float start_angle = HALF_PI;

    for (int c = 0; c < n_corners; c++) {
        float angle = start_angle + TWO_PI * (float)c / (float)n_corners;
        float r = (c % 2 == 0) ? outer_r : inner_r;
        corner_x[c] = fcos(angle) * r;
        corner_y[c] = fsin(angle) * r;
    }

    /* Distribute N_VERTS along the star edges */
    int verts_per_edge = N_VERTS / n_corners;
    int leftover = N_VERTS - verts_per_edge * n_corners;
    int vi = 0;

    for (int c = 0; c < n_corners; c++) {
        int next = (c + 1) % n_corners;
        int n_on_edge = verts_per_edge + (c < leftover ? 1 : 0);
        for (int j = 0; j < n_on_edge; j++) {
            float t = (float)j / (float)n_on_edge;
            shape_vx[shape_idx][vi] = corner_x[c] + t * (corner_x[next] - corner_x[c]);
            shape_vy[shape_idx][vi] = corner_y[c] + t * (corner_y[next] - corner_y[c]);
            vi++;
        }
    }
    while (vi < N_VERTS) {
        shape_vx[shape_idx][vi] = shape_vx[shape_idx][vi - 1];
        shape_vy[shape_idx][vi] = shape_vy[shape_idx][vi - 1];
        vi++;
    }
}

static int shapes_initialized = 0;

static void init_shapes(void) {
    if (shapes_initialized) return;
    shapes_initialized = 1;

    gen_circle(0);         /* Shape 0: circle */
    gen_polygon(1, 4);     /* Shape 1: square */
    gen_polygon(2, 3);     /* Shape 2: triangle */
    gen_star(3);           /* Shape 3: star */
}

/* ---- Smooth interpolation (smoothstep) ---- */
static float smoothstep(float t) {
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return t * t * (3.0f - 2.0f * t);
}

/* ---- Scanline fill algorithm ----
 * For each row y, find the min and max x among the shape vertices
 * that fall in that row's range, then fill between them.
 */
static void fill_shape(int n_verts, float cx, float cy, float scale,
                        int fill_r, int fill_g, int fill_b) {
    /* Convert current shape vertices to screen coordinates */
    int screen_x[N_VERTS];
    int screen_y[N_VERTS];
    int min_y = cur_h, max_y = 0;

    for (int i = 0; i < n_verts; i++) {
        screen_x[i] = (int)(cx + cur_vx[i] * scale + 0.5f);
        screen_y[i] = (int)(cy + cur_vy[i] * scale + 0.5f);
        if (screen_y[i] < min_y) min_y = screen_y[i];
        if (screen_y[i] > max_y) max_y = screen_y[i];
    }

    if (min_y < 0) min_y = 0;
    if (max_y >= cur_h) max_y = cur_h - 1;

    /* For each scanline, find crossings with edges and fill between them */
    for (int y = min_y; y <= max_y; y++) {
        /* Find all X intersections with edges at this Y */
        int x_crossings[N_VERTS];
        int n_cross = 0;

        for (int i = 0; i < n_verts; i++) {
            int j = (i + 1) % n_verts;
            int yi = screen_y[i];
            int yj = screen_y[j];

            /* Check if this edge crosses the scanline */
            if ((yi <= y && yj > y) || (yj <= y && yi > y)) {
                /* Compute X intersection */
                float t_edge = (float)(y - yi) / (float)(yj - yi);
                int ix = screen_x[i] + (int)(t_edge * (float)(screen_x[j] - screen_x[i]) + 0.5f);
                if (n_cross < N_VERTS) {
                    x_crossings[n_cross++] = ix;
                }
            }
        }

        /* Simple bubble sort of crossings */
        for (int a = 0; a < n_cross - 1; a++) {
            for (int b_idx = a + 1; b_idx < n_cross; b_idx++) {
                if (x_crossings[b_idx] < x_crossings[a]) {
                    int tmp = x_crossings[a];
                    x_crossings[a] = x_crossings[b_idx];
                    x_crossings[b_idx] = tmp;
                }
            }
        }

        /* Fill between pairs of crossings */
        for (int p = 0; p + 1 < n_cross; p += 2) {
            int x_start = x_crossings[p];
            int x_end = x_crossings[p + 1];
            for (int x = x_start; x <= x_end; x++) {
                /* Gradient: dimmer toward center for a nice effect */
                float dist_from_center = fabs_f((float)x - cx);
                float dist_y = fabs_f((float)y - cy);
                float dist = fsqrt(dist_from_center * dist_from_center + dist_y * dist_y);
                float max_dist = scale;
                if (max_dist < 1.0f) max_dist = 1.0f;
                float intensity = 0.3f + 0.7f * (dist / max_dist);
                if (intensity > 1.0f) intensity = 1.0f;

                int fr = (int)((float)fill_r * intensity);
                int fg = (int)((float)fill_g * intensity);
                int fb = (int)((float)fill_b * intensity);
                fb_set(x, y, fr, fg, fb);
            }
        }
    }
}

EXPORT(init)
void init(void) {
    cur_w = get_width();
    cur_h = get_height();
    if (cur_w > MAX_W) cur_w = MAX_W;
    if (cur_h > MAX_H) cur_h = MAX_H;
    if (cur_w < 1) cur_w = 1;
    if (cur_h < 1) cur_h = 1;
    fb_clear();
    shapes_initialized = 0;
    init_shapes();
}

EXPORT(update)
void update(int tick_ms) {
    int speed  = get_param_i32(0);
    int bright = get_param_i32(1);
    int hue    = get_param_i32(2);
    int do_fill = get_param_i32(3);

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

    init_shapes(); /* ensures shapes are ready */

    /* Time calculation:
     * At default speed (30), one morph transition ~3.5 seconds.
     * Total cycle through 4 shapes = ~14 seconds.
     * transition_ms = how long one shape-to-shape morph takes in ms.
     */
    float speed_factor = (float)speed / 30.0f;
    float transition_ms = 3500.0f / speed_factor;

    /* Total cycle time = N_SHAPES * transition_ms */
    float cycle_ms = (float)N_SHAPES * transition_ms;

    /* Current position in the cycle */
    float t_in_cycle = (float)tick_ms;
    /* Use fmod-like approach */
    while (t_in_cycle >= cycle_ms) t_in_cycle -= cycle_ms;
    while (t_in_cycle < 0.0f) t_in_cycle += cycle_ms;

    /* Which shape transition are we in? */
    int shape_from = (int)(t_in_cycle / transition_ms);
    if (shape_from >= N_SHAPES) shape_from = N_SHAPES - 1;
    int shape_to = (shape_from + 1) % N_SHAPES;

    /* Interpolation parameter within this transition */
    float t_raw = (t_in_cycle - (float)shape_from * transition_ms) / transition_ms;
    if (t_raw < 0.0f) t_raw = 0.0f;
    if (t_raw > 1.0f) t_raw = 1.0f;
    float t_smooth = smoothstep(t_raw);

    /* Interpolate vertices between the two shapes */
    for (int i = 0; i < N_VERTS; i++) {
        cur_vx[i] = shape_vx[shape_from][i] + t_smooth * (shape_vx[shape_to][i] - shape_vx[shape_from][i]);
        cur_vy[i] = shape_vy[shape_from][i] + t_smooth * (shape_vy[shape_to][i] - shape_vy[shape_from][i]);
    }

    /* Screen center and scale */
    float cx = (float)cur_w / 2.0f;
    float cy = (float)cur_h / 2.0f;
    float min_dim = (float)(cur_w < cur_h ? cur_w : cur_h);
    float scale = min_dim * 0.4f; /* shape fills ~80% of the smaller dimension */

    /* Determine color */
    int current_hue;
    if (hue == 0) {
        /* Rainbow cycle: slowly rotate hue over time */
        current_hue = ((int)((float)tick_ms * 0.03f)) & 255;
    } else {
        current_hue = hue;
    }

    /* Clear framebuffer */
    fb_clear();

    /* Fill the shape interior if enabled */
    if (do_fill) {
        int fill_hue = (current_hue + 128) & 255; /* complementary color for fill */
        int fill_bright = bright / 3;  /* dimmer fill */
        int fr, fg, fb;
        hsv_to_rgb(fill_hue, 200, fill_bright, &fr, &fg, &fb);
        fill_shape(N_VERTS, cx, cy, scale, fr, fg, fb);
    }

    /* Draw outline: connect consecutive vertices with Bresenham lines */
    int r, g, b;
    hsv_to_rgb(current_hue, 255, bright, &r, &g, &b);

    for (int i = 0; i < N_VERTS; i++) {
        int next = (i + 1) % N_VERTS;

        int x0 = (int)(cx + cur_vx[i] * scale + 0.5f);
        int y0 = (int)(cy + cur_vy[i] * scale + 0.5f);
        int x1 = (int)(cx + cur_vx[next] * scale + 0.5f);
        int y1 = (int)(cy + cur_vy[next] * scale + 0.5f);

        /* Per-edge hue variation for visual richness */
        int edge_hue = (current_hue + i * 256 / N_VERTS) & 255;
        int er, eg, eb;
        hsv_to_rgb(edge_hue, 255, bright, &er, &eg, &eb);

        draw_line(x0, y0, x1, y1, er, eg, eb);
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
