#ifndef PLAYLISTS_H
#define PLAYLISTS_H

#include <Arduino.h>
#include <vector>

// Forward declarations (engine drives program switches + advance notifications).
class ProgramManager;
class BleService;

// Playlists owned by the firmware. One JSON file per playlist:
//   /playlists/{id}.json = {"name","mode","interval","positions":[ {pos}, ... ]}
// A position carries the program id + a param snapshot:
//   {"prog":<id>,"slug":"...","name":"...","params":[{"id","value","f"}]}
// Order == array order. Positions are addressed by their array index.
// mode: 0=off, 1=next, 2=random.
//
// The LAMP owns rotation: while a playlist is "playing" the rotation engine
// (ticked on the render task) advances positions per the file's mode/interval
// and applies each position transiently (no rewrite of the program's params).

namespace Playlists {

    // JSON summary array: [{"id","name","mode","interval","count"}, ...]
    String listJson();

    // Full raw JSON of a playlist file (""=not found). Sent via chunked response.
    String getJson(uint8_t id);

    // Create an empty playlist, returns new id or -1.
    int create(const String& name);

    bool rename(uint8_t id, const String& name);
    bool remove(uint8_t id);
    bool setRotation(uint8_t id, uint8_t mode, uint16_t interval);

    // Append a position (raw JSON object). Returns new index or -1.
    int addPosition(uint8_t id, const String& posJson);

    // Remove the position at the given array index.
    bool removePosition(uint8_t id, uint8_t index);

    // Replace the param snapshot of the position at `index`. paramsJson is a JSON
    // array [{"id","value","f"}]. Order and program are kept; only params change.
    bool setPositionParams(uint8_t id, uint8_t index, const String& paramsJson);

    // Reorder positions: indicesJson is a JSON array of current indices in the
    // new order, e.g. [2,0,1].
    bool reorder(uint8_t id, const String& indicesJson);

    // ── Rotation engine (lamp-driven playback) ──────────────────────────────
    // All engine functions are cheap and safe to call from BLE callbacks except
    // tickRotation(), which MUST run on the render task (it requests program
    // switches and sends notifications).

    // Start playing a playlist from `index` (clamped; <0 means 0). Applies that
    // position and begins auto-advancing. Persists the play state for resume.
    void playStart(uint8_t id, int index);

    // Stop playback (the current program keeps running). Clears persisted state.
    void stop();

    // Render-task tick: applies a pending position and advances when the
    // interval elapses. now = millis().
    void tickRotation(ProgramManager* pm, BleService* ble, uint32_t now);

    // Cache-sync hooks, called from the matching BLE edit commands so a running
    // playlist reflects live edits without reloading the file every tick.
    void onRotationChanged(uint8_t id, uint8_t mode, uint16_t interval);
    void onPositionsChanged(uint8_t id);   // reload count, clamp index
    void onDeleted(uint8_t id);            // stop if this playlist was playing

    // Restore play state saved before the last reboot (call once at boot, after
    // ProgramManager::begin()). Applies the saved position on the next tick.
    void resumeFromState();

    // Live engine state for the GET_STATE command. playingId() = -1 when idle.
    int playingId();
    int currentIndex();

} // namespace Playlists

#endif // PLAYLISTS_H
