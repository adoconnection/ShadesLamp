#ifndef STORAGE_H
#define STORAGE_H

#include <Arduino.h>
#include <vector>

// On-flash layout (LittleFS), one directory per program:
//
//   /programs/{id}/code.wasm    WASM binary
//   /programs/{id}/meta.json    program metadata
//   /programs/{id}/params.json  saved parameter values
//   /programs/{id}/.ok          install-complete marker (commit flag)
//
// A program without the .ok marker is an incomplete/failed install and is
// wiped on the next boot. Older devices using the legacy flat layout
// (/programs/{id}.wasm, /meta/{id}.json, /params/{id}.json) are migrated to
// the new layout automatically during init().

namespace Storage {

    // Mount LittleFS, format on first use, migrate legacy flat layout
    bool init();

    // List all program IDs (one subdirectory per program), sorted ascending
    std::vector<uint8_t> listPrograms();

    // Save WASM binary to /programs/{id}/code.wasm
    bool saveProgram(uint8_t id, const uint8_t* data, size_t size);

    // Load WASM binary from /programs/{id}/code.wasm
    // Allocates buffer with ps_malloc; caller must free()
    // Returns size in bytes, 0 on error
    size_t loadProgram(uint8_t id, uint8_t** outData);

    // Remove a program entirely (whole /programs/{id}/ directory)
    bool deleteProgram(uint8_t id);

    // Find the next unused program ID (0..MAX_PROGRAMS-1)
    uint8_t nextFreeId();

    // Mark a program as fully installed (creates the .ok marker file).
    // Call this only once the install is complete (metadata written).
    bool markProgramInstalled(uint8_t id);

    // True if the program has the .ok marker (i.e. install completed)
    bool isProgramInstalled(uint8_t id);

    // Save config JSON to /config.json
    bool saveConfig(const char* json);

    // Load config JSON from /config.json (returns "" if not found)
    String loadConfig();

    // Read hardware config (pin, width, height, zigzag, colorOrder) from /config.json
    // Leaves parameters unchanged if fields are missing
    void loadHardwareConfig(uint8_t& pin, uint16_t& width, uint16_t& height, bool& zigzag, uint8_t& colorOrder);

    // Save param values for a single program to /programs/{id}/params.json
    bool saveParamValues(uint8_t id, const char* json);

    // Load param values for a single program from /programs/{id}/params.json
    String loadParamValues(uint8_t id);

    // Delete param values file for a program
    bool deleteParamValues(uint8_t id);

    // Save meta JSON to /programs/{id}/meta.json
    bool saveProgramMeta(uint8_t id, const char* json);

    // Load meta JSON from /programs/{id}/meta.json (returns "" if not found)
    String loadProgramMeta(uint8_t id);

    // Delete /programs/{id}/meta.json
    bool deleteProgramMeta(uint8_t id);

    // Generic file save/load
    bool saveFile(const char* path, const char* data);
    String loadFile(const char* path);

} // namespace Storage

#endif // STORAGE_H
