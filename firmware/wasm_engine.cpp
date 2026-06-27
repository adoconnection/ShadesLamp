#include "wasm_engine.h"
#include "led_driver.h"
#include "param_store.h"
#include <math.h>

#define TAG "[WASM]"

// Global pointer so C-style wasm3 callbacks can reach our instance
WasmEngine* g_wasmEngine = nullptr;

// ── Host function implementations ──────────────────────────────────────────

m3ApiRawFunction(host_get_width) {
    m3ApiReturnType(int32_t);
    if (g_wasmEngine && g_wasmEngine->getLedDriver()) {
        m3ApiReturn((int32_t)g_wasmEngine->getLedDriver()->getWidth());
    }
    m3ApiReturn(0);
}

m3ApiRawFunction(host_get_height) {
    m3ApiReturnType(int32_t);
    if (g_wasmEngine && g_wasmEngine->getLedDriver()) {
        m3ApiReturn((int32_t)g_wasmEngine->getLedDriver()->getHeight());
    }
    m3ApiReturn(0);
}

m3ApiRawFunction(host_set_pixel) {
    m3ApiGetArg(int32_t, x);
    m3ApiGetArg(int32_t, y);
    m3ApiGetArg(int32_t, r);
    m3ApiGetArg(int32_t, g);
    m3ApiGetArg(int32_t, b);

    if (g_wasmEngine && g_wasmEngine->getLedDriver()) {
        g_wasmEngine->getLedDriver()->setPixel(
            (uint16_t)x, (uint16_t)y,
            (uint8_t)r, (uint8_t)g, (uint8_t)b
        );
    }
    m3ApiSuccess();
}

m3ApiRawFunction(host_draw) {
    if (g_wasmEngine) {
        g_wasmEngine->present();   // FB-mode copy (bounds-checked) + show
    }
    m3ApiSuccess();
}

m3ApiRawFunction(host_get_param_i32) {
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, paramId);

    if (g_wasmEngine && g_wasmEngine->getParamStore()) {
        m3ApiReturn(g_wasmEngine->getParamStore()->getInt((uint8_t)paramId));
    }
    m3ApiReturn(0);
}

m3ApiRawFunction(host_get_param_f32) {
    m3ApiReturnType(float);
    m3ApiGetArg(int32_t, paramId);

    if (g_wasmEngine && g_wasmEngine->getParamStore()) {
        m3ApiReturn(g_wasmEngine->getParamStore()->getFloat((uint8_t)paramId));
    }
    m3ApiReturn(0.0f);
}

m3ApiRawFunction(host_set_param_i32) {
    m3ApiGetArg(int32_t, paramId);
    m3ApiGetArg(int32_t, value);

    if (g_wasmEngine && g_wasmEngine->getParamStore()) {
        g_wasmEngine->getParamStore()->setInt((uint8_t)paramId, value);
        g_wasmEngine->flagParamsChanged();
    }
    m3ApiSuccess();
}

// ── Native math primitives ─────────────────────────────────────────────────
// Heavy math runs natively (hardware FPU + libm) instead of interpreted WASM.

m3ApiRawFunction(host_m_sin) {
    m3ApiReturnType(float);
    m3ApiGetArg(float, x);
    m3ApiReturn(sinf(x));
}

m3ApiRawFunction(host_m_cos) {
    m3ApiReturnType(float);
    m3ApiGetArg(float, x);
    m3ApiReturn(cosf(x));
}

m3ApiRawFunction(host_m_sqrt) {
    m3ApiReturnType(float);
    m3ApiGetArg(float, x);
    m3ApiReturn(x > 0.0f ? sqrtf(x) : 0.0f);
}

m3ApiRawFunction(host_m_hypot) {
    m3ApiReturnType(float);
    m3ApiGetArg(float, x);
    m3ApiGetArg(float, y);
    m3ApiReturn(sqrtf(x * x + y * y));
}

m3ApiRawFunction(host_m_atan2) {
    m3ApiReturnType(float);
    m3ApiGetArg(float, y);
    m3ApiGetArg(float, x);
    m3ApiReturn(atan2f(y, x));
}

m3ApiRawFunction(host_m_exp) {
    m3ApiReturnType(float);
    m3ApiGetArg(float, x);
    m3ApiReturn(expf(x));
}

m3ApiRawFunction(host_m_pow) {
    m3ApiReturnType(float);
    m3ApiGetArg(float, base);
    m3ApiGetArg(float, exponent);
    m3ApiReturn(powf(base, exponent));
}

m3ApiRawFunction(host_m_hsv) {
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, h);
    m3ApiGetArg(int32_t, s);
    m3ApiGetArg(int32_t, v);

    if (v < 0) v = 0; if (v > 255) v = 255;
    if (s < 0) s = 0; if (s > 255) s = 255;
    int r, g, b;
    if (s == 0) {
        r = g = b = v;
    } else {
        int hh = h & 0xFF;
        int region = hh / 43;
        int rem = (hh - region * 43) * 6;
        int p = (v * (255 - s)) >> 8;
        int q = (v * (255 - ((s * rem) >> 8))) >> 8;
        int t = (v * (255 - ((s * (255 - rem)) >> 8))) >> 8;
        switch (region) {
            case 0:  r = v; g = t; b = p; break;
            case 1:  r = q; g = v; b = p; break;
            case 2:  r = p; g = v; b = t; break;
            case 3:  r = p; g = q; b = v; break;
            case 4:  r = t; g = p; b = v; break;
            default: r = v; g = p; b = q; break;
        }
    }
    m3ApiReturn((int32_t)((r << 16) | (g << 8) | b));
}

// ── Batch operations over a buffer in the program's linear memory ───────────
// One host call does W*H work natively. The host bounds-checks the whole region
// against linear-memory size, so a bad pointer can never read/write out of bounds.

m3ApiRawFunction(host_m_fade) {
    m3ApiGetArg(int32_t, ptr);
    m3ApiGetArg(int32_t, len);
    m3ApiGetArg(int32_t, keep);     // 0..256, fraction of brightness kept
    uint32_t memSize = 0;
    uint8_t* mem = m3_GetMemory(runtime, &memSize, 0);
    if (mem && ptr >= 0 && len > 0 && (uint32_t)ptr + (uint32_t)len <= memSize) {
        if (keep < 0) keep = 0; if (keep > 256) keep = 256;
        uint8_t* b = mem + ptr;
        for (int32_t i = 0; i < len; i++) b[i] = (uint8_t)(((uint32_t)b[i] * (uint32_t)keep) >> 8);
    }
    m3ApiSuccess();
}

m3ApiRawFunction(host_m_fill) {
    m3ApiGetArg(int32_t, ptr);
    m3ApiGetArg(int32_t, npix);
    m3ApiGetArg(int32_t, rgb);      // packed 0xRRGGBB
    uint32_t memSize = 0;
    uint8_t* mem = m3_GetMemory(runtime, &memSize, 0);
    if (mem && ptr >= 0 && npix > 0 && (uint32_t)ptr + (uint32_t)npix * 3 <= memSize) {
        uint8_t r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, bl = rgb & 0xFF;
        uint8_t* b = mem + ptr;
        for (int32_t i = 0; i < npix; i++) { b[i*3] = r; b[i*3+1] = g; b[i*3+2] = bl; }
    }
    m3ApiSuccess();
}

// ── Value-noise (fbm) — matches the simulator implementation exactly ────────
static int nz_hash2d(int x, int y) {
    int h = x * 374761393 + y * 668265263 + 1274126177;
    h = (h ^ (int)((uint32_t)h >> 13)) * 1274126177;
    return (int)(((uint32_t)h >> 16) & 0xFF);
}
static float nz_value(int fx, int fy) {       /* fx,fy in 8.8 fixed point */
    int ix = fx >> 8, iy = fy >> 8;
    float tx = (float)(fx & 255) / 255.0f;
    float ty = (float)(fy & 255) / 255.0f;
    tx = tx * tx * (3.0f - 2.0f * tx);
    ty = ty * ty * (3.0f - 2.0f * ty);
    float v00 = nz_hash2d(ix, iy),     v10 = nz_hash2d(ix + 1, iy);
    float v01 = nz_hash2d(ix, iy + 1), v11 = nz_hash2d(ix + 1, iy + 1);
    float top = v00 + (v10 - v00) * tx;
    float bot = v01 + (v11 - v01) * tx;
    return top + (bot - top) * ty;            /* 0..255 */
}
static float nz_fbm(int fx, int fy, int octaves) {
    if (octaves < 1) octaves = 1; if (octaves > 4) octaves = 4;
    float sum = 0.0f, amp = 1.0f, norm = 0.0f;
    int freq = 1;
    for (int o = 0; o < octaves; o++) {
        sum += nz_value(fx * freq, fy * freq) * amp;
        norm += amp; amp *= 0.5f; freq *= 2;
    }
    return sum / norm;                        /* 0..255 */
}

m3ApiRawFunction(host_m_noise_fill) {
    m3ApiGetArg(int32_t, ptr);
    m3ApiGetArg(int32_t, w);
    m3ApiGetArg(int32_t, h);
    m3ApiGetArg(int32_t, scale);    // 8.8 fixed step per cell
    m3ApiGetArg(int32_t, ox);       // 8.8 offset (pan/time)
    m3ApiGetArg(int32_t, oy);
    m3ApiGetArg(int32_t, octaves);
    uint32_t memSize = 0;
    uint8_t* mem = m3_GetMemory(runtime, &memSize, 0);
    if (mem && ptr >= 0 && w > 0 && h > 0 &&
        (uint32_t)ptr + (uint32_t)w * (uint32_t)h <= memSize) {
        uint8_t* b = mem + ptr;
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                int v = (int)(nz_fbm(x * scale + ox, y * scale + oy, octaves) + 0.5f);
                if (v < 0) v = 0; if (v > 255) v = 255;
                b[y * w + x] = (uint8_t)v;
            }
        }
    }
    m3ApiSuccess();
}

// ── Anti-aliased drawing into the program's RGB framebuffer (additive) ──────
// Additive saturating blend of one pixel scaled by coverage c (0..1).
static inline void aa_add(uint8_t* buf, uint32_t bufLen, int w, int h,
                          int x, int y, int r, int g, int b, float c) {
    if (c <= 0.0f || x < 0 || y < 0 || x >= w || y >= h) return;
    uint32_t idx = ((uint32_t)y * (uint32_t)w + (uint32_t)x) * 3u;
    if (idx + 3u > bufLen) return;
    int v;
    v = buf[idx]   + (int)(r * c + 0.5f); buf[idx]   = v > 255 ? 255 : v;
    v = buf[idx+1] + (int)(g * c + 0.5f); buf[idx+1] = v > 255 ? 255 : v;
    v = buf[idx+2] + (int)(b * c + 0.5f); buf[idx+2] = v > 255 ? 255 : v;
}

m3ApiRawFunction(host_m_blend) {
    m3ApiGetArg(int32_t, ptr);
    m3ApiGetArg(int32_t, w);
    m3ApiGetArg(int32_t, h);
    m3ApiGetArg(float,   fx);
    m3ApiGetArg(float,   fy);
    m3ApiGetArg(int32_t, rgb);
    uint32_t memSize = 0;
    uint8_t* mem = m3_GetMemory(runtime, &memSize, 0);
    if (mem && ptr >= 0 && (uint32_t)ptr <= memSize && w > 0 && h > 0) {
        uint8_t* buf = mem + ptr; uint32_t bufLen = memSize - (uint32_t)ptr;
        int r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
        float ffx = floorf(fx), ffy = floorf(fy);
        int ix = (int)ffx, iy = (int)ffy;
        float dx = fx - ffx, dy = fy - ffy;
        aa_add(buf, bufLen, w, h, ix,   iy,   r, g, b, (1.0f-dx)*(1.0f-dy));
        aa_add(buf, bufLen, w, h, ix+1, iy,   r, g, b, dx*(1.0f-dy));
        aa_add(buf, bufLen, w, h, ix,   iy+1, r, g, b, (1.0f-dx)*dy);
        aa_add(buf, bufLen, w, h, ix+1, iy+1, r, g, b, dx*dy);
    }
    m3ApiSuccess();
}

m3ApiRawFunction(host_m_line) {
    m3ApiGetArg(int32_t, ptr);
    m3ApiGetArg(int32_t, w);
    m3ApiGetArg(int32_t, h);
    m3ApiGetArg(float,   x0);
    m3ApiGetArg(float,   y0);
    m3ApiGetArg(float,   x1);
    m3ApiGetArg(float,   y1);
    m3ApiGetArg(int32_t, rgb);
    uint32_t memSize = 0;
    uint8_t* mem = m3_GetMemory(runtime, &memSize, 0);
    if (!(mem && ptr >= 0 && (uint32_t)ptr <= memSize && w > 0 && h > 0)) m3ApiSuccess();
    uint8_t* buf = mem + ptr; uint32_t bufLen = memSize - (uint32_t)ptr;
    int r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;

    int steep = fabsf(y1 - y0) > fabsf(x1 - x0);
    if (steep) { float t; t=x0;x0=y0;y0=t; t=x1;x1=y1;y1=t; }
    if (x0 > x1) { float t; t=x0;x0=x1;x1=t; t=y0;y0=y1;y1=t; }
    float dx = x1 - x0, dy = y1 - y0;
    float grad = (dx == 0.0f) ? 1.0f : dy / dx;

    /* endpoint 1 */
    float xend = floorf(x0 + 0.5f);
    float yend = y0 + grad * (xend - x0);
    float xgap = 1.0f - ((x0 + 0.5f) - floorf(x0 + 0.5f));
    int xp1 = (int)xend, yp1 = (int)floorf(yend);
    float fyy = yend - floorf(yend);
    if (steep) { aa_add(buf,bufLen,w,h, yp1,   xp1, r,g,b, (1.0f-fyy)*xgap);
                 aa_add(buf,bufLen,w,h, yp1+1, xp1, r,g,b, fyy*xgap); }
    else       { aa_add(buf,bufLen,w,h, xp1, yp1,   r,g,b, (1.0f-fyy)*xgap);
                 aa_add(buf,bufLen,w,h, xp1, yp1+1, r,g,b, fyy*xgap); }
    float intery = yend + grad;

    /* endpoint 2 */
    xend = floorf(x1 + 0.5f);
    yend = y1 + grad * (xend - x1);
    xgap = (x1 + 0.5f) - floorf(x1 + 0.5f);
    int xp2 = (int)xend, yp2 = (int)floorf(yend);
    fyy = yend - floorf(yend);
    if (steep) { aa_add(buf,bufLen,w,h, yp2,   xp2, r,g,b, (1.0f-fyy)*xgap);
                 aa_add(buf,bufLen,w,h, yp2+1, xp2, r,g,b, fyy*xgap); }
    else       { aa_add(buf,bufLen,w,h, xp2, yp2,   r,g,b, (1.0f-fyy)*xgap);
                 aa_add(buf,bufLen,w,h, xp2, yp2+1, r,g,b, fyy*xgap); }

    /* main span */
    for (int x = xp1 + 1; x < xp2; x++) {
        int iy = (int)floorf(intery);
        float f = intery - floorf(intery);
        if (steep) { aa_add(buf,bufLen,w,h, iy,   x, r,g,b, 1.0f-f);
                     aa_add(buf,bufLen,w,h, iy+1, x, r,g,b, f); }
        else       { aa_add(buf,bufLen,w,h, x, iy,   r,g,b, 1.0f-f);
                     aa_add(buf,bufLen,w,h, x, iy+1, r,g,b, f); }
        intery += grad;
    }
    m3ApiSuccess();
}

// ── WasmEngine class implementation ────────────────────────────────────────

WasmEngine::WasmEngine(LedDriver* ledDriver, ParamStore* paramStore)
    : _ledDriver(ledDriver)
    , _paramStore(paramStore)
    , _env(nullptr)
    , _runtime(nullptr)
    , _module(nullptr)
    , _funcUpdate(nullptr)
    , _loaded(false)
    , _paramsChanged(false)
    , _fbMode(false)
    , _fbPtr(0)
{
    _mutex = xSemaphoreCreateMutex();
    g_wasmEngine = this;
}

WasmEngine::~WasmEngine() {
    unload();
    if (_mutex) vSemaphoreDelete(_mutex);
    g_wasmEngine = nullptr;
}

bool WasmEngine::load(const uint8_t* wasmData, size_t wasmSize) {
    // Unload any previous program (before taking the mutex, since unload takes it too)
    if (_loaded) {
        unload();
    }

    xSemaphoreTake(_mutex, portMAX_DELAY);

    Serial.printf("%s Loading WASM (%u bytes)\r\n", TAG, wasmSize);

    // Create environment
    _env = m3_NewEnvironment();
    if (!_env) {
        Serial.printf("%s Failed to create environment\r\n", TAG);
        xSemaphoreGive(_mutex);
        return false;
    }

    // Create runtime with stack allocated in default memory
    _runtime = m3_NewRuntime(_env, WASM_STACK_SIZE, NULL);
    if (!_runtime) {
        Serial.printf("%s Failed to create runtime\r\n", TAG);
        m3_FreeEnvironment(_env);
        _env = nullptr;
        xSemaphoreGive(_mutex);
        return false;
    }

    // Parse the WASM module
    M3Result result = m3_ParseModule(_env, &_module, wasmData, wasmSize);
    if (result) {
        Serial.printf("%s Parse error: %s\r\n", TAG, result);
        m3_FreeRuntime(_runtime);
        m3_FreeEnvironment(_env);
        _runtime = nullptr;
        _env = nullptr;
        xSemaphoreGive(_mutex);
        return false;
    }

    // Load module into runtime
    result = m3_LoadModule(_runtime, _module);
    if (result) {
        Serial.printf("%s Load error: %s\r\n", TAG, result);
        // Module is freed by runtime if load fails after partial load,
        // but if it fails completely we need to free it
        m3_FreeModule(_module);
        _module = nullptr;
        m3_FreeRuntime(_runtime);
        m3_FreeEnvironment(_env);
        _runtime = nullptr;
        _env = nullptr;
        xSemaphoreGive(_mutex);
        return false;
    }

    // Link host functions
    if (!linkHostFunctions()) {
        Serial.printf("%s Failed to link host functions\r\n", TAG);
        // Module is owned by runtime after successful load
        _module = nullptr;
        m3_FreeRuntime(_runtime);
        m3_FreeEnvironment(_env);
        _runtime = nullptr;
        _env = nullptr;
        xSemaphoreGive(_mutex);
        return false;
    }

    // Extract metadata before calling init
    extractMeta();

    // Find and call init()
    IM3Function funcInit;
    result = m3_FindFunction(&funcInit, _runtime, "init");
    if (result) {
        Serial.printf("%s No init() function: %s\r\n", TAG, result);
        // init is optional; continue without it
    } else {
        result = m3_CallV(funcInit);
        if (result) {
            Serial.printf("%s init() call failed: %s\r\n", TAG, result);
        }
    }

    // Find update function
    result = m3_FindFunction(&_funcUpdate, _runtime, "update");
    if (result) {
        Serial.printf("%s No update() function: %s\r\n", TAG, result);
        _funcUpdate = nullptr;
    }

    // Detect optional framebuffer fast-path export
    detectFramebuffer();

    _loaded = true;
    Serial.printf("%s Program loaded successfully\r\n", TAG);

    xSemaphoreGive(_mutex);
    return true;
}

void WasmEngine::tick(int32_t tickMs) {
    if (!_loaded || !_funcUpdate) return;

    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(10)) != pdTRUE) {
        return; // Skip this frame if we can't acquire the lock quickly
    }

    // Re-check after acquiring mutex: unload() on another core may have
    // cleared these between the pre-check above and the mutex acquisition.
    if (!_loaded || !_funcUpdate) {
        xSemaphoreGive(_mutex);
        return;
    }

    M3Result result = m3_CallV(_funcUpdate, tickMs);
    if (result) {
        Serial.printf("%s update() error: %s\r\n", TAG, result);
    }

    xSemaphoreGive(_mutex);
}

void WasmEngine::unload() {
    xSemaphoreTake(_mutex, portMAX_DELAY);

    if (_runtime) {
        // Module is owned by runtime after m3_LoadModule, freed with runtime
        m3_FreeRuntime(_runtime);
        _runtime = nullptr;
        _module = nullptr;
    }

    if (_env) {
        m3_FreeEnvironment(_env);
        _env = nullptr;
    }

    _funcUpdate = nullptr;
    _metaJson = "";
    _loaded = false;
    _fbMode = false;
    _fbPtr = 0;

    Serial.printf("%s Program unloaded\r\n", TAG);

    xSemaphoreGive(_mutex);
}

void WasmEngine::detectFramebuffer() {
    _fbMode = false;
    _fbPtr = 0;
    IM3Function f;
    if (m3_FindFunction(&f, _runtime, "get_framebuffer")) return;  // not exported
    if (m3_CallV(f)) return;
    int32_t ptr = 0;
    m3_GetResultsV(f, &ptr);
    if (ptr < 0) return;
    _fbPtr = ptr;
    _fbMode = true;
    Serial.printf("%s Framebuffer mode (ptr=%d)\r\n", TAG, ptr);
}

void WasmEngine::present() {
    if (!_ledDriver) return;
    if (_fbMode && _runtime) {
        uint32_t memSize = 0;
        uint8_t* mem = m3_GetMemory(_runtime, &memSize, 0);
        uint32_t need = _ledDriver->bufferBytes();
        if (mem && _fbPtr >= 0 && (uint32_t)_fbPtr + need <= memSize) {
            _ledDriver->commit(mem + _fbPtr);
        }
        // invalid pointer -> keep previous frame (no copy)
    }
    _ledDriver->show();
}

String WasmEngine::getMetaJson() const {
    return _metaJson;
}

bool WasmEngine::isLoaded() const {
    return _loaded;
}

bool WasmEngine::consumeParamsChanged() {
    if (_paramsChanged) {
        _paramsChanged = false;
        return true;
    }
    return false;
}

bool WasmEngine::linkHostFunctions() {
    M3Result result;

    result = m3_LinkRawFunction(_module, "env", "get_width", "i()", &host_get_width);
    if (result && result != m3Err_functionLookupFailed) {
        Serial.printf("%s Link get_width: %s\r\n", TAG, result);
    }

    result = m3_LinkRawFunction(_module, "env", "get_height", "i()", &host_get_height);
    if (result && result != m3Err_functionLookupFailed) {
        Serial.printf("%s Link get_height: %s\r\n", TAG, result);
    }

    result = m3_LinkRawFunction(_module, "env", "set_pixel", "v(iiiii)", &host_set_pixel);
    if (result && result != m3Err_functionLookupFailed) {
        Serial.printf("%s Link set_pixel: %s\r\n", TAG, result);
    }

    result = m3_LinkRawFunction(_module, "env", "draw", "v()", &host_draw);
    if (result && result != m3Err_functionLookupFailed) {
        Serial.printf("%s Link draw: %s\r\n", TAG, result);
    }

    result = m3_LinkRawFunction(_module, "env", "get_param_i32", "i(i)", &host_get_param_i32);
    if (result && result != m3Err_functionLookupFailed) {
        Serial.printf("%s Link get_param_i32: %s\r\n", TAG, result);
    }

    result = m3_LinkRawFunction(_module, "env", "get_param_f32", "f(i)", &host_get_param_f32);
    if (result && result != m3Err_functionLookupFailed) {
        Serial.printf("%s Link get_param_f32: %s\r\n", TAG, result);
    }

    result = m3_LinkRawFunction(_module, "env", "set_param_i32", "v(ii)", &host_set_param_i32);
    if (result && result != m3Err_functionLookupFailed) {
        Serial.printf("%s Link set_param_i32: %s\r\n", TAG, result);
    }

    // Native math primitives (links silently skipped if not imported)
    m3_LinkRawFunction(_module, "env", "m_sin",   "f(f)",   &host_m_sin);
    m3_LinkRawFunction(_module, "env", "m_cos",   "f(f)",   &host_m_cos);
    m3_LinkRawFunction(_module, "env", "m_sqrt",  "f(f)",   &host_m_sqrt);
    m3_LinkRawFunction(_module, "env", "m_hypot", "f(ff)",  &host_m_hypot);
    m3_LinkRawFunction(_module, "env", "m_atan2", "f(ff)",  &host_m_atan2);
    m3_LinkRawFunction(_module, "env", "m_exp",   "f(f)",   &host_m_exp);
    m3_LinkRawFunction(_module, "env", "m_pow",   "f(ff)",  &host_m_pow);
    m3_LinkRawFunction(_module, "env", "m_hsv",   "i(iii)", &host_m_hsv);

    // Batch operations over a memory buffer
    m3_LinkRawFunction(_module, "env", "m_fade",       "v(iii)",     &host_m_fade);
    m3_LinkRawFunction(_module, "env", "m_fill",       "v(iii)",     &host_m_fill);
    m3_LinkRawFunction(_module, "env", "m_noise_fill", "v(iiiiiii)", &host_m_noise_fill);
    m3_LinkRawFunction(_module, "env", "m_blend",      "v(iiiffi)",  &host_m_blend);
    m3_LinkRawFunction(_module, "env", "m_line",       "v(iiiffffi)",&host_m_line);

    // Not a hard failure if individual links fail (function may not be imported)
    return true;
}

bool WasmEngine::extractMeta() {
    _metaJson = "{}";

    // Find get_meta_ptr and get_meta_len exports
    IM3Function funcPtr, funcLen;
    M3Result result;

    result = m3_FindFunction(&funcPtr, _runtime, "get_meta_ptr");
    if (result) {
        Serial.printf("%s No get_meta_ptr(): %s\r\n", TAG, result);
        return false;
    }

    result = m3_FindFunction(&funcLen, _runtime, "get_meta_len");
    if (result) {
        Serial.printf("%s No get_meta_len(): %s\r\n", TAG, result);
        return false;
    }

    // Call get_meta_ptr
    result = m3_CallV(funcPtr);
    if (result) {
        Serial.printf("%s get_meta_ptr() failed: %s\r\n", TAG, result);
        return false;
    }
    int32_t metaPtr = 0;
    m3_GetResultsV(funcPtr, &metaPtr);

    // Call get_meta_len
    result = m3_CallV(funcLen);
    if (result) {
        Serial.printf("%s get_meta_len() failed: %s\r\n", TAG, result);
        return false;
    }
    int32_t metaLen = 0;
    m3_GetResultsV(funcLen, &metaLen);

    if (metaLen <= 0 || metaLen > 4096) {
        Serial.printf("%s Invalid meta length: %d\r\n", TAG, metaLen);
        return false;
    }

    // Read from WASM linear memory
    uint32_t memSize = 0;
    uint8_t* mem = m3_GetMemory(_runtime, &memSize, 0);
    if (!mem) {
        Serial.printf("%s Cannot access WASM memory\r\n", TAG);
        return false;
    }

    if ((uint32_t)metaPtr + (uint32_t)metaLen > memSize) {
        Serial.printf("%s Meta out of bounds: ptr=%d len=%d memSize=%u\r\n",
                      TAG, metaPtr, metaLen, memSize);
        return false;
    }

    // Copy meta string from WASM memory
    char* metaBuf = (char*)malloc(metaLen + 1);
    if (!metaBuf) return false;

    memcpy(metaBuf, mem + metaPtr, metaLen);
    metaBuf[metaLen] = '\0';

    _metaJson = String(metaBuf);
    free(metaBuf);

    Serial.printf("%s Meta extracted (%d bytes)\r\n", TAG, metaLen);
    return true;
}

// ── Standalone metadata extraction ─────────────────────────────────────────

static void linkAllHostFunctions(IM3Module mod) {
    m3_LinkRawFunction(mod, "env", "get_width",     "i()",     &host_get_width);
    m3_LinkRawFunction(mod, "env", "get_height",    "i()",     &host_get_height);
    m3_LinkRawFunction(mod, "env", "set_pixel",     "v(iiiii)",&host_set_pixel);
    m3_LinkRawFunction(mod, "env", "draw",          "v()",     &host_draw);
    m3_LinkRawFunction(mod, "env", "get_param_i32", "i(i)",    &host_get_param_i32);
    m3_LinkRawFunction(mod, "env", "get_param_f32", "f(i)",    &host_get_param_f32);
    m3_LinkRawFunction(mod, "env", "set_param_i32", "v(ii)",   &host_set_param_i32);
    m3_LinkRawFunction(mod, "env", "m_sin",   "f(f)",   &host_m_sin);
    m3_LinkRawFunction(mod, "env", "m_cos",   "f(f)",   &host_m_cos);
    m3_LinkRawFunction(mod, "env", "m_sqrt",  "f(f)",   &host_m_sqrt);
    m3_LinkRawFunction(mod, "env", "m_hypot", "f(ff)",  &host_m_hypot);
    m3_LinkRawFunction(mod, "env", "m_atan2", "f(ff)",  &host_m_atan2);
    m3_LinkRawFunction(mod, "env", "m_exp",   "f(f)",   &host_m_exp);
    m3_LinkRawFunction(mod, "env", "m_pow",   "f(ff)",  &host_m_pow);
    m3_LinkRawFunction(mod, "env", "m_hsv",   "i(iii)", &host_m_hsv);
    m3_LinkRawFunction(mod, "env", "m_fade",       "v(iii)",     &host_m_fade);
    m3_LinkRawFunction(mod, "env", "m_fill",       "v(iii)",     &host_m_fill);
    m3_LinkRawFunction(mod, "env", "m_noise_fill", "v(iiiiiii)", &host_m_noise_fill);
    m3_LinkRawFunction(mod, "env", "m_blend",      "v(iiiffi)",  &host_m_blend);
    m3_LinkRawFunction(mod, "env", "m_line",       "v(iiiffffi)",&host_m_line);
}

bool wasmValidate(const uint8_t* wasmData, size_t wasmSize) {
    IM3Environment env = m3_NewEnvironment();
    if (!env) return false;

    IM3Runtime runtime = m3_NewRuntime(env, WASM_STACK_SIZE, NULL);
    if (!runtime) {
        m3_FreeEnvironment(env);
        return false;
    }

    IM3Module module;
    M3Result result = m3_ParseModule(env, &module, wasmData, wasmSize);
    if (result) {
        Serial.printf("[WASM] Validate parse error: %s\r\n", result);
        m3_FreeRuntime(runtime);
        m3_FreeEnvironment(env);
        return false;
    }

    result = m3_LoadModule(runtime, module);
    if (result) {
        Serial.printf("[WASM] Validate load error: %s\r\n", result);
        m3_FreeModule(module);
        m3_FreeRuntime(runtime);
        m3_FreeEnvironment(env);
        return false;
    }

    m3_FreeRuntime(runtime);
    m3_FreeEnvironment(env);
    return true;
}

String wasmExtractMeta(const uint8_t* wasmData, size_t wasmSize) {
    String metaJson = "{}";

    IM3Environment env = m3_NewEnvironment();
    if (!env) return metaJson;

    IM3Runtime runtime = m3_NewRuntime(env, WASM_STACK_SIZE, NULL);
    if (!runtime) {
        m3_FreeEnvironment(env);
        return metaJson;
    }

    IM3Module module;
    M3Result result = m3_ParseModule(env, &module, wasmData, wasmSize);
    if (result) {
        m3_FreeRuntime(runtime);
        m3_FreeEnvironment(env);
        return metaJson;
    }

    result = m3_LoadModule(runtime, module);
    if (result) {
        m3_FreeModule(module);
        m3_FreeRuntime(runtime);
        m3_FreeEnvironment(env);
        return metaJson;
    }

    // Link host functions so compilation succeeds
    linkAllHostFunctions(module);

    // Find and call get_meta_ptr / get_meta_len
    IM3Function funcPtr, funcLen;
    if (!m3_FindFunction(&funcPtr, runtime, "get_meta_ptr") &&
        !m3_FindFunction(&funcLen, runtime, "get_meta_len")) {

        if (!m3_CallV(funcPtr)) {
            int32_t metaPtr = 0;
            m3_GetResultsV(funcPtr, &metaPtr);

            if (!m3_CallV(funcLen)) {
                int32_t metaLen = 0;
                m3_GetResultsV(funcLen, &metaLen);

                if (metaLen > 0 && metaLen <= 4096) {
                    uint32_t memSize = 0;
                    uint8_t* mem = m3_GetMemory(runtime, &memSize, 0);
                    if (mem && (uint32_t)metaPtr + (uint32_t)metaLen <= memSize) {
                        char* buf = (char*)malloc(metaLen + 1);
                        if (buf) {
                            memcpy(buf, mem + metaPtr, metaLen);
                            buf[metaLen] = '\0';
                            metaJson = String(buf);
                            free(buf);
                        }
                    }
                }
            }
        }
    }

    m3_FreeRuntime(runtime);
    m3_FreeEnvironment(env);
    return metaJson;
}
