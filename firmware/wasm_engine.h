#ifndef WASM_ENGINE_H
#define WASM_ENGINE_H

#include <Arduino.h>
#include <wasm3.h>
#include <m3_env.h>

// Forward declarations
class LedDriver;
class ParamStore;

#define WASM_STACK_SIZE 4096

class WasmEngine {
public:
    WasmEngine(LedDriver* ledDriver, ParamStore* paramStore);
    ~WasmEngine();

    // Load a WASM binary, link host functions, call init(), extract metadata
    bool load(const uint8_t* wasmData, size_t wasmSize);

    // Call the WASM update(tick_ms) function
    void tick(int32_t tickMs);

    // Free runtime and module resources
    void unload();

    // Get cached metadata JSON extracted from WASM memory
    String getMetaJson() const;

    // Check if a WASM program is currently loaded
    bool isLoaded() const;

    // Accessors used by host functions
    LedDriver*  getLedDriver()  const { return _ledDriver; }
    ParamStore* getParamStore() const { return _paramStore; }

private:
    bool linkHostFunctions();
    bool extractMeta();

    LedDriver*   _ledDriver;
    ParamStore*  _paramStore;

    IM3Environment _env;
    IM3Runtime     _runtime;
    IM3Module      _module;
    IM3Function    _funcUpdate;

    String _metaJson;
    bool   _loaded;

    SemaphoreHandle_t _mutex;
};

// Global pointer for C-style wasm3 callbacks
extern WasmEngine* g_wasmEngine;

// Validate a WASM binary by attempting to parse and load it.
// Returns true if the binary is a valid WASM module.
bool wasmValidate(const uint8_t* wasmData, size_t wasmSize);

// Extract metadata JSON from raw WASM binary without affecting the active engine.
// Returns "{}" if metadata cannot be extracted.
String wasmExtractMeta(const uint8_t* wasmData, size_t wasmSize);

#endif // WASM_ENGINE_H
