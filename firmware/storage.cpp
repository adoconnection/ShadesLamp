#include "storage.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <algorithm>

#define TAG "[STOR]"
#define MAX_PROGRAMS 128

// Marker file written once the legacy flat layout has been migrated, so the
// (slightly expensive) migration scan runs at most once per device.
#define LAYOUT_MARKER "/.layout_v2"

namespace Storage {

// ── Path helpers ─────────────────────────────────────────────────────────────

// Directory for a program, e.g. "/programs/3"
static void progDir(char* buf, size_t n, uint8_t id) {
    snprintf(buf, n, "/programs/%u", id);
}

// File inside a program directory, e.g. "/programs/3/code.wasm"
static void progFile(char* buf, size_t n, uint8_t id, const char* file) {
    snprintf(buf, n, "/programs/%u/%s", id, file);
}

// Make sure /programs/{id}/ exists
static bool ensureProgDir(uint8_t id) {
    char dir[48];
    progDir(dir, sizeof(dir), id);
    if (LittleFS.exists(dir)) return true;
    return LittleFS.mkdir(dir);
}

// ── Legacy flat-layout migration ─────────────────────────────────────────────
//
// Old layout: /programs/{id}.wasm, /meta/{id}.json, /params/{id}.json
// New layout: /programs/{id}/{code.wasm,meta.json,params.json,.ok}
//
// Existing programs are trusted (they were running before), so they get the
// .ok marker as the final migration step.
static void migrateFlatLayout() {
    if (LittleFS.exists(LAYOUT_MARKER)) return;  // already migrated

    // Collect legacy flat program IDs first (don't rename while iterating).
    std::vector<uint8_t> flatIds;
    File dir = LittleFS.open("/programs");
    if (dir && dir.isDirectory()) {
        File entry;
        while ((entry = dir.openNextFile())) {
            String name = entry.name();
            int lastSlash = name.lastIndexOf('/');
            if (lastSlash >= 0) name = name.substring(lastSlash + 1);
            bool isDir = entry.isDirectory();
            entry.close();
            if (isDir) continue;                 // already a program folder
            if (!name.endsWith(".wasm")) continue;
            String idStr = name.substring(0, name.length() - 5);
            int id = idStr.toInt();
            if (id >= 0 && id < MAX_PROGRAMS && (id != 0 || idStr == "0")) {
                flatIds.push_back((uint8_t)id);
            }
        }
        dir.close();
    }

    for (uint8_t id : flatIds) {
        char src[48], dst[48];

        if (!ensureProgDir(id)) {
            Serial.printf("%s Migrate: cannot create dir for program %u\r\n", TAG, id);
            continue;
        }

        // code.wasm — the essential file. Move it, then mark .ok so the program
        // survives even if the meta/params moves below are interrupted.
        snprintf(src, sizeof(src), "/programs/%u.wasm", id);
        progFile(dst, sizeof(dst), id, "code.wasm");
        if (LittleFS.exists(src)) LittleFS.rename(src, dst);

        markProgramInstalled(id);  // existing program -> trusted

        // meta.json (best effort)
        snprintf(src, sizeof(src), "/meta/%u.json", id);
        progFile(dst, sizeof(dst), id, "meta.json");
        if (LittleFS.exists(src)) LittleFS.rename(src, dst);

        // params.json (best effort)
        snprintf(src, sizeof(src), "/params/%u.json", id);
        progFile(dst, sizeof(dst), id, "params.json");
        if (LittleFS.exists(src)) LittleFS.rename(src, dst);

        Serial.printf("%s Migrated program %u to folder layout\r\n", TAG, id);
    }

    // Drop now-empty legacy directories (best effort; ignore failures).
    LittleFS.rmdir("/meta");
    LittleFS.rmdir("/params");

    // Record completion so we never scan again.
    File marker = LittleFS.open(LAYOUT_MARKER, "w");
    if (marker) { marker.print("2"); marker.close(); }
    Serial.printf("%s Layout migration complete (%u programs)\r\n", TAG, (unsigned)flatIds.size());
}

// ── Init ─────────────────────────────────────────────────────────────────────

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

    // Migrate any legacy flat-layout programs into per-program folders
    migrateFlatLayout();

    return true;
}

// ── Program listing ──────────────────────────────────────────────────────────

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
        // entry.name() may return full path (e.g. "/programs/0") or just the name
        int lastSlash = name.lastIndexOf('/');
        if (lastSlash >= 0) name = name.substring(lastSlash + 1);
        bool isDir = entry.isDirectory();
        entry.close();

        if (!isDir) continue;  // only per-program directories count

        // Directory names are numeric program IDs
        int id = name.toInt();
        if (id >= 0 && id < MAX_PROGRAMS && (id != 0 || name == "0")) {
            ids.push_back((uint8_t)id);
        }
    }
    dir.close();

    std::sort(ids.begin(), ids.end());
    return ids;
}

bool saveProgram(uint8_t id, const uint8_t* data, size_t size) {
    if (!ensureProgDir(id)) {
        Serial.printf("%s Cannot create dir for program %u\r\n", TAG, id);
        return false;
    }

    char path[48];
    progFile(path, sizeof(path), id, "code.wasm");

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

    char path[48];
    progFile(path, sizeof(path), id, "code.wasm");

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
    char dir[48];
    progDir(dir, sizeof(dir), id);

    if (!LittleFS.exists(dir)) {
        Serial.printf("%s Program %u not found\r\n", TAG, id);
        return false;
    }

    // Remove every file inside the program directory, then the directory.
    File d = LittleFS.open(dir);
    if (d && d.isDirectory()) {
        std::vector<String> files;
        File entry;
        while ((entry = d.openNextFile())) {
            // entry.name() may be a full path or a bare filename; normalize
            // to an absolute path so LittleFS.remove() always resolves it.
            String name = entry.name();
            entry.close();
            if (name.startsWith("/")) {
                files.push_back(name);
            } else {
                files.push_back(String(dir) + "/" + name);
            }
        }
        d.close();
        for (const String& f : files) {
            LittleFS.remove(f);
        }
    }

    bool ok = LittleFS.rmdir(dir);
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

bool markProgramInstalled(uint8_t id) {
    if (!ensureProgDir(id)) return false;
    char path[48];
    progFile(path, sizeof(path), id, ".ok");
    File f = LittleFS.open(path, "w");
    if (!f) return false;
    f.print("1");
    f.close();
    Serial.printf("%s Program %u marked installed (.ok)\r\n", TAG, id);
    return true;
}

bool isProgramInstalled(uint8_t id) {
    char path[48];
    progFile(path, sizeof(path), id, ".ok");
    return LittleFS.exists(path);
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

void loadHardwareConfig(uint8_t& pin, uint16_t& width, uint16_t& height, bool& zigzag, uint8_t& colorOrder) {
    String configStr = loadConfig();
    if (configStr.length() == 0) return;

    JsonDocument doc;
    if (deserializeJson(doc, configStr)) return;

    if (doc.containsKey("ledPin"))        pin        = doc["ledPin"].as<uint8_t>();
    if (doc.containsKey("ledWidth"))      width      = doc["ledWidth"].as<uint16_t>();
    if (doc.containsKey("ledHeight"))     height     = doc["ledHeight"].as<uint16_t>();
    if (doc.containsKey("ledZigzag"))     zigzag     = doc["ledZigzag"].as<bool>();
    if (doc.containsKey("ledColorOrder")) colorOrder = doc["ledColorOrder"].as<uint8_t>();

    Serial.printf("%s HW config: pin=%u, %ux%u, zigzag=%d, order=%u\r\n", TAG, pin, width, height, zigzag, colorOrder);
}

bool saveParamValues(uint8_t id, const char* json) {
    if (!ensureProgDir(id)) return false;
    char path[48];
    progFile(path, sizeof(path), id, "params.json");
    File f = LittleFS.open(path, "w");
    if (!f) return false;
    f.print(json);
    f.close();
    return true;
}

String loadParamValues(uint8_t id) {
    char path[48];
    progFile(path, sizeof(path), id, "params.json");
    if (!LittleFS.exists(path)) return String();
    File f = LittleFS.open(path, "r");
    if (!f) return String();
    String content = f.readString();
    f.close();
    return content;
}

bool deleteParamValues(uint8_t id) {
    char path[48];
    progFile(path, sizeof(path), id, "params.json");
    if (!LittleFS.exists(path)) return true;
    return LittleFS.remove(path);
}

bool saveProgramMeta(uint8_t id, const char* json) {
    if (!ensureProgDir(id)) return false;
    char path[48];
    progFile(path, sizeof(path), id, "meta.json");
    File f = LittleFS.open(path, "w");
    if (!f) return false;
    f.print(json);
    f.close();
    Serial.printf("%s Meta saved for program %u\r\n", TAG, id);
    return true;
}

String loadProgramMeta(uint8_t id) {
    char path[48];
    progFile(path, sizeof(path), id, "meta.json");
    if (!LittleFS.exists(path)) return String();
    File f = LittleFS.open(path, "r");
    if (!f) return String();
    String content = f.readString();
    f.close();
    return content;
}

bool deleteProgramMeta(uint8_t id) {
    char path[48];
    progFile(path, sizeof(path), id, "meta.json");
    if (!LittleFS.exists(path)) return true;
    return LittleFS.remove(path);
}

bool saveFile(const char* path, const char* data) {
    File f = LittleFS.open(path, "w");
    if (!f) return false;
    f.print(data);
    f.close();
    return true;
}

String loadFile(const char* path) {
    if (!LittleFS.exists(path)) return String();
    File f = LittleFS.open(path, "r");
    if (!f) return String();
    String content = f.readString();
    f.close();
    return content;
}

} // namespace Storage
