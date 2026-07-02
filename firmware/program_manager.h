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
    String  guid;
    String  slug;       // stable program slug (from meta.json); fallback identity
    String  version;
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

    // True if a program with this device id is currently installed.
    bool hasProgram(uint8_t id) const;

    // Stable program guid (from meta.json) for a device id, or "" if unknown.
    String programGuid(uint8_t id) const;

    // Resolve a stable program guid to its current device id, or -1 if no
    // installed program carries that guid. Used by playlists so a saved
    // position survives delete/update/re-download (the numeric id may change,
    // the guid does not).
    int resolveGuid(const String& guid) const;

    // Resolve a program slug to its current device id, or -1 if none installed.
    // Slug is the reliable stored identity for legacy positions whose numeric
    // `prog` slot has since drifted to a different program.
    int resolveSlug(const String& slug) const;

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

    // Program display order
    String getOrderJson() const;
    bool setOrder(const String& json);

    // IDs in display order (custom order first, then any unordered programs).
    // Used by hardware controls to navigate next/previous.
    std::vector<uint8_t> getOrderedIds() const;

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

    // Debounced persist of the active program (for power-on resume). A manual
    // program switch calls this instead of saving config.json on every switch;
    // the write happens RESUME_DEBOUNCE_MS after the last user switch.
    void requestConfigSave();

    // Request async program switch that, after loading, overlays the given param
    // values IN MEMORY ONLY (paramsJson = [{"id","value","f"}, ...]). Used by the
    // playlist rotation engine: a position must not rewrite the program's stored
    // /params/{id}.json, and must not re-save config.json on every rotation.
    void requestSwitchTransient(uint8_t programId, const String& paramsJson);

    // True if a program switch is queued but not yet applied (for crossfade timing)
    bool hasPendingSwitch() const { return _pendingSwitchId != 0xFF; }

    // Request async program delete (returns immediately, processed in processPending())
    void requestDelete(uint8_t programId);

    // Request async wipe of ALL programs (wasm + meta + params). Processed in
    // processPending() on the render task. Device config (name/hw) is kept.
    void requestClearAll();

    // Process pending delete/wipe + deferred saves; call from loop().
    // applySwitch=false leaves a queued program switch untouched so the
    // caller's crossfade machine can pick it up on the next frame instead of
    // applying it instantly (no fade).
    void processPending(bool applySwitch = true);

    // Device name (persisted in config.json, used for BLE advertising)
    String getDeviceName() const;
    void setDeviceName(const String& name);

    // Hardware config (persisted in config.json, requires reboot to apply)
    uint8_t  getLedPin() const;
    uint16_t getLedWidth() const;
    uint16_t getLedHeight() const;
    bool     getLedZigzag() const;
    uint8_t  getLedColorOrder() const;
    void setHardwareConfig(uint8_t pin, uint16_t width, uint16_t height, bool zigzag, uint8_t colorOrder);

private:
    int findProgramIndex(uint8_t id) const;
    // Overlay param values into the active ParamStore in memory only (no flash
    // write, no deferred save). For transient playlist-position application.
    void applyTransientParams(const String& paramsJson);
    void clearAllPrograms();   // actual wipe; runs on render task via processPending()
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
    uint8_t  _ledColorOrder;

    std::vector<uint8_t> _order;  // custom display order (program IDs)

    SemaphoreHandle_t _mutex;

    // Deferred param save support
    static const unsigned long SAVE_DEBOUNCE_MS = 3000;
    volatile bool     _paramsDirty;
    unsigned long     _lastParamDirtyTime;

    // Debounced active-program persist (resume on power-on). Don't write
    // config.json on every switch — only once the user has settled for 10 s.
    static const unsigned long RESUME_DEBOUNCE_MS = 10000;
    volatile bool     _configDirty;
    unsigned long     _configDirtyTime;
    // The program to resume on power-on = the last MANUALLY chosen program.
    // Distinct from _activeId, which during playlist rotation holds the current
    // (transient) position's program. config.json["active"] persists this.
    uint8_t           _resumeProgramId;

    // Async program switch / delete (set from BLE callback, processed in loop)
    volatile uint8_t  _pendingSwitchId;  // 0xFF = none
    volatile uint8_t  _pendingDeleteId;  // 0xFF = none
    volatile bool     _pendingClearAll;  // wipe all programs on next processPending()

    // Transient (playlist) switch: when set, the pending switch overlays these
    // params in memory and skips the config.json save. Written/consumed on the
    // render task only (the rotation engine ticks there), so no cross-task race.
    volatile bool     _pendingTransientSwitch;
    String            _pendingTransientParams;
};

#endif // PROGRAM_MANAGER_H
