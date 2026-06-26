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
#include "touch_input.h"

// Touch sensor (digital, active-HIGH) for hardware power/program control
static const uint8_t TOUCH_PIN = 1;

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

    // Host-side crossfade on program switch: fade the old program out, swap,
    // then fade the new one in (dip to black). Switch is deferred until fade-out.
    enum FadeState { FADE_IDLE, FADE_OUT, FADE_IN };
    FadeState fadeState = FADE_IDLE;
    uint32_t  fadeStart = 0;
    const uint32_t FADE_MS = 300;

    while (true) {
        // Skip rendering while upload is in progress (LED shows progress bar)
        if (bleService->isPausedByUpload()) {
            vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(33));
            continue;
        }

        // Skip rendering when power is off (LEDs stay dark)
        if (!bleService->isPowerOn()) {
            vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(100));
            continue;
        }

        // ── Program-switch crossfade state machine ──
        uint32_t now = millis();
        if (fadeState == FADE_IDLE) {
            if (programManager->hasPendingSwitch()) {
                // Begin fading the current program out; apply the switch later.
                fadeState = FADE_OUT;
                fadeStart = now;
            } else {
                // No switch pending: run normally, handle deletes + deferred saves.
                ledDriver->setFadeScale(256);
                uint8_t prevActive = programManager->getActiveId();
                programManager->processPending();
                uint8_t newActive = programManager->getActiveId();
                if (newActive != prevActive) {
                    bleService->notifyActiveProgram(newActive);
                }
            }
        }

        if (fadeState == FADE_OUT) {
            uint32_t el = now - fadeStart;
            if (el >= FADE_MS) {
                // Fade-out done: swap program (at black), then fade the new one in.
                uint8_t prevActive = programManager->getActiveId();
                ledDriver->setFadeScale(0);
                programManager->processPending();   // applies the queued switch
                uint8_t newActive = programManager->getActiveId();
                if (newActive != prevActive) {
                    bleService->notifyActiveProgram(newActive);
                    Serial.printf("[MAIN] Program switched: %u -> %u\r\n", prevActive, newActive);
                }
                fadeState = FADE_IN;
                fadeStart = millis();
            } else {
                ledDriver->setFadeScale((uint16_t)(256 - el * 256 / FADE_MS)); // 256 -> 0
            }
        } else if (fadeState == FADE_IN) {
            uint32_t el = now - fadeStart;
            if (el >= FADE_MS) {
                ledDriver->setFadeScale(256);
                fadeState = FADE_IDLE;
            } else {
                ledDriver->setFadeScale((uint16_t)(el * 256 / FADE_MS));         // 0 -> 256
            }
        }

        // Process deferred BLE event notifications (queued from BLE callbacks)
        bleService->processPendingEvents();

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
    uint8_t ledColorOrder = 0; // 0=GRB (default WS2812)
    Storage::loadHardwareConfig(ledPin, ledWidth, ledHeight, ledZigzag, ledColorOrder);

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
    ledDriver = new LedDriver(ledPin, ledWidth, ledHeight, ledZigzag, ledColorOrder);
    ledDriver->begin();
    ledDriver->setMaxCurrent(2000); // limit estimated LED draw to ~2 A

    // Create engine and manager with dynamic pointers
    wasmEngine = new WasmEngine(ledDriver, &paramStore);
    programManager = new ProgramManager(wasmEngine, &paramStore, ledDriver);
    bleService = new BleService(programManager, ledDriver);

    // Initialize program manager (loads programs from flash, activates saved program)
    programManager->begin();

    // Initialize BLE service (use device name from config, default: "Shades LED Lamp")
    bleService->begin(programManager->getDeviceName().c_str());

    // Initialize hardware touch button
    g_touch = new TouchInput(TOUCH_PIN, /*activeLow=*/false);
    g_touch->begin(bleService, programManager);

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
    if (g_touch) g_touch->tick();
    vTaskDelay(pdMS_TO_TICKS(5));
}
