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

    // Read hardware config (pin, width, height, zigzag, colorOrder) from /config.json
    // Leaves parameters unchanged if fields are missing
    void loadHardwareConfig(uint8_t& pin, uint16_t& width, uint16_t& height, bool& zigzag, uint8_t& colorOrder);

    // Save param values for a single program to /params/{id}.json
    bool saveParamValues(uint8_t id, const char* json);

    // Load param values for a single program from /params/{id}.json
    String loadParamValues(uint8_t id);

    // Delete param values file for a program
    bool deleteParamValues(uint8_t id);

    // Save meta JSON to /meta/{id}.json
    bool saveProgramMeta(uint8_t id, const char* json);

    // Load meta JSON from /meta/{id}.json (returns "" if not found)
    String loadProgramMeta(uint8_t id);

    // Delete /meta/{id}.json
    bool deleteProgramMeta(uint8_t id);

    // Generic file save/load
    bool saveFile(const char* path, const char* data);
    String loadFile(const char* path);

} // namespace Storage

#endif // STORAGE_H
