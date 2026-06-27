#ifndef WASMLED_API_H
#define WASMLED_API_H

// Use wasm-compatible types
typedef unsigned char uint8_t;
typedef int int32_t;
typedef unsigned int uint32_t;
typedef float float32_t;

// Host functions (imported from "env" module)
__attribute__((import_module("env"), import_name("get_width")))
int get_width(void);

__attribute__((import_module("env"), import_name("get_height")))
int get_height(void);

__attribute__((import_module("env"), import_name("set_pixel")))
void set_pixel(int x, int y, int r, int g, int b);

__attribute__((import_module("env"), import_name("draw")))
void draw(void);

__attribute__((import_module("env"), import_name("get_param_i32")))
int get_param_i32(int param_id);

__attribute__((import_module("env"), import_name("get_param_f32")))
float get_param_f32(int param_id);

__attribute__((import_module("env"), import_name("set_param_i32")))
void set_param_i32(int param_id, int value);

// ── Native math primitives ──────────────────────────────────────────────
// Implemented natively by the host (firmware libm / JS Math) — much faster
// than doing the same in interpreted WASM. Angles are in radians.

__attribute__((import_module("env"), import_name("m_sin")))
float m_sin(float x);

__attribute__((import_module("env"), import_name("m_cos")))
float m_cos(float x);

__attribute__((import_module("env"), import_name("m_sqrt")))
float m_sqrt(float x);

// length of vector (x, y) = sqrt(x*x + y*y)
__attribute__((import_module("env"), import_name("m_hypot")))
float m_hypot(float x, float y);

__attribute__((import_module("env"), import_name("m_atan2")))
float m_atan2(float y, float x);

__attribute__((import_module("env"), import_name("m_exp")))
float m_exp(float x);

__attribute__((import_module("env"), import_name("m_pow")))
float m_pow(float base, float exponent);

// HSV (each 0-255) -> packed 0xRRGGBB. Unpack: r=(c>>16)&255, g=(c>>8)&255, b=c&255
__attribute__((import_module("env"), import_name("m_hsv")))
int m_hsv(int h, int s, int v);

// ── Batch operations ────────────────────────────────────────────────────
// Do W*H work in a single native call (the host bounds-checks the whole
// region, so a bad pointer can never escape the program's memory).

// Multiply `len` bytes at buf by keep/256 (trail fade). keep: 0..256.
__attribute__((import_module("env"), import_name("m_fade")))
void m_fade(void* buf, int len, int keep);

// Fill num_pixels RGB triplets at buf with packed color 0xRRGGBB.
__attribute__((import_module("env"), import_name("m_fill")))
void m_fill(void* buf, int num_pixels, int rgb);

// Write a w*h grayscale value-noise (fbm) field into buf (1 byte/cell, 0-255).
// scale/ox/oy are 8.8 fixed-point (256 = one noise cell). octaves: 1..4.
__attribute__((import_module("env"), import_name("m_noise_fill")))
void m_noise_fill(void* buf, int w, int h, int scale, int ox, int oy, int octaves);

// Anti-aliased drawing into an RGB framebuffer (row-major (y*w+x)*3), ADDITIVE
// (saturating). Use these for smooth sub-pixel motion instead of integer
// set_pixel/stamps. `rgb` is packed 0xRRGGBB. Coords are floats. The host
// bounds-checks against the buffer. Pair with get_framebuffer()/draw().

// Splat one anti-aliased point at (fx,fy): a 2x2 bilinear additive blend.
__attribute__((import_module("env"), import_name("m_blend")))
void m_blend(void* buf, int w, int h, float fx, float fy, int rgb);

// Draw an anti-aliased line (Xiaolin Wu) from (x0,y0) to (x1,y1). Does NOT wrap
// across edges; for a cylinder seam, draw with stepped m_blend (wrap x yourself)
// or draw a second copy shifted by ±w.
__attribute__((import_module("env"), import_name("m_line")))
void m_line(void* buf, int w, int h, float x0, float y0, float x1, float y1, int rgb);

// ── Framebuffer fast-path (optional) ─────────────────────────────────────
// Instead of calling set_pixel per pixel, EXPORT a function named
// "get_framebuffer" that returns a pointer to an RGB buffer in your own memory
// (row-major (y*W + x)*3, sized for the largest display you support). Write
// pixels there, then call draw() — the host copies W*H*3 bytes in one go.
//
//   static uint8_t FB[MAX_W * MAX_H * 3];
//   EXPORT(get_framebuffer) int get_framebuffer(void) { return (int)FB; }

// Export helpers
#define EXPORT(name) __attribute__((export_name(#name)))

#endif
