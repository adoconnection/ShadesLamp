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

// Export helpers
#define EXPORT(name) __attribute__((export_name(#name)))

#endif
