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

// ── RAM store ────────────────────────────────────────────────────────────────
// All playlist files live in RAM after init(); flash is touched only to
// persist writes (write-through) and once at boot. Accessed from the BLE task
// (edits, GETs) and the render task (rotation), hence the mutex; critical
// sections only copy Strings — no flash I/O and no JSON parsing inside.

static String            g_store[MAX_PLAYLISTS];   // raw JSON, "" = absent
static SemaphoreHandle_t g_storeMutex = nullptr;
static bool              g_storeReady = false;

// Set when any playlist changes so the render-task position cache (see
// rotation engine below) rebuilds on the next tick. Playlist writes are rare
// (user edits), so no need to check whether it's the playing one. volatile
// bool: written from the BLE task, read on the render task.
static volatile bool g_cacheDirty = true;

static std::vector<uint8_t> listIdsFromFlash() {
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

void init() {
    if (!g_storeMutex) g_storeMutex = xSemaphoreCreateMutex();
    uint32_t t0 = millis();
    int n = 0;
    for (uint8_t id : listIdsFromFlash()) {
        char p[40]; path(p, sizeof(p), id);
        g_store[id] = Storage::loadFile(p);
        if (g_store[id].length() > 0) n++;
    }
    g_storeReady = true;
    Serial.printf("%s init: %d playlist(s) cached in RAM (%lu ms)\r\n", TAG, n, (unsigned long)(millis() - t0));
}

static std::vector<uint8_t> listIds() {
    if (!g_storeReady) return listIdsFromFlash();
    std::vector<uint8_t> ids;
    xSemaphoreTake(g_storeMutex, portMAX_DELAY);
    for (int i = 0; i < MAX_PLAYLISTS; i++)
        if (g_store[i].length() > 0) ids.push_back((uint8_t)i);
    xSemaphoreGive(g_storeMutex);
    return ids;
}

// Raw JSON of one playlist from the RAM store ("" = absent).
static String storeGet(uint8_t id) {
    if (id >= MAX_PLAYLISTS) return String();
    if (!g_storeReady) {
        char p[40]; path(p, sizeof(p), id);
        return Storage::loadFile(p);
    }
    xSemaphoreTake(g_storeMutex, portMAX_DELAY);
    String s = g_store[id];
    xSemaphoreGive(g_storeMutex);
    return s;
}

static void storePut(uint8_t id, const String& json) {
    if (id >= MAX_PLAYLISTS) return;
    xSemaphoreTake(g_storeMutex, portMAX_DELAY);
    g_store[id] = json;
    xSemaphoreGive(g_storeMutex);
    g_cacheDirty = true;
}

static bool load(uint8_t id, JsonDocument& doc) {
    String s = storeGet(id);
    if (s.length() == 0) return false;
    return deserializeJson(doc, s) == DeserializationError::Ok;
}

static bool save(uint8_t id, JsonDocument& doc) {
    char p[40]; path(p, sizeof(p), id);
    String out;
    serializeJson(doc, out);
    bool ok = Storage::writeFileEnsure(p, (const uint8_t*)out.c_str(), out.length());
    if (ok) storePut(id, out);
    return ok;
}

void onFileChanged(const String& fpath) {
    // "/playlists/{id}.json" written/appended/deleted via generic file
    // commands: mirror the flash state back into the RAM store.
    if (!fpath.startsWith("/playlists/") || !fpath.endsWith(".json")) return;
    String idStr = fpath.substring(11, fpath.length() - 5);
    int id = idStr.toInt();
    if (id < 0 || id >= MAX_PLAYLISTS || (id == 0 && idStr != "0")) return;
    storePut((uint8_t)id, Storage::loadFile(fpath.c_str()));
    Serial.printf("%s RAM store refreshed for playlist %d (external write)\r\n", TAG, id);
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
    return storeGet(id);
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
    bool ok = Storage::deletePath(p);
    if (ok) storePut(id, String());
    return ok;
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

// One-time migration: stamp a stable guid onto legacy positions that only have
// a numeric `prog`. Run once at boot (after ProgramManager::begin, before
// resumeFromState) so older playlists survive program delete/update/re-download.
// Idempotent: positions that already carry a guid are left untouched, and a file
// is only rewritten if something actually changed.
void migrateGuids(ProgramManager* pm) {
    for (uint8_t id : listIds()) {
        JsonDocument doc;
        if (!load(id, doc)) continue;
        JsonArray pos = doc["positions"].as<JsonArray>();
        if (!pos) continue;
        bool changed = false;
        for (JsonObject o : pos) {
            const char* g = o["guid"] | (const char*)nullptr;
            if (g && g[0]) continue;                 // already has a guid
            // Resolve by slug first (the reliable stored identity), then by the
            // legacy prog slot. prog ids drift as programs are added/removed, so
            // a slug match is what keeps a stamped guid correct.
            int id = -1;
            const char* slug = o["slug"] | (const char*)nullptr;
            if (slug && slug[0]) id = pm->resolveSlug(String(slug));
            if (id < 0) {
                int prog = o["prog"] | -1;
                if (prog >= 0 && prog <= 255 && pm->hasProgram((uint8_t)prog)) id = prog;
            }
            if (id < 0) continue;                     // program missing → leave legacy
            String guid = pm->programGuid((uint8_t)id);
            if (guid.length() == 0) continue;        // program has no guid → skip
            o["guid"] = guid;                        // ArduinoJson deep-copies
            changed = true;
        }
        if (changed) {
            save(id, doc);
            Serial.printf("%s migrated guids for playlist %u\r\n", TAG, id);
        }
    }
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
static volatile int      g_advanceNotifyIdx = -1; // EVT_PL_ADVANCE deferred until the switch lands (-1 = none)
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

// ── Render-task position cache ──────────────────────────────────────────────
// The rotation advance used to re-read + re-parse the playlist file from
// LittleFS (twice: findPlayableIndex + applyPositionIndex) on the render task
// while the outgoing program was still at full brightness — a visible freeze
// right before the fade-out. The positions are cached in RAM instead; the
// periodic advance path then does zero flash I/O. Rebuilt when g_cacheDirty is
// set (any playlist write) or the playing id changes. Touched ONLY on the
// render task, so no locking is needed on the vector itself.

struct PosCache {
    String guid;
    String slug;
    int    prog;
    String params;   // serialized params array ("[]" when absent)
};
static std::vector<PosCache> g_cache;
static int g_cacheId = -1;   // playlist id the cache holds (-1 = none)

// Render task only. Cheap when the cache is already valid.
static bool ensureCache(ProgramManager* pm, uint8_t id) {
    if (!g_cacheDirty && g_cacheId == (int)id) return true;
    g_cacheDirty = false;
    g_cacheId = -1;
    g_cache.clear();

    JsonDocument doc;
    if (!load(id, doc)) return false;
    JsonArray pos = doc["positions"].as<JsonArray>();
    if (pos) {
        for (JsonObject o : pos) {
            if (o.isNull()) continue;
            PosCache e;
            e.guid = (const char*)(o["guid"] | "");
            e.slug = (const char*)(o["slug"] | "");
            e.prog = o["prog"] | -1;
            JsonArray pa = o["params"].as<JsonArray>();
            if (pa) serializeJson(pa, e.params); else e.params = "[]";
            g_cache.push_back(e);
        }
    }
    g_cacheId = (int)id;

    // Pull every program's meta into RAM now (guid/slug resolution needs it),
    // so the lazy per-file loads don't land on a mid-rotation frame.
    pm->warmAllMeta();
    return true;
}

// Resolve a cached position to a currently-installed program id, or -1 if its
// program is missing (deleted / not yet re-downloaded). Resolution order mirrors
// the app (guid → slug → prog): the guid is authoritative when present; legacy
// positions fall back to slug (a reliable stored identity) and only then to the
// numeric `prog` slot, which drifts as programs are added/removed.
static int resolvePosProgram(ProgramManager* pm, const PosCache& e) {
    if (e.guid.length() > 0) return pm->resolveGuid(e.guid);    // -1 if uninstalled
    if (e.slug.length() > 0) {
        int id = pm->resolveSlug(e.slug);
        if (id >= 0) return id;
    }
    if (e.prog < 0 || e.prog > 255) return -1;
    return pm->hasProgram((uint8_t)e.prog) ? e.prog : -1;       // verify still present
}

// Find the next index (within `count` steps from `start`, stepping by `dir`)
// whose position resolves to an installed program. Returns -1 if the whole
// playlist is currently unplayable (every program missing / empty).
// Render task only (uses the cache).
static int findPlayableIndex(ProgramManager* pm, uint8_t id, int start, int dir) {
    if (!ensureCache(pm, id)) return -1;
    int n = (int)g_cache.size();
    if (n <= 0) return -1;
    if (dir == 0) dir = 1;
    int i = ((start % n) + n) % n;
    for (int k = 0; k < n; k++) {
        if (resolvePosProgram(pm, g_cache[i]) >= 0) return i;
        i = (((i + dir) % n) + n) % n;
    }
    return -1;
}

// Issue the transient switch for position `index` of playlist `id`.
// MUST run on the render task (calls pm->requestSwitchTransient, uses the cache).
// Returns false if the position's program is missing — caller skips it.
static bool applyPositionIndex(ProgramManager* pm, uint8_t id, int index) {
    if (!ensureCache(pm, id)) return false;
    if (index < 0 || index >= (int)g_cache.size()) return false;
    const PosCache& e = g_cache[index];
    int prog = resolvePosProgram(pm, e);
    if (prog < 0) return false;   // program missing → skip this position
    pm->requestSwitchTransient((uint8_t)prog, e.params);
    return true;
}

void playStart(uint8_t id, int index) {
    int n = 0;
    if (!loadMeta(id, &n)) return;   // playlist missing
    g_playId = id;
    g_cacheDirty = true;             // file may have been edited since last cached
    g_clearPending = false;          // we're (re)starting — don't let a deferred clear wipe the new state
    g_notifyStopPending = false;     // ...nor a deferred stop-notify cancel it in the app
    // Schedule a debounced persist (10 s after the last play/swipe).
    g_stateDirtyMs = millis();
    g_stateDirty = true;
    g_advanceNotifyIdx = -1;         // a stale deferred advance is now obsolete
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
    g_advanceNotifyIdx = -1;         // don't announce an advance after stopping
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
    g_cacheDirty = true;
    if (g_playId != (int)id) return;
    int n = 0;
    if (!loadMeta(id, &n)) return;
    if (n <= 0) { g_index = 0; return; }
    if (g_index >= n) g_index = 0;
}

void onDeleted(uint8_t id) {
    if (g_playId == (int)id) stop();
}

void clearResumeState() {
    clearState();
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

    // A deferred EVT_PL_ADVANCE fires once the queued switch has actually been
    // applied (the crossfade machine consumes it at the dip-to-black), so the
    // app's index change lines up with the visible transition — old program
    // already faded out, new one not shown yet — instead of preceding the fade.
    if (g_advanceNotifyIdx >= 0 && !pm->hasPendingSwitch()) {
        int idx = g_advanceNotifyIdx;
        g_advanceNotifyIdx = -1;
        if (ble) ble->notifyEvent(EVT_PL_ADVANCE, (uint8_t)idx);
    }

    // Apply a freshly-requested position (play/jump/resume), even mid-fade.
    // If the requested program is missing, skip forward to the next playable
    // position and tell the app where we actually landed.
    if (g_applyPending) {
        g_applyPending = false;
        int want = g_index;
        int idx = findPlayableIndex(pm, (uint8_t)g_playId, want, +1);
        if (idx >= 0) {
            g_index = idx;
            if (applyPositionIndex(pm, (uint8_t)g_playId, idx)) g_lastMs = now;
            if (idx != want) g_advanceNotifyIdx = idx;
        }
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

    // Skip over any positions whose program is currently missing.
    next = findPlayableIndex(pm, (uint8_t)g_playId, next, +1);
    if (next < 0) {                                // nothing playable right now
        g_lastMs = now;                            // hold; retry next interval
        onPositionsChanged((uint8_t)g_playId);     // positions may have shrunk
        return;
    }

    if (applyPositionIndex(pm, (uint8_t)g_playId, next)) {
        g_index = next;
        g_lastMs = now;
        g_advanceNotifyIdx = next;   // announced at the dip-to-black (see above)
    } else {
        g_lastMs = now;                            // skip a beat, retry next interval
        onPositionsChanged((uint8_t)g_playId);     // positions may have shrunk
    }
}

int playingId()    { return g_playId; }
int currentIndex() { return g_index; }

} // namespace Playlists
