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
#include "playlists.h"

#include <Update.h>
#include "esp_task_wdt.h"

// setup() runs in the Arduino loopTask. programManager->begin() activates the
// saved program, which executes the WASM init() through the wasm3 interpreter —
// and wasm3 chains its op-codes deep on the C stack. The default 8 KB loopTask
// stack overflows there (stack-canary panic), so give it generous headroom.
SET_LOOP_TASK_STACK_SIZE(32 * 1024);

// Touch sensor (digital, active-HIGH) for hardware power/program control
static const uint8_t TOUCH_PIN = 1;

// ── Crash-loop guard ───────────────────────────────────────────────────────
// If the lamp keeps dying young (crash/watchdog/brownout within the first
// BOOT_GUARD_OK_MS, BOOT_GUARD_LIMIT times in a row), the active program is
// almost certainly the culprit — boot without one so the lamp stays reachable
// over BLE. Only abnormal resets count: unplugging or OTA restarts don't.
#define BOOT_GUARD_PATH   "/boot_guard"
#define BOOT_GUARD_LIMIT  3
#define BOOT_GUARD_OK_MS  20000UL

// A hung WASM program (endless loop in update()) never reboots on its own, so
// the render task is subscribed to the task watchdog: a tick stuck for longer
// than this panics and reboots — which the boot guard above then counts.
#define RENDER_WDT_TIMEOUT_MS 10000UL

static bool g_safeMode = false;         // this boot runs with no active program
static bool g_bootGuardCleared = false; // counter reset after surviving 20 s

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

    // Watchdog: a WASM program stuck in an endless loop hangs this task
    // forever without rebooting — subscribe so a stuck tick panics and the
    // crash-loop guard can eventually boot into safe mode.
    if (esp_task_wdt_add(NULL) == ESP_OK) {
        Serial.println("[MAIN] Render task subscribed to task watchdog");
    }

    // Host-side crossfade on program switch: fade the old program out, swap,
    // then fade the new one in (dip to black). Switch is deferred until fade-out.
    // Fade progress is counted in RENDERED frames, not wall time: the render
    // task can be held for longer than the whole fade (BLE-busy freeze re-armed
    // by notify bursts, flash I/O), and a wall-clock fade collapses into
    // "freeze, then hard cut" — e.g. on playlist rotation, where EVT_PL_ADVANCE
    // and the post-switch param-values push land inside the fade window. A
    // skipped frame must pause the fade, not consume it.
    enum FadeState { FADE_IDLE, FADE_OUT, FADE_IN };
    const uint32_t FADE_FRAMES = 9;        // ~300 ms crossfade at 30 FPS
    const uint32_t BOOT_FADE_FRAMES = 30;  // ~1 s power-on fade-in
    // Boot: start "already faded out" at black, so the first pass through the
    // machine applies any switch queued during setup (e.g. a resumed playlist
    // position) and fades the result in over BOOT_FADE_FRAMES — instead of the
    // program popping to full brightness on the first rendered frame.
    FadeState fadeState = FADE_OUT;
    uint32_t  fadeFrame = FADE_FRAMES;
    uint32_t  fadeInFrames = FADE_FRAMES;  // length of the current fade-in
    bool      bootFade = true;

    while (true) {
        esp_task_wdt_reset();

        // Transmit any queued BLE response from here (render task), NOT from the
        // BLE callback — sending a large multi-chunk reply inline on the BLE
        // stack task starves the notify tx-buffers and the reply stalls after a
        // few chunks. Flushed first so it runs even when paused/powered off.
        bleService->flushPendingResponse();

        // Skip rendering while upload is in progress (LED shows progress bar)
        if (bleService->isPausedByUpload()) {
            vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(33));
            continue;
        }

        // Skip rendering when power is off (LEDs stay dark). The BLE task
        // already cleared the strip in setPower(), but a frame that was
        // mid-render on this task can latch AFTER that clear and stick —
        // so wipe once more from here, the last writer, on the transition.
        static bool wasPoweredOn = true;
        if (!bleService->isPowerOn()) {
            if (wasPoweredOn) {
                ledDriver->clear();
                ledDriver->show();
                wasPoweredOn = false;
            }
            vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(100));
            continue;
        }
        wasPoweredOn = true;

        // NOTE: a BLE-busy render freeze used to live here (hold the last
        // latched frame while isBleBusy(), to keep WS2812 timing away from
        // radio bursts). It never actually cured the flicker, but it did stall
        // animations and crossfades around BLE traffic — disabled for now.

        uint32_t now = millis();

        // A WASM upload just finished: its green bar was faded to black on the
        // BLE task. Apply the queued switch at black and fade the new one in.
        if (bleService->consumeFadeIn()) {
            ledDriver->setFadeScale(0);
            programManager->processPending();
            bleService->notifyActiveProgram(programManager->getActiveId());
            fadeState = FADE_IN;
            fadeFrame = 0;
            fadeInFrames = FADE_FRAMES;
            bootFade = false;
            // processPending() loads the WASM program, which can take a while;
            // refresh `now` so the rotation tick below doesn't see a stale time.
            now = millis();
        }

        // Lamp-driven playlist rotation: applies a pending position and advances
        // when the file's interval elapses. Runs here (render task) so it can
        // request switches; the crossfade machine below picks them up.
        Playlists::tickRotation(programManager, bleService, now);

        // ── Program-switch crossfade state machine ──
        if (fadeState == FADE_IDLE) {
            if (programManager->hasPendingSwitch()) {
                // Begin fading the current program out; apply the switch later.
                fadeState = FADE_OUT;
                fadeFrame = 0;
            } else {
                // No switch pending: run normally, handle deletes + deferred
                // saves. Switches stay queued here (applySwitch=false): a BLE
                // write can land between the check above and this call, and it
                // must fade next frame, not hard-cut.
                ledDriver->setFadeScale(256);
                uint8_t prevActive = programManager->getActiveId();
                programManager->processPending(false);
                uint8_t newActive = programManager->getActiveId();
                if (newActive != prevActive) {
                    bleService->notifyActiveProgram(newActive);
                }
            }
        }

        if (fadeState == FADE_OUT) {
            if (fadeFrame >= FADE_FRAMES) {
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
                fadeFrame = 0;
                fadeInFrames = bootFade ? BOOT_FADE_FRAMES : FADE_FRAMES;
                bootFade = false;
            } else {
                fadeFrame++;
                ledDriver->setFadeScale((uint16_t)(256 - fadeFrame * 256 / FADE_FRAMES)); // 256 -> 0
            }
        } else if (fadeState == FADE_IN) {
            if (fadeFrame >= fadeInFrames) {
                ledDriver->setFadeScale(256);
                fadeState = FADE_IDLE;
            } else {
                fadeFrame++;
                ledDriver->setFadeScale((uint16_t)(fadeFrame * 256 / fadeInFrames));      // 0 -> 256
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

    // Crash-loop guard: count consecutive early deaths (abnormal resets only —
    // unplugging or an OTA restart resets the streak).
    esp_reset_reason_t rr = esp_reset_reason();
    bool abnormalReset = (rr == ESP_RST_PANIC || rr == ESP_RST_INT_WDT ||
                          rr == ESP_RST_TASK_WDT || rr == ESP_RST_WDT ||
                          rr == ESP_RST_BROWNOUT);
    int prevFails = Storage::loadFile(BOOT_GUARD_PATH).toInt();
    int bootFails = abnormalReset ? prevFails + 1 : 0;
    if (bootFails != prevFails) {
        Storage::saveFile(BOOT_GUARD_PATH, String(bootFails).c_str());
    }
    g_bootGuardCleared = (bootFails == 0);  // nothing to clear on a clean boot
    g_safeMode = (bootFails >= BOOT_GUARD_LIMIT);
    if (abnormalReset) {
        Serial.printf("[MAIN] Abnormal reset (reason %d), early-death count: %d%s\r\n",
                      rr, bootFails, g_safeMode ? " -> SAFE MODE" : "");
    }

    // Preload every playlist into the RAM store: from here on all playlist
    // reads (rotation, GETs) are RAM-only; flash is written only to persist.
    Playlists::init();

    // Read hardware config from flash (before creating LedDriver)
    uint8_t ledPin = 48;
    uint16_t ledWidth = 1, ledHeight = 1;
    bool ledZigzag = false;
    uint8_t ledColorOrder = 0; // 0=GRB (default WS2812)
    uint16_t ledRotation = 0;  // wiring orientation: 0/90/180/270 clockwise
    bool ledMirror = false;    //  + optional mirror (for reflected wiring)
    uint32_t ledMaxCurrent = 2000;  // estimated LED draw cap, mA (0 = no limit)
    uint8_t ledBrightness = 255;    // global brightness (255 = full)
    Storage::loadHardwareConfig(ledPin, ledWidth, ledHeight, ledZigzag, ledColorOrder, ledRotation, ledMirror,
                                ledMaxCurrent, ledBrightness);

    // Optional multi-panel layout ("panels" array in config.json). When
    // present it overrides the legacy single-panel pin/zigzag fields.
    LedPanel ledPanels[LED_MAX_PANELS];
    uint8_t ledPanelCount = Storage::loadPanelConfig(ledPanels, LED_MAX_PANELS);

    Serial.println();
    Serial.println("=================================");
    Serial.println("  Shades Lamp v1.0");
    Serial.println("  ESP32-S3 N16R8");
    if (ledPanelCount > 0) {
        Serial.printf("  LED: %ux%u canvas, %u panel(s)\r\n", ledWidth, ledHeight, ledPanelCount);
    } else {
        Serial.printf("  LED: %ux%u on GPIO %u%s\r\n", ledWidth, ledHeight, ledPin, ledZigzag ? " (zigzag)" : "");
    }
    Serial.println("=================================");
    Serial.println();

    // Report memory
    Serial.printf("[MAIN] Free heap: %u bytes\r\n", ESP.getFreeHeap());
    Serial.printf("[MAIN] Free PSRAM: %u bytes\r\n", ESP.getFreePsram());

    // Create and initialize LED driver with config values
    ledDriver = new LedDriver(ledPin, ledWidth, ledHeight, ledZigzag, ledColorOrder, ledRotation, ledMirror);
    if (ledPanelCount > 0) {
        ledDriver->setPanels(ledPanels, ledPanelCount);
    }
    ledDriver->begin();
    ledDriver->setMaxCurrent(ledMaxCurrent);
    ledDriver->setBrightness(ledBrightness);

    // Create engine and manager with dynamic pointers
    wasmEngine = new WasmEngine(ledDriver, &paramStore);
    programManager = new ProgramManager(wasmEngine, &paramStore, ledDriver);
    bleService = new BleService(programManager, ledDriver);

    // Initialize program manager (loads programs from flash, activates saved
    // program — unless the crash-loop guard put us in safe mode).
    programManager->begin(g_safeMode);

    // One-time: stamp stable guids onto legacy playlist positions so they keep
    // resolving after a program is deleted/updated/re-downloaded.
    Playlists::migrateGuids(programManager);

    if (g_safeMode) {
        // Don't resume the playlist either, and forget it persistently — the
        // next (clean) boot must not walk into the same crash.
        Playlists::clearResumeState();
        Serial.println("[MAIN] SAFE MODE: no program activated; pick one via the app");
    } else {
        // Resume a playlist that was rotating before the last reboot (the render
        // task applies the saved position on its first tick).
        Playlists::resumeFromState();
    }

    // Initialize BLE service (use device name from config, default: "Shades LED Lamp")
    bleService->begin(programManager->getDeviceName().c_str());

    // Initialize hardware touch button
    g_touch = new TouchInput(TOUCH_PIN, /*activeLow=*/false);
    g_touch->begin(bleService, programManager);

    // Task watchdog: 10 s and PANIC on trigger (Arduino's default only logs a
    // warning, which would let a hung WASM tick spin forever). Idle tasks are
    // deliberately unwatched — only the render task subscribes, so a starved
    // idle task during BLE bursts can't reboot the lamp.
    esp_task_wdt_config_t wdtCfg = {};
    wdtCfg.timeout_ms = RENDER_WDT_TIMEOUT_MS;
    wdtCfg.idle_core_mask = 0;
    wdtCfg.trigger_panic = true;
    if (esp_task_wdt_reconfigure(&wdtCfg) != ESP_OK && esp_task_wdt_init(&wdtCfg) != ESP_OK) {
        Serial.println("[MAIN] Task WDT setup failed — hang protection inactive");
    }

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

// ── OTA firmware update (over BLE) ───────────────────────────────────────────

// Draw a horizontal progress fill (blue) from the bottom up. Runs while
// rendering is paused (pausedByUpload), so there's no contention for the LEDs.
static void otaProgressBar(size_t cur, size_t total) {
    if (!ledDriver || total == 0) return;
    uint16_t w = ledDriver->getWidth();
    uint16_t h = ledDriver->getHeight();
    uint32_t totalPixels = (uint32_t)w * h;
    // The BLE-receive phase already filled the bottom 90%; the flash-write phase
    // continues the SAME bar through the top 10% so it reads as one sweep.
    uint32_t base = totalPixels * 9 / 10;
    uint32_t filled = base + (uint32_t)((uint64_t)cur * (totalPixels - base) / total);
    ledDriver->clear();
    for (uint32_t i = 0; i < filled && i < totalPixels; i++) {
        ledDriver->setPixel(i % w, i / w, 0, 80, 255);  // blue
    }
    ledDriver->show();
}

// Fade a full blue panel out to black (mirrors the green post-upload fade),
// used as the OTA "done" animation right before reboot.
static void otaFadeOut() {
    if (!ledDriver) return;
    uint16_t w = ledDriver->getWidth();
    uint16_t h = ledDriver->getHeight();
    for (uint16_t y = 0; y < h; y++)
        for (uint16_t x = 0; x < w; x++)
            ledDriver->setPixel(x, y, 0, 80, 255);
    const int STEPS = 40;
    for (int i = STEPS; i >= 0; i--) {
        float t = (float)i / STEPS;                 // 1 -> 0
        ledDriver->setFadeScale((uint16_t)(t * t * 256.0f + 0.5f));
        ledDriver->show();
        delay(11);
    }
    ledDriver->setFadeScale(0);
}

// Flash a firmware image (already received over BLE into PSRAM) into the
// inactive OTA slot, then reboot. Blocking; runs on loop()'s 32 KB stack so it
// can feed the watchdog. The buffer is freed here (ownership transferred from
// BleService). On failure it resumes normal rendering.
static void performBleOta(uint8_t* img, size_t size) {
    Serial.printf("[OTA] Flashing %u bytes from BLE image...\r\n", (unsigned)size);
    ledDriver->setFadeScale(256);

    if (!Update.begin(size, U_FLASH)) {
        Serial.printf("[OTA] Update.begin failed: %s\r\n", Update.errorString());
        free(img);
        bleService->pausedByUpload = false;
        return;
    }

    // Write in blocks, updating the LED bar and feeding the task watchdog.
    const size_t BLOCK = 16 * 1024;
    size_t written = 0;
    bool ok = true;
    while (written < size) {
        size_t n = (size - written < BLOCK) ? (size - written) : BLOCK;
        if (Update.write(img + written, n) != n) {
            Serial.printf("[OTA] Update.write failed at %u: %s\r\n", (unsigned)written, Update.errorString());
            ok = false;
            break;
        }
        written += n;
        otaProgressBar(written, size);
        delay(0);   // yield so the idle task can pet the watchdog
    }

    free(img);

    if (!ok) {
        Update.abort();
        bleService->pausedByUpload = false;
        return;
    }

    if (!Update.end(true)) {   // true = set boot partition; expects full image
        Serial.printf("[OTA] Update.end failed: %s\r\n", Update.errorString());
        bleService->pausedByUpload = false;
        return;
    }

    Serial.println("[OTA] Success — rebooting into new firmware");
    otaFadeOut();        // fade the full blue bar out to black, like the green one
    delay(150);
    ESP.restart();
}

// ── Arduino Loop ───────────────────────────────────────────────────────────

void loop() {
    if (g_touch) g_touch->tick();

    // Crash-loop guard: survived long enough — this boot counts as healthy.
    if (!g_bootGuardCleared && millis() >= BOOT_GUARD_OK_MS) {
        g_bootGuardCleared = true;
        Storage::saveFile(BOOT_GUARD_PATH, "0");
        if (g_safeMode) {
            Serial.println("[MAIN] SAFE MODE boot stable, guard counter reset");
        }
    }

    // Firmware image received over BLE? Flash it here (loop has the large stack).
    if (bleService) {
        uint8_t* otaImg = nullptr;
        size_t   otaSize = 0;
        if (bleService->consumeOtaFlash(&otaImg, &otaSize) && otaImg) {
            performBleOta(otaImg, otaSize);
        }
    }

    // Refresh the advertised chip temperature every ~5 s so scanners see it live.
    static uint32_t lastTempPub = 0;
    uint32_t nowMs = millis();
    if (bleService && (nowMs - lastTempPub) >= 5000) {
        lastTempPub = nowMs;
        bleService->updateAdvertisedTemp();
    }

    vTaskDelay(pdMS_TO_TICKS(5));
}
