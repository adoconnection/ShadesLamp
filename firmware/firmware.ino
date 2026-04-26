/**
 * Shades Lamp — WASM-programmable LED controller for ESP32-S3 (N16R8)
 *
 * Runs WASM programs to control WS2812 LEDs. Programs are uploaded
 * and managed over BLE, stored persistently in LittleFS.
 *
 * Hardware: ESP32-S3-DevKitC-1 N16R8, 1x WS2812 on GPIO 48
 */

#include "led_driver.h"
#include "param_store.h"
#include "wasm_engine.h"
#include "program_manager.h"
#include "ble_service.h"
#include "storage.h"

// ── Global Objects ─────────────────────────────────────────────────────────

LedDriver*     ledDriver = nullptr;
ParamStore     paramStore;
WasmEngine*    wasmEngine = nullptr;
ProgramManager* programManager = nullptr;
BleService*    bleService = nullptr;

// ── FreeRTOS Render Task ───────────────────────────────────────────────────

// Render task runs on Core 1 at ~30 FPS, calling the WASM update() function
void renderTask(void* param) {
    TickType_t lastWake = xTaskGetTickCount();
    uint32_t startTick = millis();

    Serial.printf("[MAIN] Render task started on core %d\r\n", xPortGetCoreID());

    while (true) {
        // Process pending program switches + deferred saves
        uint8_t prevActive = programManager->getActiveId();
        programManager->processPending();
        uint8_t newActive = programManager->getActiveId();
        if (newActive != prevActive) {
            bleService->notifyActiveProgram(newActive);
            Serial.printf("[MAIN] Program switched: %u -> %u\r\n", prevActive, newActive);
        }

        if (wasmEngine->isLoaded()) {
            int32_t tick = (int32_t)(millis() - startTick);
            wasmEngine->tick(tick);

            // If WASM program changed params (e.g. preset → RGB), notify BLE clients
            if (wasmEngine->consumeParamsChanged()) {
                programManager->requestParamSave();
                bleService->notifyParamValues();
            }
        }
        vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(33)); // ~30 FPS
    }
}

// ── Arduino Setup ──────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    delay(500); // Wait for serial monitor to connect

    // Initialize flash storage (needed before reading config)
    if (!Storage::init()) {
        Serial.println("[MAIN] Storage init FAILED — halting");
        while (true) { delay(1000); }
    }

    // Read hardware config from flash (before creating LedDriver)
    uint8_t ledPin = 48;
    uint16_t ledWidth = 1, ledHeight = 1;
    bool ledZigzag = false;
    Storage::loadHardwareConfig(ledPin, ledWidth, ledHeight, ledZigzag);

    Serial.println();
    Serial.println("=================================");
    Serial.println("  Shades Lamp v1.0");
    Serial.println("  ESP32-S3 N16R8");
    Serial.printf("  LED: %ux%u on GPIO %u%s\r\n", ledWidth, ledHeight, ledPin, ledZigzag ? " (zigzag)" : "");
    Serial.println("=================================");
    Serial.println();

    // Report memory
    Serial.printf("[MAIN] Free heap: %u bytes\r\n", ESP.getFreeHeap());
    Serial.printf("[MAIN] Free PSRAM: %u bytes\r\n", ESP.getFreePsram());

    // Create and initialize LED driver with config values
    ledDriver = new LedDriver(ledPin, ledWidth, ledHeight, ledZigzag);
    ledDriver->begin();

    // Create engine and manager with dynamic pointers
    wasmEngine = new WasmEngine(ledDriver, &paramStore);
    programManager = new ProgramManager(wasmEngine, &paramStore, ledDriver);
    bleService = new BleService(programManager);

    // Initialize program manager (loads programs from flash, activates saved program)
    programManager->begin();

    // Initialize BLE service (use device name from config, default: "Shades LED Lamp")
    bleService->begin(programManager->getDeviceName().c_str());

    // Create render task on Core 1 (BLE runs on Core 0)
    BaseType_t taskResult = xTaskCreatePinnedToCore(
        renderTask,     // task function
        "render",       // name
        65536,          // stack size (bytes) — wasm3 interpreter + switchProgram needs generous stack
        NULL,           // parameter
        2,              // priority
        NULL,           // task handle
        1               // core ID
    );

    if (taskResult != pdPASS) {
        Serial.println("[MAIN] Failed to create render task!");
    }

    // Startup success: blink white LED
    ledDriver->setPixel(0, 0, 255, 255, 255);
    ledDriver->show();
    delay(300);
    ledDriver->clear();
    ledDriver->show();
    delay(200);
    ledDriver->setPixel(0, 0, 255, 255, 255);
    ledDriver->show();
    delay(300);
    ledDriver->clear();
    ledDriver->show();

    Serial.printf("[MAIN] Setup complete. Free heap: %u bytes\r\n", ESP.getFreeHeap());
}

// ── Arduino Loop ───────────────────────────────────────────────────────────

void loop() {
    vTaskDelay(pdMS_TO_TICKS(1000));
}
