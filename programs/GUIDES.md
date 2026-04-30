# Writing LED Programs (WASM)

Guide for creating new LED programs for the Shades Lamp platform.

## Overview

Each program is a standalone WebAssembly module compiled from C. The firmware runs it via the wasm3 interpreter on an ESP32-S3. Programs render frames by calling host functions (`set_pixel`, `draw`).

## File Structure

```
programs/
  common/
    api.h              # Host function declarations (read-only)
  your_program/
    main.c             # Source code
    main.wasm          # Compiled binary
    meta.json          # Metadata for the app/catalog
  index.json           # Program catalog (must include your entry)
```

## Step 1: Create `main.c`

### Minimal Template

```c
#include "api.h"

/* ---- Metadata JSON (embedded in WASM) ---- */
static const char META[] =
    "{\"name\":\"My Program\","
    "\"desc\":\"Short description of the effect\","
    "\"params\":["
        "{\"id\":0,\"name\":\"Speed\",\"type\":\"int\","
         "\"min\":1,\"max\":100,\"default\":50,"
         "\"desc\":\"Animation speed\"}"
    "]}";

EXPORT(get_meta_ptr)
int get_meta_ptr(void) { return (int)META; }

EXPORT(get_meta_len)
int get_meta_len(void) { return sizeof(META) - 1; }

EXPORT(init)
void init(void) {
    /* Called once when program loads. Initialize state here. */
}

EXPORT(update)
void update(int tick_ms) {
    int speed = get_param_i32(0);
    int W = get_width();
    int H = get_height();

    /* Clear screen */
    for (int x = 0; x < W; x++)
        for (int y = 0; y < H; y++)
            set_pixel(x, y, 0, 0, 0);

    /* ... render your effect ... */

    draw();  /* Flush frame to display */
}
```

### Required Exports

| Function | Signature | Purpose |
|----------|-----------|---------|
| `get_meta_ptr` | `int ()` | Pointer to embedded META JSON |
| `get_meta_len` | `int ()` | Length of META string |
| `init` | `void ()` | One-time initialization |
| `update` | `void (int tick_ms)` | Called every frame |

### Host API (`api.h`)

| Function | Signature | Description |
|----------|-----------|-------------|
| `get_width()` | `int ()` | Display width in pixels |
| `get_height()` | `int ()` | Display height in pixels |
| `set_pixel(x, y, r, g, b)` | `void (int, int, int, int, int)` | Set pixel color (0-255 per channel) |
| `draw()` | `void ()` | Flush frame buffer to LEDs |
| `get_param_i32(id)` | `int (int)` | Read integer parameter by id |
| `get_param_f32(id)` | `float (int)` | Read float parameter by id |
| `set_param_i32(id, val)` | `void (int, int)` | Write parameter value (for linked params) |

## Coordinate System

- **Y=0 is the BOTTOM** of the physical display, Y=H-1 is the top
- "Falling" effects = decreasing Y, "rising" = increasing Y

## Timing: `tick_ms`

`tick_ms` is **total elapsed time** since program start (NOT per-frame delta).

For delta-time physics:
```c
static int32_t prev_tick;

EXPORT(update)
void update(int tick_ms) {
    int32_t delta_ms = tick_ms - prev_tick;
    if (delta_ms <= 0 || delta_ms > 200) delta_ms = 33;
    prev_tick = tick_ms;
    float dt = (float)delta_ms / 1000.0f;
    /* use dt for physics */
}
```

For time-as-phase (sin waves, breathing):
```c
float phase = (float)tick_ms * speed * 0.00008f;
```

## Parameter Types

### `int` — slider

```json
{"id":0, "name":"Speed", "type":"int", "min":1, "max":100, "default":50, "desc":"Animation speed"}
```

Read with `get_param_i32(0)`.

### `select` — dropdown

```json
{"id":0, "name":"Mode", "type":"select",
 "options":["Option A","Option B","Option C"],
 "default":0, "desc":"Choose mode"}
```

Read with `get_param_i32(0)` — returns 0-based index.

## Common Patterns

### PRNG (xorshift32)

```c
static uint32_t rng_state = 12345;

static uint32_t rng_next(void) {
    uint32_t x = rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng_state = x;
    return x;
}

static int random_range(int lo, int hi) {
    if (lo >= hi) return lo;
    return lo + (int)(rng_next() % (uint32_t)(hi - lo));
}

static float random_float(void) {
    return (float)(rng_next() & 0xFFFF) / 65536.0f;
}
```

Seed RNG with tick for variation: `rng_state ^= (uint32_t)tick_ms;` (once in `update`).

### HSV to RGB

```c
/* hue 0-255, sat 0-255, val 0-255 */
static void hsv_to_rgb(int h, int s, int v, int *r, int *g, int *b) {
    if (s == 0) { *r = *g = *b = v; return; }
    h = h & 0xFF;
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
```

### Object Pool (parallel arrays)

For particles, projectiles, flowers, etc. — use static parallel arrays:

```c
#define MAX_OBJECTS 10

static int   obj_phase[MAX_OBJECTS];
static float obj_x[MAX_OBJECTS];
static float obj_y[MAX_OBJECTS];
/* ... more per-object state ... */
```

This avoids dynamic allocation (no `malloc` in WASM).

## Constraints

- **No standard library** (`-nostdlib`): no `malloc`, `printf`, `math.h`, `string.h`
- Implement math functions yourself (`sin`, `cos`, `sqrt`, etc.)
- Static arrays live in WASM linear memory (64KB page), not C stack
- Keep total static data well under 64KB
- No floating-point math in signatures — only `int` and `float`
- Compiled WASM files are typically 1-8KB

## Step 2: Compile

```bash
clang --target=wasm32 -nostdlib -O2 \
  -Wl,--no-entry -Wl,--export-dynamic -Wl,--allow-undefined \
  -I programs/common \
  -o programs/your_program/main.wasm \
  programs/your_program/main.c
```

Requires LLVM/Clang with wasm32 target support.

## Step 3: Create `meta.json`

```json
{
  "guid": "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx",
  "name": "My Program",
  "desc": "Short English description",
  "author": "Your Name",
  "category": "Particles",
  "version": "1.0.0",
  "cover": {"from": "#000000", "to": "#ff0000", "angle": 0},
  "pulse": "#ff0000",
  "tags": ["keyword1", "keyword2"],
  "i18n": {
    "ru": {
      "name": "Моя Программа",
      "desc": "Краткое описание на русском"
    }
  }
}
```

### Fields

| Field | Required | Description |
|-------|----------|-------------|
| `guid` | yes | Unique UUID (generate a new one) |
| `name` | yes | Display name (English) |
| `desc` | yes | Short description (English) |
| `author` | yes | Author name |
| `category` | yes | One of the categories below |
| `version` | yes | Semver (`1.0.0`) |
| `cover` | yes | Gradient for UI card (`from`, `to`, optional `via`, `angle` in degrees) |
| `pulse` | yes | Accent color hex |
| `tags` | yes | Search keywords |
| `i18n` | yes | At least `ru` translation with `name` and `desc` |

### Categories

| Category | Examples |
|----------|----------|
| `Ambient` | breathing, solid_color, twinkling, tricolor |
| `Fire` | candle, flame, lava, lava_lamp |
| `Nature` | flowers, forest |
| `Particles` | fireworks, snow, confetti, bouncing_balls |
| `Streams` | aurora, plasma, rainbow, ocean, matrix_rain |
| `Effects` | matrix_test |

## Step 4: Add to `index.json`

Add your program entry to `programs/index.json` in alphabetical order:

```json
{"slug": "your_program", "meta": "programs/your_program/meta.json", "wasm": "programs/your_program/main.wasm"}
```

## Step 5: Commit and Push

```bash
git add programs/your_program/main.c programs/your_program/main.wasm programs/your_program/meta.json programs/index.json
git commit -m "Add YourProgram effect"
git push
```

The mobile app fetches the catalog from the repo, so after `git push` the program becomes available.
