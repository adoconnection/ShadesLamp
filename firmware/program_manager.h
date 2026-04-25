#ifndef PROGRAM_MANAGER_H
#define PROGRAM_MANAGER_H

#include <Arduino.h>
#include <vector>

// Forward declarations
class WasmEngine;
class ParamStore;
class LedDriver;

#define MAX_PROGRAMS 16

struct ProgramInfo {
    uint8_t id;
    String  name;
    String  metaJson;
    bool    loaded;     // true if metadata was successfully extracted
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

    // Get params metadata JSON for a program (from WASM meta)
    String getProgramParamsJson(uint8_t id) const;

    // Get current param values JSON for a program
    String getParamValuesJson(uint8_t id) const;

    // Set a parameter value for a program
    bool setParam(uint8_t programId, uint8_t paramId, const uint8_t* value, size_t len);

    // Persist active program + param values to config.json
    void saveState();

private:
    int findProgramIndex(uint8_t id) const;
    void loadConfig();
    void loadProgramMeta(uint8_t id);

    WasmEngine* _engine;
    ParamStore* _paramStore;
    LedDriver*  _ledDriver;

    std::vector<ProgramInfo> _programs;
    uint8_t _activeId;

    // Saved parameter values per program: _savedParams[programId] = JSON string
    String _savedParams[MAX_PROGRAMS];

    SemaphoreHandle_t _mutex;
};

#endif // PROGRAM_MANAGER_H
