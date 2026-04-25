#include "storage.h"
#include <LittleFS.h>
#include <algorithm>

#define TAG "[STOR]"
#define MAX_PROGRAMS 16

namespace Storage {

bool init() {
    if (!LittleFS.begin(true)) {  // format on fail
        Serial.printf("%s LittleFS mount failed even after format\r\n", TAG);
        return false;
    }
    Serial.printf("%s LittleFS mounted\r\n", TAG);

    // Ensure /programs directory exists
    if (!LittleFS.exists("/programs")) {
        LittleFS.mkdir("/programs");
        Serial.printf("%s Created /programs directory\r\n", TAG);
    }

    return true;
}

std::vector<uint8_t> listPrograms() {
    std::vector<uint8_t> ids;

    File dir = LittleFS.open("/programs");
    if (!dir || !dir.isDirectory()) {
        Serial.printf("%s Cannot open /programs directory\r\n", TAG);
        return ids;
    }

    File entry;
    while ((entry = dir.openNextFile())) {
        String name = entry.name();
        // entry.name() may return full path (e.g. "/programs/0.wasm") or just filename
        int lastSlash = name.lastIndexOf('/');
        if (lastSlash >= 0) {
            name = name.substring(lastSlash + 1);
        }
        // File names are like "0.wasm", "1.wasm", etc.
        if (name.endsWith(".wasm")) {
            // Extract ID from filename
            String idStr = name.substring(0, name.length() - 5);
            int id = idStr.toInt();
            // toInt returns 0 for non-numeric strings, so validate
            if (id >= 0 && id < MAX_PROGRAMS && (id != 0 || idStr == "0")) {
                ids.push_back((uint8_t)id);
            }
        }
        entry.close();
    }
    dir.close();

    std::sort(ids.begin(), ids.end());
    return ids;
}

bool saveProgram(uint8_t id, const uint8_t* data, size_t size) {
    char path[32];
    snprintf(path, sizeof(path), "/programs/%u.wasm", id);

    File f = LittleFS.open(path, "w");
    if (!f) {
        Serial.printf("%s Cannot open %s for writing\r\n", TAG, path);
        return false;
    }

    size_t written = f.write(data, size);
    f.close();

    if (written != size) {
        Serial.printf("%s Write error: %u/%u bytes\r\n", TAG, written, size);
        return false;
    }

    Serial.printf("%s Saved program %u (%u bytes)\r\n", TAG, id, size);
    return true;
}

size_t loadProgram(uint8_t id, uint8_t** outData) {
    if (!outData) return 0;
    *outData = nullptr;

    char path[32];
    snprintf(path, sizeof(path), "/programs/%u.wasm", id);

    File f = LittleFS.open(path, "r");
    if (!f) {
        Serial.printf("%s Cannot open %s for reading\r\n", TAG, path);
        return 0;
    }

    size_t size = f.size();
    if (size == 0) {
        f.close();
        return 0;
    }

    // Allocate in PSRAM for large WASM binaries
    uint8_t* buf = (uint8_t*)ps_malloc(size);
    if (!buf) {
        // Fallback to regular malloc
        buf = (uint8_t*)malloc(size);
        if (!buf) {
            Serial.printf("%s Cannot allocate %u bytes for program %u\r\n", TAG, size, id);
            f.close();
            return 0;
        }
    }

    size_t readBytes = f.read(buf, size);
    f.close();

    if (readBytes != size) {
        Serial.printf("%s Read error: %u/%u bytes\r\n", TAG, readBytes, size);
        free(buf);
        return 0;
    }

    *outData = buf;
    Serial.printf("%s Loaded program %u (%u bytes)\r\n", TAG, id, size);
    return size;
}

bool deleteProgram(uint8_t id) {
    char path[32];
    snprintf(path, sizeof(path), "/programs/%u.wasm", id);

    if (!LittleFS.exists(path)) {
        Serial.printf("%s Program %u not found\r\n", TAG, id);
        return false;
    }

    bool ok = LittleFS.remove(path);
    Serial.printf("%s Delete program %u: %s\r\n", TAG, id, ok ? "OK" : "FAIL");
    return ok;
}

uint8_t nextFreeId() {
    std::vector<uint8_t> ids = listPrograms();
    for (uint8_t i = 0; i < MAX_PROGRAMS; i++) {
        bool found = false;
        for (uint8_t existing : ids) {
            if (existing == i) { found = true; break; }
        }
        if (!found) return i;
    }
    return 0xFF; // No free slot
}

bool saveConfig(const char* json) {
    File f = LittleFS.open("/config.json", "w");
    if (!f) {
        Serial.printf("%s Cannot open /config.json for writing\r\n", TAG);
        return false;
    }
    f.print(json);
    f.close();
    Serial.printf("%s Config saved\r\n", TAG);
    return true;
}

String loadConfig() {
    if (!LittleFS.exists("/config.json")) {
        return String();
    }

    File f = LittleFS.open("/config.json", "r");
    if (!f) {
        return String();
    }

    String content = f.readString();
    f.close();
    Serial.printf("%s Config loaded (%u bytes)\r\n", TAG, content.length());
    return content;
}

} // namespace Storage
