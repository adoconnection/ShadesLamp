#ifndef PROGRAM_MANAGER_H
#define PROGRAM_MANAGER_H

#include <Arduino.h>
#include <vector>

// Forward declarations
class WasmEngine;
class ParamStore;
class LedDriver;

#define MAX_PROGRAMS 128

struct ProgramInfo {
    uint8_t id;
    String  name;
    String  metaJson;
    bool    loaded;     // true if metadata was successfully extracted

    // Cached fields from /meta/{id}.json for fast getProgramListJson()
    String  author;
    String  category;
    String  pulse;
    String  coverJson;  // serialized cover object e.g. {"from":"#...","to":"#...","angle":135}
};

class ProgramManager {
public:
    ProgramManager(WasmEngine* engine, ParamStore* paramStore, LedDriver* ledDriver);

    // Load all programs from storage, read config, activate saved program
    void begin();

    // Switch to a different program by ID
    bool switchProgram(uint8_t id);

    // Upload a new program: validate, save to storage, return new ID or -1
    int8_t uploadProgram(const uint8_t* data, size_t size);

    // Delete a program by ID
    bool deleteProgram(uint8_t id);

    // Get the currently active program ID (255 if none)
    uint8_t getActiveId() const;

    // Get number of stored programs
    uint8_t getProgramCount() const;

    // Get program name by ID
    String getProgramName(uint8_t id) const;

    // Get JSON array of all programs: [{"id":0,"name":"..."},...]
    String getProgramListJson() const;

    // Get full meta.json for a program (from /meta/{id}.json or fallback)
    String getProgramMeta(uint8_t id) const;

    // Set meta.json for a program (saves to /meta/{id}.json)
    bool setProgramMeta(uint8_t id, const String& json);

    // Get params metadata JSON for a program (from WASM meta)
    String getProgramParamsJson(uint8_t id) const;

    // Get current param values JSON for a program
    String getParamValuesJson(uint8_t id) const;

    // Set a parameter value for a program
    bool setParam(uint8_t programId, uint8_t paramId, const uint8_t* value, size_t len);

    // Persist global config (active program, name, hw) to /config.json
    void saveConfig();

    // Persist param values for a specific program to /params/{id}.json
    void saveActiveParams();

    // Deferred save: marks params as dirty, actual write happens after SAVE_DEBOUNCE_MS
    void requestParamSave();

    // Check if a deferred save is pending and enough time has passed; call from loop()
    void flushIfDirty();

    // Request async program switch (returns immediately, processed in processPending())
    void requestSwitch(uint8_t programId);

    // Process pending switch + deferred saves; call from loop()
    void processPending();

    // Device name (persisted in config.json, used for BLE advertising)
    String getDeviceName() const;
    void setDeviceName(const String& name);

    // Hardware config (persisted in config.json, requires reboot to apply)
    uint8_t  getLedPin() const;
    uint16_t getLedWidth() const;
    uint16_t getLedHeight() const;
    bool     getLedZigzag() const;
    void setHardwareConfig(uint8_t pin, uint16_t width, uint16_t height, bool zigzag);

private:
    int findProgramIndex(uint8_t id) const;
    void loadConfig();
    void loadProgramMeta(uint8_t id);
    void ensureMetaLoaded(uint8_t id) const;

    WasmEngine* _engine;
    ParamStore* _paramStore;
    LedDriver*  _ledDriver;

    std::vector<ProgramInfo> _programs;
    uint8_t _activeId;

    String _deviceName;

    uint8_t  _ledPin;
    uint16_t _ledWidth;
    uint16_t _ledHeight;
    bool     _ledZigzag;

    SemaphoreHandle_t _mutex;

    // Deferred param save support
    static const unsigned long SAVE_DEBOUNCE_MS = 3000;
    volatile bool     _paramsDirty;
    unsigned long     _lastParamDirtyTime;

    // Async program switch (set from BLE callback, processed in loop)
    volatile uint8_t  _pendingSwitchId;  // 0xFF = none
};

#endif // PROGRAM_MANAGER_H
