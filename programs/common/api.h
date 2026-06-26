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

// Export helpers
#define EXPORT(name) __attribute__((export_name(#name)))

#endif
