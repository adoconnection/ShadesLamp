#include "program_manager.h"
#include "wasm_engine.h"
#include "param_store.h"
#include "led_driver.h"
#include "storage.h"
#include <ArduinoJson.h>

#define TAG "[PMGR]"

ProgramManager::ProgramManager(WasmEngine* engine, ParamStore* paramStore, LedDriver* ledDriver)
    : _engine(engine)
    , _paramStore(paramStore)
    , _ledDriver(ledDriver)
    , _activeId(0xFF)
    , _deviceName("Shades LED Lamp")
{
    _mutex = xSemaphoreCreateMutex();
}

void ProgramManager::begin() {
    Serial.printf("%s Initializing...\r\n", TAG);

    // Load metadata for all stored programs
    std::vector<uint8_t> ids = Storage::listPrograms();
    Serial.printf("%s Found %u programs on flash\r\n", TAG, ids.size());

    for (uint8_t id : ids) {
        loadProgramMeta(id);
    }

    // Load config (active program + saved params)
    loadConfig();

    // If we have a saved active program, switch to it
    if (_activeId != 0xFF) {
        int idx = findProgramIndex(_activeId);
        if (idx >= 0) {
            Serial.printf("%s Activating saved program %u\r\n", TAG, _activeId);
            uint8_t savedId = _activeId;
            _activeId = 0xFF; // Reset so switchProgram doesn't skip
            switchProgram(savedId);
        } else {
            Serial.printf("%s Saved active program %u not found\r\n", TAG, _activeId);
            _activeId = 0xFF;
        }
    }

    // If no active program but we have programs, activate the first one
    if (_activeId == 0xFF && !_programs.empty()) {
        Serial.printf("%s No saved active program, activating first available\r\n", TAG);
        switchProgram(_programs[0].id);
    }

    Serial.printf("%s Ready, %u programs, active=%u\r\n", TAG, _programs.size(), _activeId);
}

bool ProgramManager::switchProgram(uint8_t id) {
    xSemaphoreTake(_mutex, portMAX_DELAY);

    int idx = findProgramIndex(id);
    if (idx < 0) {
        Serial.printf("%s Program %u not found\r\n", TAG, id);
        xSemaphoreGive(_mutex);
        return false;
    }

    // Unload current program
    _engine->unload();
    _ledDriver->clear();
    _ledDriver->show();

    // Load WASM binary from storage
    uint8_t* wasmData = nullptr;
    size_t wasmSize = Storage::loadProgram(id, &wasmData);
    if (wasmSize == 0 || !wasmData) {
        Serial.printf("%s Failed to load program %u from storage\r\n", TAG, id);
        xSemaphoreGive(_mutex);
        return false;
    }

    // Reset param store (params applied after metadata is loaded below)
    _paramStore->reset();

    // Load into WASM engine
    bool ok = _engine->load(wasmData, wasmSize);
    free(wasmData);

    if (!ok) {
        Serial.printf("%s Failed to load program %u into WASM engine\r\n", TAG, id);
        xSemaphoreGive(_mutex);
        return false;
    }

    _activeId = id;

    // Update cached metadata from engine (may have been refreshed)
    String meta = _engine->getMetaJson();
    if (meta.length() > 2) { // More than "{}"
        _programs[idx].metaJson = meta;

        // Extract name from meta
        JsonDocument doc;
        if (!deserializeJson(doc, meta)) {
            if (doc.containsKey("name")) {
                _programs[idx].name = doc["name"].as<String>();
            }
        }
    }

    // Apply saved param values (or defaults) using metadata for correct types
    {
        JsonDocument savedDoc;
        bool hasSaved = (_savedParams[id].length() > 0) &&
                        !deserializeJson(savedDoc, _savedParams[id]);

        JsonDocument metaDoc;
        if (!deserializeJson(metaDoc, _programs[idx].metaJson)) {
            JsonArray params = metaDoc["params"].as<JsonArray>();
            if (params) {
                for (JsonObject p : params) {
                    uint8_t paramId = p["id"].as<uint8_t>();
                    String type = p["type"].as<String>();
                    bool isFloat = (type == "float");
                    String key = String(paramId);

                    if (hasSaved && savedDoc.containsKey(key)) {
                        if (isFloat)
                            _paramStore->setFloat(paramId, savedDoc[key].as<float>());
                        else
                            _paramStore->setInt(paramId, savedDoc[key].as<int32_t>());
                    } else {
                        if (isFloat)
                            _paramStore->setFloat(paramId, p["default"].as<float>());
                        else
                            _paramStore->setInt(paramId, p["default"].as<int32_t>());
                    }
                }
            }
        }
    }

    Serial.printf("%s Switched to program %u (%s)\r\n", TAG, id, _programs[idx].name.c_str());
    xSemaphoreGive(_mutex);
    return true;
}

int8_t ProgramManager::uploadProgram(const uint8_t* data, size_t size) {
    xSemaphoreTake(_mutex, portMAX_DELAY);

    if (_programs.size() >= MAX_PROGRAMS) {
        Serial.printf("%s Maximum number of programs reached\r\n", TAG);
        xSemaphoreGive(_mutex);
        return -1;
    }

    // Validate WASM binary (try parse + load in a temporary engine)
    if (!wasmValidate(data, size)) {
        Serial.printf("%s Upload rejected: invalid WASM\r\n", TAG);
        xSemaphoreGive(_mutex);
        return -1;
    }

    // Find a free ID
    uint8_t newId = Storage::nextFreeId();
    if (newId == 0xFF) {
        Serial.printf("%s No free program slot\r\n", TAG);
        xSemaphoreGive(_mutex);
        return -1;
    }

    // Save to storage
    if (!Storage::saveProgram(newId, data, size)) {
        Serial.printf("%s Failed to save program to storage\r\n", TAG);
        xSemaphoreGive(_mutex);
        return -1;
    }

    // Load metadata for the new program
    xSemaphoreGive(_mutex);
    loadProgramMeta(newId);

    Serial.printf("%s Uploaded program with ID %u\r\n", TAG, newId);
    return (int8_t)newId;
}

bool ProgramManager::deleteProgram(uint8_t id) {
    xSemaphoreTake(_mutex, portMAX_DELAY);

    int idx = findProgramIndex(id);
    if (idx < 0) {
        Serial.printf("%s Program %u not found for deletion\r\n", TAG, id);
        xSemaphoreGive(_mutex);
        return false;
    }

    // If this is the active program, unload it first
    bool wasActive = (_activeId == id);
    if (wasActive) {
        _engine->unload();
        _ledDriver->clear();
        _ledDriver->show();
        _activeId = 0xFF;
    }

    // Delete from storage
    if (!Storage::deleteProgram(id)) {
        xSemaphoreGive(_mutex);
        return false;
    }

    // Remove from program list
    _programs.erase(_programs.begin() + idx);
    _savedParams[id] = "";

    // If the deleted program was active, switch to another
    if (wasActive && !_programs.empty()) {
        uint8_t nextId = _programs[0].id;
        xSemaphoreGive(_mutex);
        switchProgram(nextId);
        saveState();
        return true;
    }

    xSemaphoreGive(_mutex);
    saveState();
    return true;
}

uint8_t ProgramManager::getActiveId() const {
    return _activeId;
}

uint8_t ProgramManager::getProgramCount() const {
    return (uint8_t)_programs.size();
}

String ProgramManager::getProgramName(uint8_t id) const {
    int idx = findProgramIndex(id);
    if (idx < 0) return String("Unknown");
    return _programs[idx].name;
}

String ProgramManager::getProgramListJson() const {
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();

    for (const ProgramInfo& p : _programs) {
        JsonObject obj = arr.add<JsonObject>();
        obj["id"] = p.id;
        obj["name"] = p.name;
    }

    String output;
    serializeJson(doc, output);
    return output;
}

String ProgramManager::getProgramParamsJson(uint8_t id) const {
    int idx = findProgramIndex(id);
    if (idx < 0) return "[]";

    // Extract "params" array from the cached meta JSON
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, _programs[idx].metaJson);
    if (err || !doc.containsKey("params")) {
        return "[]";
    }

    String output;
    serializeJson(doc["params"], output);
    return output;
}

String ProgramManager::getParamValuesJson(uint8_t id) const {
    if (id == _activeId) {
        return _paramStore->toJson();
    }
    // For non-active programs, return saved params
    if (id < MAX_PROGRAMS && _savedParams[id].length() > 0) {
        return _savedParams[id];
    }
    return "{}";
}

bool ProgramManager::setParam(uint8_t programId, uint8_t paramId, const uint8_t* value, size_t len) {
    if (paramId >= MAX_PARAMS || len < 4) return false;

    xSemaphoreTake(_mutex, portMAX_DELAY);

    // Copy the 4-byte value
    int32_t intVal;
    memcpy(&intVal, value, 4);

    if (programId == _activeId) {
        // Determine param type from metadata
        bool isFloat = false;
        int idx = findProgramIndex(programId);
        if (idx >= 0) {
            JsonDocument doc;
            if (!deserializeJson(doc, _programs[idx].metaJson)) {
                JsonArray params = doc["params"].as<JsonArray>();
                if (params) {
                    for (JsonObject p : params) {
                        if (p["id"].as<uint8_t>() == paramId) {
                            String type = p["type"].as<String>();
                            isFloat = (type == "float");
                            break;
                        }
                    }
                }
            }
        }

        if (isFloat) {
            float fVal;
            memcpy(&fVal, value, 4);
            _paramStore->setFloat(paramId, fVal);
            Serial.printf("%s Set param %u = %.3f (float)\r\n", TAG, paramId, fVal);
        } else {
            _paramStore->setInt(paramId, intVal);
            Serial.printf("%s Set param %u = %d (int)\r\n", TAG, paramId, intVal);
        }

        // Update saved params
        _savedParams[programId] = _paramStore->toJson();
    } else {
        // Update saved params for non-active program
        JsonDocument doc;
        if (_savedParams[programId].length() > 0) {
            deserializeJson(doc, _savedParams[programId]);
        }
        doc[String(paramId)] = intVal;
        _savedParams[programId] = "";
        serializeJson(doc, _savedParams[programId]);
    }

    xSemaphoreGive(_mutex);

    // Persist to config
    saveState();
    return true;
}

String ProgramManager::getDeviceName() const {
    return _deviceName;
}

void ProgramManager::setDeviceName(const String& name) {
    _deviceName = name;
    saveState();
}

void ProgramManager::saveState() {
    JsonDocument doc;
    doc["active"] = _activeId;
    doc["name"] = _deviceName;

    JsonObject paramsObj = doc["params"].to<JsonObject>();
    for (const ProgramInfo& p : _programs) {
        String paramJson = (_savedParams[p.id].length() > 0) ? _savedParams[p.id] : "{}";
        JsonDocument paramDoc;
        deserializeJson(paramDoc, paramJson);
        paramsObj[String(p.id)] = paramDoc;
    }

    String output;
    serializeJson(doc, output);
    Storage::saveConfig(output.c_str());
}

// ── Private helpers ────────────────────────────────────────────────────────

int ProgramManager::findProgramIndex(uint8_t id) const {
    for (size_t i = 0; i < _programs.size(); i++) {
        if (_programs[i].id == id) return (int)i;
    }
    return -1;
}

void ProgramManager::loadConfig() {
    String configStr = Storage::loadConfig();
    if (configStr.length() == 0) {
        Serial.printf("%s No config found\r\n", TAG);
        return;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, configStr);
    if (err) {
        Serial.printf("%s Config parse error: %s\r\n", TAG, err.c_str());
        return;
    }

    if (doc.containsKey("name")) {
        _deviceName = doc["name"].as<String>();
        Serial.printf("%s Config: device name = '%s'\r\n", TAG, _deviceName.c_str());
    }

    if (doc.containsKey("active")) {
        _activeId = doc["active"].as<uint8_t>();
        Serial.printf("%s Config: active program = %u\r\n", TAG, _activeId);
    }

    if (doc.containsKey("params")) {
        JsonObject paramsObj = doc["params"].as<JsonObject>();
        for (JsonPair kv : paramsObj) {
            uint8_t progId = (uint8_t)atoi(kv.key().c_str());
            if (progId < MAX_PROGRAMS) {
                String paramStr;
                serializeJson(kv.value(), paramStr);
                _savedParams[progId] = paramStr;
                Serial.printf("%s Config: params[%u] = %s\r\n", TAG, progId, paramStr.c_str());
            }
        }
    }
}

void ProgramManager::loadProgramMeta(uint8_t id) {
    // Load WASM binary from storage
    uint8_t* wasmData = nullptr;
    size_t wasmSize = Storage::loadProgram(id, &wasmData);
    if (wasmSize == 0 || !wasmData) {
        Serial.printf("%s Cannot load program %u for metadata\r\n", TAG, id);
        return;
    }

    ProgramInfo info;
    info.id = id;
    info.name = "Program " + String(id);
    info.metaJson = "{}";
    info.loaded = false;

    // Use the standalone metadata extractor from wasm_engine
    String meta = wasmExtractMeta(wasmData, wasmSize);
    free(wasmData);

    if (meta.length() > 2) { // More than "{}"
        info.metaJson = meta;
        info.loaded = true;

        // Extract program name from metadata JSON
        JsonDocument metaDoc;
        if (!deserializeJson(metaDoc, meta)) {
            if (metaDoc.containsKey("name")) {
                info.name = metaDoc["name"].as<String>();
            }
        }
    }

    // Add to program list (or update existing)
    xSemaphoreTake(_mutex, portMAX_DELAY);
    int existing = findProgramIndex(id);
    if (existing >= 0) {
        _programs[existing] = info;
    } else {
        _programs.push_back(info);
    }
    xSemaphoreGive(_mutex);

    Serial.printf("%s Program %u: '%s' (meta: %s)\r\n", TAG, id, info.name.c_str(),
                  info.loaded ? "OK" : "none");
}
