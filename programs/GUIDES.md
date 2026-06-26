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

### Native math primitives (`api.h`)

Heavy math runs **natively** on the host (firmware libm / JS `Math`) — far faster
than the same code interpreted in WASM. Prefer these over hand-rolled sin tables,
Newton sqrt, or inline HSV. Angles are in **radians**.

| Function | Signature | Description |
|----------|-----------|-------------|
| `m_sin(x)` | `float (float)` | Sine |
| `m_cos(x)` | `float (float)` | Cosine |
| `m_sqrt(x)` | `float (float)` | Square root (returns 0 for x ≤ 0) |
| `m_hypot(x, y)` | `float (float, float)` | Vector length `sqrt(x*x+y*y)` |
| `m_atan2(y, x)` | `float (float, float)` | Angle of (x, y), radians |
| `m_exp(x)` | `float (float)` | e^x (glow falloff) |
| `m_pow(base, exp)` | `float (float, float)` | base^exp |
| `m_hsv(h, s, v)` | `int (int, int, int)` | HSV (0-255 each) → packed `0xRRGGBB` |

Unpack `m_hsv`: `r=(c>>16)&255, g=(c>>8)&255, b=c&255`.

> Note: a program that calls `m_*` requires firmware that provides them — flash
> the updated firmware before publishing such a program. Programs that don't
> import them are unaffected. The simulator implements the same contract.

### Batch operations (`api.h`)

Do `W*H` work in a **single** native call (amortizes the host-call boundary,
runs natively). The host bounds-checks the whole region against linear-memory
size, so a bad pointer can never escape your program's memory.

| Function | Signature | Description |
|----------|-----------|-------------|
| `m_fade(buf, len, keep)` | `void (void*, int, int)` | Multiply `len` bytes by `keep/256` (trail fade). `keep` 0–256 |
| `m_fill(buf, num_pixels, rgb)` | `void (void*, int, int)` | Fill RGB triplets with packed `0xRRGGBB` |
| `m_noise_fill(buf, w, h, scale, ox, oy, octaves)` | `void (void*, int, int, int, int, int, int)` | Write a `w*h` grayscale value-noise (fbm) field, 1 byte/cell. `scale/ox/oy` are 8.8 fixed-point (256 = one cell), `octaves` 1–4 |

### Framebuffer fast-path (optional)

Instead of calling `set_pixel` per pixel (one host call each), export a
`get_framebuffer()` that returns a pointer to your own RGB buffer; write pixels
there and call `draw()` — the host copies `W*H*3` bytes in one go.

```c
#define MAX_W 64
#define MAX_H 64
static uint8_t FB[MAX_W * MAX_H * 3];          // row-major (y*W + x)*3, RGB
EXPORT(get_framebuffer) int get_framebuffer(void) { return (int)FB; }

EXPORT(update) void update(int tick_ms) {
    int W = get_width(), H = get_height();
    m_fade(FB, W*H*3, 200);                     // fade the trail
    /* ... write into FB ... */
    draw();                                     // host reads FB (bounds-checked)
}
```

Size `FB` for the **largest** display you support. Programs that don't export
`get_framebuffer` keep using `set_pixel` unchanged (both paths coexist).

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
- Prefer the **native math primitives** (`m_sin`, `m_cos`, `m_sqrt`, `m_hsv`, …) over
  hand-rolled versions — they're faster and run on the host. Implement your own only
  as a fallback for older firmware.
- Static arrays live in WASM linear memory (64KB page), not C stack
- Keep total static data well under 64KB
- No floating-point math in signatures — only `int` and `float`
- Compiled WASM files are typically 1-8KB

## Step 2: Compile

The output **must** be named `main.wasm` — that's the file `index.json` and the
app load. (`tools\build_wasm.bat` is a legacy helper that only builds a few
seed programs and writes `{name}.wasm`; don't rely on it for new programs —
just run clang directly.)

```bash
clang --target=wasm32 -nostdlib -O2 \
  -Wl,--no-entry -Wl,--export-dynamic -Wl,--allow-undefined \
  -I programs/common \
  -o programs/your_program/main.wasm \
  programs/your_program/main.c
```

Requires LLVM/Clang with wasm32 target support.

### On Windows (PowerShell)

PowerShell parses the bare `-Wl,...` linker flags as its own arguments and
fails with *"Missing argument in parameter list"*. Use the `--%` stop-parsing
token so everything after it is passed to clang verbatim:

```powershell
$env:Path = "C:\Program Files\LLVM\bin;$env:Path"
clang --% --target=wasm32 -nostdlib -O2 -I programs\common -Wl,--no-entry -Wl,--export-dynamic -Wl,--allow-undefined -o programs\your_program\main.wasm programs\your_program\main.c
```

A successful build is typically 1–8 KB.

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

## Step 5: Test in the simulator

`simulator.html` (repo root) is a 3D lamp emulator that runs your `.wasm`
exactly like the firmware does (same host functions, switchable lamp sizes).
Serve the repo over HTTP and open it:

```bash
python -m http.server 8777
# then open http://localhost:8777/simulator.html
```

- **Drag-and-drop** your `main.wasm` onto the page, or click *"Открыть .wasm"* —
  works for any file with no extra setup.
- To get your program into the built-in dropdown, add its slug to the `PRESETS`
  array near the bottom of `simulator.html`.
- Use the lamp-size buttons (16×16 … 32×48) to confirm the effect holds up at
  every aspect ratio — `get_width()`/`get_height()` change live each frame.

For headless / automated checks you can instantiate the module directly with
your own host functions and assert on the pixels — e.g. sample per-row
brightness to verify a flame's vertical profile, or measure frame-to-frame
variation to confirm it actually animates.

## Step 6: Commit and Push

```bash
git add programs/your_program/main.c programs/your_program/main.wasm programs/your_program/meta.json programs/index.json
git commit -m "Add YourProgram effect"
git push
```

The mobile app fetches the catalog from the repo, so after `git push` the program becomes available.
