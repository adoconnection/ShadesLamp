#include "wasm_engine.h"
#include "led_driver.h"
#include "param_store.h"

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
    if (g_wasmEngine && g_wasmEngine->getLedDriver()) {
        g_wasmEngine->getLedDriver()->show();
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

// ── WasmEngine class implementation ────────────────────────────────────────

WasmEngine::WasmEngine(LedDriver* ledDriver, ParamStore* paramStore)
    : _ledDriver(ledDriver)
    , _paramStore(paramStore)
    , _env(nullptr)
    , _runtime(nullptr)
    , _module(nullptr)
    , _funcUpdate(nullptr)
    , _loaded(false)
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

    Serial.printf("%s Program unloaded\r\n", TAG);

    xSemaphoreGive(_mutex);
}

String WasmEngine::getMetaJson() const {
    return _metaJson;
}

bool WasmEngine::isLoaded() const {
    return _loaded;
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
