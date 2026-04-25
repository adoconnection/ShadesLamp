#include "param_store.h"
#include <ArduinoJson.h>

#define TAG "[PARAM]"

ParamStore::ParamStore() {
    reset();
}

void ParamStore::setInt(uint8_t id, int32_t val) {
    if (id >= MAX_PARAMS) return;
    _values[id].i = val;
    _set[id] = true;
}

int32_t ParamStore::getInt(uint8_t id) const {
    if (id >= MAX_PARAMS) return 0;
    return _values[id].i;
}

void ParamStore::setFloat(uint8_t id, float val) {
    if (id >= MAX_PARAMS) return;
    _values[id].f = val;
    _set[id] = true;
}

float ParamStore::getFloat(uint8_t id) const {
    if (id >= MAX_PARAMS) return 0.0f;
    return _values[id].f;
}

void ParamStore::reset() {
    memset(_values, 0, sizeof(_values));
    memset(_set, 0, sizeof(_set));
}

String ParamStore::toJson() const {
    JsonDocument doc;

    for (uint8_t i = 0; i < MAX_PARAMS; i++) {
        if (_set[i]) {
            doc[String(i)] = _values[i].i;
        }
    }

    String output;
    serializeJson(doc, output);
    return output;
}

void ParamStore::fromJson(const char* json) {
    if (!json || json[0] == '\0') return;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json);
    if (err) {
        Serial.printf("%s Failed to parse param JSON: %s\r\n", TAG, err.c_str());
        return;
    }

    for (JsonPair kv : doc.as<JsonObject>()) {
        uint8_t id = (uint8_t)atoi(kv.key().c_str());
        if (id < MAX_PARAMS) {
            _values[id].i = kv.value().as<int32_t>();
            _set[id] = true;
        }
    }
}
