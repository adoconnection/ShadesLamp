#ifndef STORAGE_H
#define STORAGE_H

#include <Arduino.h>
#include <vector>

namespace Storage {

    // Mount LittleFS, format on first use
    bool init();

    // List all program IDs from /programs/ directory, sorted ascending
    std::vector<uint8_t> listPrograms();

    // Save WASM binary to /programs/{id}.wasm
    bool saveProgram(uint8_t id, const uint8_t* data, size_t size);

    // Load WASM binary from /programs/{id}.wasm
    // Allocates buffer with ps_malloc; caller must free()
    // Returns size in bytes, 0 on error
    size_t loadProgram(uint8_t id, uint8_t** outData);

    // Delete /programs/{id}.wasm
    bool deleteProgram(uint8_t id);

    // Find the next unused program ID (0..15)
    uint8_t nextFreeId();

    // Save config JSON to /config.json
    bool saveConfig(const char* json);

    // Load config JSON from /config.json (returns "" if not found)
    String loadConfig();

} // namespace Storage

#endif // STORAGE_H
