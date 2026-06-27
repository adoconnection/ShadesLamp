#include "playlists.h"
#include "storage.h"
#include "program_manager.h"
#include "ble_service.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <algorithm>

#define TAG "[PLST]"
#define MAX_PLAYLISTS 64
#define PL_STATE_PATH "/pl_state.json"

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

bool setPositionParams(uint8_t id, uint8_t index, const String& paramsJson) {
    JsonDocument doc;
    if (!load(id, doc)) return false;
    JsonArray pos = doc["positions"].as<JsonArray>();
    if (!pos || index >= pos.size()) return false;
    JsonObject o = pos[index].as<JsonObject>();
    if (o.isNull()) return false;

    JsonDocument pDoc;
    if (deserializeJson(pDoc, paramsJson) != DeserializationError::Ok) return false;
    JsonArray pa = pDoc.as<JsonArray>();
    if (!pa) return false;

    o["params"] = pa;   // deep-copies the new params array into the playlist doc
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

// ── Rotation engine ─────────────────────────────────────────────────────────
// Engine state. Scalars are written from the BLE task (playStart/stop/edits) and
// read on the render task (tickRotation); 32-bit aligned access is atomic on the
// ESP32, and a one-cycle stale read of mode/interval is harmless, so `volatile`
// is enough — no mutex needed. The actual program switch is always issued from
// the render task via the g_applyPending flag, so no String crosses tasks.
static volatile int      g_playId      = -1;   // -1 = nothing playing
static volatile int      g_index       = 0;    // current position index
static volatile uint8_t  g_mode        = 0;    // 0=off, 1=next, 2=random
static volatile uint16_t g_interval    = 30;   // seconds
static volatile int      g_count       = 0;    // positions in the playing list
static volatile bool     g_applyPending = false; // render task should apply g_index now
static volatile bool     g_clearPending = false; // render task should delete the state file
static volatile bool     g_notifyStopPending = false; // render task should emit EVT_PL_STOPPED
static volatile bool     g_stateDirty  = false; // render task should persist play state
static uint32_t          g_stateDirtyMs = 0;    // millis() of last play/jump (debounce)
static uint32_t          g_lastMs      = 0;    // render-task only

// Persist which playlist is playing only after the user has settled, so rapid
// play/swipe doesn't hammer flash. The resume position isn't important.
#define PL_STATE_DEBOUNCE_MS 10000U

static void saveState() {
    JsonDocument doc;
    doc["id"] = g_playId;
    doc["index"] = g_index;
    String out; serializeJson(doc, out);
    Storage::writeFileEnsure(PL_STATE_PATH, (const uint8_t*)out.c_str(), out.length());
}

static void clearState() {
    Storage::deletePath(PL_STATE_PATH);
}

// Load mode/interval/count from a playlist file into the engine cache.
static bool loadMeta(uint8_t id, int* outCount) {
    JsonDocument doc;
    if (!load(id, doc)) return false;
    g_mode = doc["mode"] | 0;
    g_interval = doc["interval"] | 30;
    JsonArray pos = doc["positions"].as<JsonArray>();
    int n = pos ? (int)pos.size() : 0;
    g_count = n;
    if (outCount) *outCount = n;
    return true;
}

// Issue the transient switch for position `index` of playlist `id`.
// MUST run on the render task (calls pm->requestSwitchTransient).
static bool applyPositionIndex(ProgramManager* pm, uint8_t id, int index) {
    JsonDocument doc;
    if (!load(id, doc)) return false;
    JsonArray pos = doc["positions"].as<JsonArray>();
    if (!pos || index < 0 || index >= (int)pos.size()) return false;
    JsonObject o = pos[index].as<JsonObject>();
    if (o.isNull()) return false;
    int prog = o["prog"] | -1;
    if (prog < 0 || prog > 255) return false;
    String params;
    JsonArray pa = o["params"].as<JsonArray>();
    if (pa) serializeJson(pa, params); else params = "[]";
    pm->requestSwitchTransient((uint8_t)prog, params);
    return true;
}

void playStart(uint8_t id, int index) {
    int n = 0;
    if (!loadMeta(id, &n)) return;   // playlist missing
    g_playId = id;
    g_clearPending = false;          // we're (re)starting — don't let a deferred clear wipe the new state
    g_notifyStopPending = false;     // ...nor a deferred stop-notify cancel it in the app
    // Schedule a debounced persist (10 s after the last play/swipe).
    g_stateDirtyMs = millis();
    g_stateDirty = true;
    if (n <= 0) { g_index = 0; g_applyPending = false; return; }
    if (index < 0 || index >= n) index = 0;
    g_index = index;
    g_applyPending = true;           // render task applies it on next tick
    Serial.printf("%s play id=%u index=%d (mode=%u interval=%u count=%d)\r\n",
                  TAG, id, index, g_mode, g_interval, n);
}

void stop() {
    if (g_playId < 0) return;
    g_playId = -1;
    g_applyPending = false;
    g_stateDirty = false;            // cancel any pending debounced save
    // Defer the flash delete + client notify to the render task so stop() is
    // safe to call from any context (BLE callback, touch task).
    g_clearPending = true;
    g_notifyStopPending = true;
    Serial.printf("%s stop\r\n", TAG);
}

void onRotationChanged(uint8_t id, uint8_t mode, uint16_t interval) {
    if (g_playId == (int)id) { g_mode = mode; g_interval = interval; }
}

void onPositionsChanged(uint8_t id) {
    if (g_playId != (int)id) return;
    int n = 0;
    if (!loadMeta(id, &n)) return;
    if (n <= 0) { g_index = 0; return; }
    if (g_index >= n) g_index = 0;
}

void onDeleted(uint8_t id) {
    if (g_playId == (int)id) stop();
}

void resumeFromState() {
    String s = Storage::loadFile(PL_STATE_PATH);
    if (s.length() == 0) return;
    JsonDocument doc;
    if (deserializeJson(doc, s)) { clearState(); return; }
    int id = doc["id"] | -1;
    int index = doc["index"] | 0;
    if (id < 0) { clearState(); return; }
    int n = 0;
    if (!loadMeta((uint8_t)id, &n)) { clearState(); return; }  // playlist gone
    g_playId = id;
    g_index = (n > 0 && index >= 0 && index < n) ? index : 0;
    g_applyPending = (n > 0);
    Serial.printf("%s resume id=%d index=%d\r\n", TAG, id, g_index);
}

void tickRotation(ProgramManager* pm, BleService* ble, uint32_t now) {
    // A stop() from any task asked us to drop the persisted state / notify the
    // app; do it here on the render task (runs every frame, even while idle).
    if (g_clearPending) { g_clearPending = false; clearState(); }
    if (g_notifyStopPending) { g_notifyStopPending = false; if (ble) ble->notifyEvent(EVT_PL_STOPPED, 0); }
    if (g_stateDirty && (uint32_t)(now - g_stateDirtyMs) >= PL_STATE_DEBOUNCE_MS) {
        g_stateDirty = false;
        if (g_playId >= 0) saveState();   // persist which playlist to resume
    }
    if (g_playId < 0) return;

    // Apply a freshly-requested position (play/jump/resume), even mid-fade.
    if (g_applyPending) {
        g_applyPending = false;
        if (applyPositionIndex(pm, (uint8_t)g_playId, g_index)) g_lastMs = now;
        return;
    }

    if (g_mode == 0 || g_count <= 0) return;       // 'off' or empty: hold
    if (pm->hasPendingSwitch()) return;            // a switch is already in flight

    uint16_t sec = g_interval < 2 ? 2 : g_interval; // floor to avoid thrash
    if ((uint32_t)(now - g_lastMs) < (uint32_t)sec * 1000UL) return;

    int next;
    if (g_mode == 2 && g_count > 1) {
        next = (int)(now % (uint32_t)g_count);     // pseudo-random (no rand dep)
        if (next == g_index) next = (next + 1) % g_count;
    } else {
        next = (g_index + 1) % g_count;
    }

    if (applyPositionIndex(pm, (uint8_t)g_playId, next)) {
        g_index = next;
        g_lastMs = now;
        if (ble) ble->notifyEvent(EVT_PL_ADVANCE, (uint8_t)g_index);
    } else {
        g_lastMs = now;                            // skip a beat, retry next interval
        onPositionsChanged((uint8_t)g_playId);     // positions may have shrunk
    }
}

int playingId()    { return g_playId; }
int currentIndex() { return g_index; }

} // namespace Playlists
