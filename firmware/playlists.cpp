#include "playlists.h"
#include "storage.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <algorithm>

#define TAG "[PLST]"
#define MAX_PLAYLISTS 64

namespace Playlists {

static void path(char* buf, size_t n, uint8_t id) {
    snprintf(buf, n, "/playlists/%u.json", id);
}

static std::vector<uint8_t> listIds() {
    std::vector<uint8_t> ids;
    File dir = LittleFS.open("/playlists");
    if (!dir || !dir.isDirectory()) return ids;
    File e;
    while ((e = dir.openNextFile())) {
        String name = e.name();
        int slash = name.lastIndexOf('/');
        if (slash >= 0) name = name.substring(slash + 1);
        bool isDir = e.isDirectory();
        e.close();
        if (isDir || !name.endsWith(".json")) continue;
        String idStr = name.substring(0, name.length() - 5);
        int id = idStr.toInt();
        if (id >= 0 && id < MAX_PLAYLISTS && (id != 0 || idStr == "0"))
            ids.push_back((uint8_t)id);
    }
    dir.close();
    std::sort(ids.begin(), ids.end());
    return ids;
}

static bool load(uint8_t id, JsonDocument& doc) {
    char p[40]; path(p, sizeof(p), id);
    String s = Storage::loadFile(p);
    if (s.length() == 0) return false;
    return deserializeJson(doc, s) == DeserializationError::Ok;
}

static bool save(uint8_t id, JsonDocument& doc) {
    char p[40]; path(p, sizeof(p), id);
    String out;
    serializeJson(doc, out);
    return Storage::writeFileEnsure(p, (const uint8_t*)out.c_str(), out.length());
}

String listJson() {
    JsonDocument out;
    JsonArray arr = out.to<JsonArray>();
    for (uint8_t id : listIds()) {
        JsonDocument doc;
        if (!load(id, doc)) continue;
        JsonObject o = arr.add<JsonObject>();
        o["id"] = id;
        o["name"] = doc["name"].as<const char*>();
        o["mode"] = doc["mode"] | 0;
        o["interval"] = doc["interval"] | 30;
        JsonArray pos = doc["positions"].as<JsonArray>();
        o["count"] = pos ? pos.size() : 0;
    }
    String s; serializeJson(out, s); return s;
}

String getJson(uint8_t id) {
    char p[40]; path(p, sizeof(p), id);
    return Storage::loadFile(p);
}

int create(const String& name) {
    std::vector<uint8_t> ids = listIds();
    int id = -1;
    for (int i = 0; i < MAX_PLAYLISTS; i++) {
        bool used = false;
        for (uint8_t e : ids) if (e == i) { used = true; break; }
        if (!used) { id = i; break; }
    }
    if (id < 0) return -1;
    JsonDocument doc;
    doc["name"] = name;
    doc["mode"] = 0;
    doc["interval"] = 30;
    doc["positions"].to<JsonArray>();
    if (!save((uint8_t)id, doc)) return -1;
    Serial.printf("%s create id=%d '%s'\r\n", TAG, id, name.c_str());
    return id;
}

bool rename(uint8_t id, const String& name) {
    JsonDocument doc;
    if (!load(id, doc)) return false;
    doc["name"] = name;
    return save(id, doc);
}

bool remove(uint8_t id) {
    char p[40]; path(p, sizeof(p), id);
    return Storage::deletePath(p);
}

bool setRotation(uint8_t id, uint8_t mode, uint16_t interval) {
    JsonDocument doc;
    if (!load(id, doc)) return false;
    doc["mode"] = mode;
    doc["interval"] = interval;
    return save(id, doc);
}

int addPosition(uint8_t id, const String& posJson) {
    JsonDocument doc;
    if (!load(id, doc)) return -1;
    JsonArray pos = doc["positions"].as<JsonArray>();
    if (!pos) pos = doc["positions"].to<JsonArray>();
    pos.add(serialized(posJson));
    int index = (int)pos.size() - 1;
    if (!save(id, doc)) return -1;
    return index;
}

bool removePosition(uint8_t id, uint8_t index) {
    JsonDocument doc;
    if (!load(id, doc)) return false;
    JsonArray pos = doc["positions"].as<JsonArray>();
    if (!pos || index >= pos.size()) return false;
    pos.remove(index);
    return save(id, doc);
}

bool reorder(uint8_t id, const String& indicesJson) {
    JsonDocument doc;
    if (!load(id, doc)) return false;
    JsonArray pos = doc["positions"].as<JsonArray>();
    if (!pos) return false;

    // Snapshot current positions as raw JSON strings.
    std::vector<String> items;
    for (JsonVariant v : pos) { String s; serializeJson(v, s); items.push_back(s); }

    JsonDocument idxDoc;
    if (deserializeJson(idxDoc, indicesJson) != DeserializationError::Ok) return false;
    JsonArray idx = idxDoc.as<JsonArray>();
    if (!idx) return false;

    JsonDocument out;
    out["name"] = doc["name"];
    out["mode"] = doc["mode"];
    out["interval"] = doc["interval"];
    JsonArray np = out["positions"].to<JsonArray>();
    for (JsonVariant iv : idx) {
        int i = iv.as<int>();
        if (i >= 0 && i < (int)items.size()) np.add(serialized(items[i]));
    }
    return save(id, out);
}

} // namespace Playlists
