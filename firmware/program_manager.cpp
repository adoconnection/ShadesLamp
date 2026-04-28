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
    , _ledPin(48)
    , _ledWidth(1)
    , _ledHeight(1)
    , _ledZigzag(false)
    , _paramsDirty(false)
    , _lastParamDirtyTime(0)
    , _pendingSwitchId(0xFF)
    , _pendingDeleteId(0xFF)
{
    _mutex = xSemaphoreCreateMutex();
}

void ProgramManager::begin() {
    Serial.printf("%s Initializing...\r\n", TAG);

    // Register all program IDs (no file reads, just directory listing)
    std::vector<uint8_t> ids = Storage::listPrograms();
    Serial.printf("%s Found %u programs on flash\r\n", TAG, ids.size());

    for (uint8_t id : ids) {
        loadProgramMeta(id);  // Just registers ID, no I/O
    }

    // Load config (active program, device name, hw settings)
    loadConfig();

    // Load custom order if exists
    String orderStr = Storage::loadFile("/order.json");
    if (orderStr.length() > 2) {
        JsonDocument orderDoc;
        if (!deserializeJson(orderDoc, orderStr)) {
            JsonArray arr = orderDoc.as<JsonArray>();
            for (JsonVariant v : arr) {
                _order.push_back(v.as<uint8_t>());
            }
            Serial.printf("%s Loaded custom order: %u entries\r\n", TAG, _order.size());
        }
    }

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
    // Flush any pending param save before switching
    if (_paramsDirty) {
        _paramsDirty = false;
        saveActiveParams();
    }

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

    // Load saved param values from /params/{id}.json
    String savedParamsStr = Storage::loadParamValues(id);

    // Apply saved param values (or defaults) using metadata for correct types
    {
        JsonDocument savedDoc;
        bool hasSaved = (savedParamsStr.length() > 0) &&
                        !deserializeJson(savedDoc, savedParamsStr);

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

    // Generate fallback meta.json if none exists
    String existingMeta = Storage::loadProgramMeta(newId);
    if (existingMeta.length() < 3) {
        String fallback = getProgramMeta(newId); // generates fallback
        Storage::saveProgramMeta(newId, fallback.c_str());
        Serial.printf("%s Generated fallback meta for program %u\r\n", TAG, newId);
    }

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

    // Delete from storage (wasm + params + meta)
    Storage::deleteProgram(id);
    Storage::deleteParamValues(id);
    Storage::deleteProgramMeta(id);

    // Remove from program list
    _programs.erase(_programs.begin() + idx);

    // If the deleted program was active, switch to another
    if (wasActive && !_programs.empty()) {
        uint8_t nextId = _programs[0].id;
        xSemaphoreGive(_mutex);
        switchProgram(nextId);
        saveConfig();
        return true;
    }

    xSemaphoreGive(_mutex);
    saveConfig();
    return true;
}

uint8_t ProgramManager::getActiveId() const {
    return _activeId;
}

uint8_t ProgramManager::getProgramCount() const {
    return (uint8_t)_programs.size();
}

String ProgramManager::getProgramName(uint8_t id) const {
    ensureMetaLoaded(id);
    int idx = findProgramIndex(id);
    if (idx < 0) return String("Unknown");
    return _programs[idx].name;
}

String ProgramManager::getProgramListJson() const {
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();

    auto addProgram = [&](uint8_t id) {
        int idx = findProgramIndex(id);
        if (idx < 0) return;
        const ProgramInfo& p = _programs[idx];
        ensureMetaLoaded(p.id);
        JsonObject obj = arr.add<JsonObject>();
        obj["id"] = p.id;
        obj["name"] = p.name;
        if (p.guid.length() > 0)    obj["guid"] = p.guid;
        if (p.version.length() > 0) obj["version"] = p.version;
    };

    if (_order.size() > 0) {
        // Emit in custom order first
        for (uint8_t id : _order) addProgram(id);
        // Add any programs not in the order list
        for (const ProgramInfo& p : _programs) {
            bool found = false;
            for (uint8_t oid : _order) { if (oid == p.id) { found = true; break; } }
            if (!found) addProgram(p.id);
        }
    } else {
        for (const ProgramInfo& p : _programs) addProgram(p.id);
    }

    String output;
    serializeJson(doc, output);
    return output;
}

String ProgramManager::getProgramMeta(uint8_t id) const {
    // Try loading from /meta/{id}.json
    String metaStr = Storage::loadProgramMeta(id);
    if (metaStr.length() > 2) return metaStr;

    // Generate minimal fallback (no WASM loading here)
    int idx = findProgramIndex(id);
    if (idx < 0) return "{}";

    JsonDocument out;
    out["name"] = _programs[idx].name;
    out["desc"] = "";
    out["author"] = "unknown";
    out["category"] = "Effects";

    JsonObject cover = out["cover"].to<JsonObject>();
    cover["from"] = "#555555";
    cover["to"] = "#999999";
    cover["angle"] = 135;

    out["pulse"] = "#888888";

    String result;
    serializeJson(out, result);
    return result;
}

bool ProgramManager::setProgramMeta(uint8_t id, const String& json) {
    int idx = findProgramIndex(id);
    if (idx < 0) return false;

    if (!Storage::saveProgramMeta(id, json.c_str())) return false;

    // Update cached fields
    JsonDocument richDoc;
    if (!deserializeJson(richDoc, json)) {
        xSemaphoreTake(_mutex, portMAX_DELAY);
        if (richDoc.containsKey("author"))   _programs[idx].author = richDoc["author"].as<String>();
        if (richDoc.containsKey("category")) _programs[idx].category = richDoc["category"].as<String>();
        if (richDoc.containsKey("pulse"))    _programs[idx].pulse = richDoc["pulse"].as<String>();
        if (richDoc.containsKey("cover")) {
            String coverStr;
            serializeJson(richDoc["cover"], coverStr);
            _programs[idx].coverJson = coverStr;
        }
        xSemaphoreGive(_mutex);
    }

    return true;
}

String ProgramManager::getProgramParamsJson(uint8_t id) const {
    int idx = findProgramIndex(id);
    if (idx < 0) return "[]";

    // If metaJson is empty, it hasn't been extracted from WASM yet
    // For the active program, it was populated during switchProgram()
    // For non-active programs, extract on demand
    if (_programs[idx].metaJson.length() < 3) {
        // Lazy-load WASM metadata
        uint8_t* wasmData = nullptr;
        size_t wasmSize = Storage::loadProgram(id, &wasmData);
        if (wasmSize > 0 && wasmData) {
            String meta = wasmExtractMeta(wasmData, wasmSize);
            free(wasmData);
            if (meta.length() > 2) {
                // Cache it (cast away const for lazy init)
                const_cast<ProgramInfo&>(_programs[idx]).metaJson = meta;
            }
        }
    }

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
    // For non-active programs, load from file
    String params = Storage::loadParamValues(id);
    if (params.length() > 0) return params;
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
    } else {
        // Update saved params for non-active program (direct file write)
        String savedStr = Storage::loadParamValues(programId);
        JsonDocument doc;
        if (savedStr.length() > 0) {
            deserializeJson(doc, savedStr);
        }
        doc[String(paramId)] = intVal;
        String output;
        serializeJson(doc, output);
        Storage::saveParamValues(programId, output.c_str());
    }

    xSemaphoreGive(_mutex);

    // Deferred save for active program params (throttled to avoid flash wear)
    if (programId == _activeId) {
        requestParamSave();
    }
    return true;
}

String ProgramManager::getDeviceName() const {
    return _deviceName;
}

void ProgramManager::setDeviceName(const String& name) {
    _deviceName = name;
    saveConfig();
}

uint8_t ProgramManager::getLedPin() const { return _ledPin; }
uint16_t ProgramManager::getLedWidth() const { return _ledWidth; }
uint16_t ProgramManager::getLedHeight() const { return _ledHeight; }
bool ProgramManager::getLedZigzag() const { return _ledZigzag; }

void ProgramManager::setHardwareConfig(uint8_t pin, uint16_t width, uint16_t height, bool zigzag) {
    _ledPin = pin;
    _ledWidth = width;
    _ledHeight = height;
    _ledZigzag = zigzag;
    saveConfig();
}

void ProgramManager::saveConfig() {
    JsonDocument doc;
    doc["active"] = _activeId;
    doc["name"] = _deviceName;
    doc["ledPin"] = _ledPin;
    doc["ledWidth"] = _ledWidth;
    doc["ledHeight"] = _ledHeight;
    doc["ledZigzag"] = _ledZigzag;

    String output;
    serializeJson(doc, output);
    Storage::saveConfig(output.c_str());
    Serial.printf("%s Config saved (%u bytes)\r\n", TAG, output.length());
}

void ProgramManager::saveActiveParams() {
    if (_activeId == 0xFF) return;
    String json = _paramStore->toJson();
    Storage::saveParamValues(_activeId, json.c_str());
    Serial.printf("%s Params saved for program %u\r\n", TAG, _activeId);
}

void ProgramManager::requestParamSave() {
    _lastParamDirtyTime = millis();
    _paramsDirty = true;
}

void ProgramManager::flushIfDirty() {
    if (!_paramsDirty) return;
    if (millis() - _lastParamDirtyTime < SAVE_DEBOUNCE_MS) return;

    _paramsDirty = false;
    saveActiveParams();
}

void ProgramManager::requestSwitch(uint8_t programId) {
    _pendingSwitchId = programId;
}

void ProgramManager::requestDelete(uint8_t programId) {
    _pendingDeleteId = programId;
}

void ProgramManager::processPending() {
    // Handle async program delete (must run on render task, not BLE core)
    uint8_t deleteId = _pendingDeleteId;
    if (deleteId != 0xFF) {
        _pendingDeleteId = 0xFF;
        deleteProgram(deleteId);
    }

    // Handle async program switch
    uint8_t pendingId = _pendingSwitchId;
    if (pendingId != 0xFF) {
        _pendingSwitchId = 0xFF;
        if (switchProgram(pendingId)) {
            saveConfig();
        }
    }

    // Handle deferred param save
    flushIfDirty();
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

    if (doc.containsKey("ledPin"))    _ledPin    = doc["ledPin"].as<uint8_t>();
    if (doc.containsKey("ledWidth"))  _ledWidth  = doc["ledWidth"].as<uint16_t>();
    if (doc.containsKey("ledHeight")) _ledHeight = doc["ledHeight"].as<uint16_t>();
    if (doc.containsKey("ledZigzag")) _ledZigzag = doc["ledZigzag"].as<bool>();

    if (doc.containsKey("active")) {
        _activeId = doc["active"].as<uint8_t>();
        Serial.printf("%s Config: active program = %u\r\n", TAG, _activeId);
    }

    // Migrate old-style params from config.json to individual files
    if (doc.containsKey("params")) {
        Serial.printf("%s Migrating params from config.json to /params/\r\n", TAG);
        JsonObject paramsObj = doc["params"].as<JsonObject>();
        for (JsonPair kv : paramsObj) {
            uint8_t progId = (uint8_t)atoi(kv.key().c_str());
            if (progId < MAX_PROGRAMS) {
                String paramStr;
                serializeJson(kv.value(), paramStr);
                Storage::saveParamValues(progId, paramStr.c_str());
                Serial.printf("%s  Migrated params[%u]\r\n", TAG, progId);
            }
        }
        // Re-save config without params
        saveConfig();
    }
}

void ProgramManager::loadProgramMeta(uint8_t id) {
    // At startup: just register the program ID. No file reads.
    // Meta is loaded lazily when a BLE client requests it.
    ProgramInfo info;
    info.id = id;
    info.name = "";
    info.metaJson = "";
    info.loaded = false;

    xSemaphoreTake(_mutex, portMAX_DELAY);
    int existing = findProgramIndex(id);
    if (existing >= 0) {
        _programs[existing] = info;
    } else {
        _programs.push_back(info);
    }
    xSemaphoreGive(_mutex);
}

// Ensure /meta/{id}.json fields are cached for a program (lazy load)
void ProgramManager::ensureMetaLoaded(uint8_t id) const {
    int idx = findProgramIndex(id);
    if (idx < 0) return;
    if (_programs[idx].loaded) return;  // Already cached

    ProgramInfo& info = const_cast<ProgramInfo&>(_programs[idx]);

    String richMeta = Storage::loadProgramMeta(id);
    if (richMeta.length() > 2) {
        JsonDocument richDoc;
        if (!deserializeJson(richDoc, richMeta)) {
            if (richDoc.containsKey("name"))     info.name = richDoc["name"].as<String>();
            if (richDoc.containsKey("author"))   info.author = richDoc["author"].as<String>();
            if (richDoc.containsKey("category")) info.category = richDoc["category"].as<String>();
            if (richDoc.containsKey("pulse"))    info.pulse = richDoc["pulse"].as<String>();
            if (richDoc.containsKey("cover")) {
                String coverStr;
                serializeJson(richDoc["cover"], coverStr);
                info.coverJson = coverStr;
            }
            if (richDoc.containsKey("guid"))    info.guid = richDoc["guid"].as<String>();
            if (richDoc.containsKey("version")) info.version = richDoc["version"].as<String>();
        }
    }

    // If still no name, use fallback
    if (info.name.length() == 0) {
        info.name = "Program " + String(id);
    }

    info.loaded = true;
}

String ProgramManager::getOrderJson() const {
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();

    if (_order.size() > 0) {
        for (uint8_t id : _order) arr.add(id);
    } else {
        for (const ProgramInfo& p : _programs) arr.add(p.id);
    }

    String output;
    serializeJson(doc, output);
    return output;
}

bool ProgramManager::setOrder(const String& json) {
    JsonDocument doc;
    if (deserializeJson(doc, json)) return false;

    JsonArray arr = doc.as<JsonArray>();
    _order.clear();
    for (JsonVariant v : arr) {
        _order.push_back(v.as<uint8_t>());
    }

    // Persist to /order.json
    Storage::saveFile("/order.json", json.c_str());
    return true;
}
