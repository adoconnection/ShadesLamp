#include "ble_service.h"
#include "program_manager.h"
#include "led_driver.h"
#include <LittleFS.h>

#define TAG "[BLE]"

// Chunk header: [seq(1)][flags(1)]
#define CHUNK_HEADER_SIZE 2
// Fallback chunk payload when MTU is unknown (default BLE MTU 23 - 3 ATT - 2 header)
#define MIN_CHUNK_PAYLOAD 18
// Maximum chunk payload — keep conservative to avoid notification truncation
#define MAX_CHUNK_PAYLOAD 200

// Maximum WASM upload size (64 KB)
#define MAX_UPLOAD_SIZE   (64 * 1024)

// Global pointer for BLE callbacks
BleService* g_bleService = nullptr;

// ── BLE Server Callbacks ───────────────────────────────────────────────────

class ServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) override {
        Serial.printf("%s Client connected (total: %d)\r\n", TAG, pServer->getConnectedCount());
        // Keep advertising so additional clients can connect
        BLEDevice::startAdvertising();
    }

    void onDisconnect(BLEServer* pServer) override {
        Serial.printf("%s Client disconnected (remaining: %d)\r\n", TAG, pServer->getConnectedCount());
        // Reset MTU to default when client disconnects
        if (g_bleService && pServer->getConnectedCount() <= 1) {
            g_bleService->setNegotiatedMtu(23);
        }
        // Restart advertising so new clients can connect
        BLEDevice::startAdvertising();
    }

#if defined(CONFIG_BLUEDROID_ENABLED)
    void onMtuChanged(BLEServer* pServer, esp_ble_gatts_cb_param_t* param) override {
        uint16_t mtu = param->mtu.mtu;
        Serial.printf("%s MTU negotiated: %u\r\n", TAG, mtu);
        if (g_bleService) g_bleService->setNegotiatedMtu(mtu);
    }
#elif defined(CONFIG_NIMBLE_ENABLED)
    void onMtuChanged(BLEServer* pServer, ble_gap_conn_desc* desc, uint16_t mtu) override {
        Serial.printf("%s MTU negotiated: %u\r\n", TAG, mtu);
        if (g_bleService) g_bleService->setNegotiatedMtu(mtu);
    }
#endif
};

// ── Command Characteristic Callbacks ───────────────────────────────────────

class CommandCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* pChar) override {
        if (!g_bleService) return;

        String val = pChar->getValue();
        if (val.length() == 0) return;

        uint8_t cmd = (uint8_t)val[0];
        const uint8_t* payload = (const uint8_t*)val.c_str() + 1;
        size_t payloadLen = val.length() - 1;

        ProgramManager* pm = g_bleService->getProgramManager();

        switch (cmd) {
            case CMD_GET_PROGRAMS: {
                String json = pm->getProgramListJson();
                Serial.printf("%s CMD GET_PROGRAMS -> %s\r\n", TAG, json.c_str());
                g_bleService->sendResponse(json);
                break;
            }

            case CMD_GET_PARAMS: {
                if (payloadLen < 1) {
                    g_bleService->sendResponse("{\"ok\":false,\"err\":\"missing program_id\"}", true);
                    break;
                }
                uint8_t progId = payload[0];
                String json = pm->getProgramParamsJson(progId);
                Serial.printf("%s CMD GET_PARAMS[%u] -> %s\r\n", TAG, progId, json.c_str());
                g_bleService->sendResponse(json);
                break;
            }

            case CMD_SET_PARAM: {
                if (payloadLen < 6) { // program_id(1) + param_id(1) + value(4)
                    g_bleService->sendResponse("{\"ok\":false,\"err\":\"invalid payload\"}", true);
                    break;
                }
                uint8_t progId = payload[0];
                uint8_t paramId = payload[1];
                const uint8_t* value = payload + 2;

                bool ok = pm->setParam(progId, paramId, value, 4);
                if (ok) {
                    Serial.printf("%s CMD SET_PARAM prog=%u param=%u OK\r\n", TAG, progId, paramId);
                    g_bleService->sendResponse("{\"ok\":true}");
                } else {
                    Serial.printf("%s CMD SET_PARAM prog=%u param=%u FAIL\r\n", TAG, progId, paramId);
                    g_bleService->sendResponse("{\"ok\":false,\"err\":\"set_param failed\"}", true);
                }
                break;
            }

            case CMD_GET_PARAM_VALUES: {
                if (payloadLen < 1) {
                    g_bleService->sendResponse("{\"ok\":false,\"err\":\"missing program_id\"}", true);
                    break;
                }
                uint8_t progId = payload[0];
                String json = pm->getParamValuesJson(progId);
                Serial.printf("%s CMD GET_PARAM_VALUES[%u] -> %s\r\n", TAG, progId, json.c_str());
                g_bleService->sendResponse(json);
                break;
            }

            case CMD_UPLOAD_START: {
                if (payloadLen < 4) {
                    g_bleService->sendResponse("{\"ok\":false,\"err\":\"missing size\"}", true);
                    break;
                }
                uint32_t totalSize = 0;
                memcpy(&totalSize, payload, 4); // little-endian

                // Optional: type(1) + progId(1) after size
                // type: 0=WASM (default), 1=META
                uint8_t upType = (payloadLen >= 5) ? payload[4] : 0;
                uint8_t metaProgId = (payloadLen >= 6) ? payload[5] : 0;

                uint32_t maxSize = (upType == 1) ? 8192 : MAX_UPLOAD_SIZE;
                if (totalSize == 0 || totalSize > maxSize) {
                    Serial.printf("%s UPLOAD_START: invalid size %u (type=%u)\r\n", TAG, totalSize, upType);
                    g_bleService->sendResponse("{\"ok\":false,\"err\":\"invalid size\"}", true);
                    break;
                }

                // Free previous upload buffer if any
                if (g_bleService->uploadBuffer) {
                    free(g_bleService->uploadBuffer);
                    g_bleService->uploadBuffer = nullptr;
                }

                // Allocate upload buffer (PSRAM for WASM, regular for small meta)
                if (upType == 1) {
                    g_bleService->uploadBuffer = (uint8_t*)malloc(totalSize);
                } else {
                    g_bleService->uploadBuffer = (uint8_t*)ps_malloc(totalSize);
                    if (!g_bleService->uploadBuffer) {
                        g_bleService->uploadBuffer = (uint8_t*)malloc(totalSize);
                    }
                }
                if (!g_bleService->uploadBuffer) {
                    Serial.printf("%s UPLOAD_START: alloc failed for %u bytes\r\n", TAG, totalSize);
                    g_bleService->sendResponse("{\"ok\":false,\"err\":\"alloc failed\"}", true);
                    break;
                }

                g_bleService->uploadSize = totalSize;
                g_bleService->uploadOffset = 0;
                g_bleService->uploadInProgress = true;
                g_bleService->uploadType = upType;
                g_bleService->uploadMetaProgId = metaProgId;

                // Pause rendering and show progress only for WASM uploads
                if (upType == 0) {
                    g_bleService->pausedByUpload = true;
                    LedDriver* led = g_bleService->getLedDriver();
                    if (led) {
                        led->clear();
                        led->show();
                    }
                }

                Serial.printf("%s UPLOAD_START: %u bytes, type=%u\r\n", TAG, totalSize, upType);
                g_bleService->sendResponse("{\"ok\":true}");
                break;
            }

            case CMD_UPLOAD_FINISH: {
                if (!g_bleService->uploadInProgress || !g_bleService->uploadBuffer) {
                    g_bleService->sendResponse("{\"ok\":false,\"err\":\"no upload in progress\"}", true);
                    break;
                }

                uint8_t upType = g_bleService->uploadType;
                Serial.printf("%s UPLOAD_FINISH: received %u/%u bytes (type=%u)\r\n", TAG,
                              g_bleService->uploadOffset, g_bleService->uploadSize, upType);

                if (g_bleService->uploadOffset != g_bleService->uploadSize) {
                    Serial.printf("%s UPLOAD_FINISH: incomplete upload\r\n", TAG);
                    free(g_bleService->uploadBuffer);
                    g_bleService->uploadBuffer = nullptr;
                    g_bleService->uploadInProgress = false;
                    g_bleService->pausedByUpload = false;
                    g_bleService->sendResponse("{\"ok\":false,\"err\":\"incomplete upload\"}", true);
                    break;
                }

                if (upType == 1) {
                    // ── META upload ──
                    uint8_t progId = g_bleService->uploadMetaProgId;
                    String json((const char*)g_bleService->uploadBuffer, g_bleService->uploadSize);

                    free(g_bleService->uploadBuffer);
                    g_bleService->uploadBuffer = nullptr;
                    g_bleService->uploadInProgress = false;

                    bool ok = pm->setProgramMeta(progId, json);
                    if (ok) {
                        Serial.printf("%s META_FINISH[%u] OK (%u bytes)\r\n", TAG, progId, json.length());
                        g_bleService->sendResponse("{\"ok\":true}");
                    } else {
                        g_bleService->sendResponse("{\"ok\":false,\"err\":\"set_meta failed\"}", true);
                    }
                } else {
                    // ── WASM upload ──
                    // Flash all green briefly to indicate success before processing
                    {
                        LedDriver* led = g_bleService->getLedDriver();
                        if (led) {
                            uint16_t w = led->getWidth();
                            uint16_t h = led->getHeight();
                            for (uint16_t y = 0; y < h; y++)
                                for (uint16_t x = 0; x < w; x++)
                                    led->setPixel(x, y, 0, 255, 0);
                            led->show();
                        }
                    }

                    int8_t newId = pm->uploadProgram(g_bleService->uploadBuffer, g_bleService->uploadSize);

                    free(g_bleService->uploadBuffer);
                    g_bleService->uploadBuffer = nullptr;
                    g_bleService->uploadInProgress = false;
                    g_bleService->pausedByUpload = false;

                    if (newId < 0) {
                        g_bleService->sendResponse("{\"ok\":false,\"err\":\"invalid WASM\"}", true);
                    } else {
                        char resp[64];
                        snprintf(resp, sizeof(resp), "{\"ok\":true,\"id\":%d}", newId);
                        Serial.printf("%s UPLOAD_FINISH: saved as ID %d\r\n", TAG, newId);
                        g_bleService->sendResponse(String(resp));
                        g_bleService->queueEvent(EVT_PROGRAM_ADDED, (uint8_t)newId);
                    }
                }
                break;
            }

            case CMD_DELETE_PROGRAM: {
                if (payloadLen < 1) {
                    g_bleService->sendResponse("{\"ok\":false,\"err\":\"missing program_id\"}", true);
                    break;
                }
                uint8_t progId = payload[0];
                // Defer to render task to avoid race with WASM tick() on Core 1
                Serial.printf("%s CMD DELETE prog=%u (deferred)\r\n", TAG, progId);
                pm->requestDelete(progId);
                g_bleService->sendResponse("{\"ok\":true}");
                g_bleService->queueEvent(EVT_PROGRAM_DELETED, progId);
                break;
            }

            case CMD_SET_NAME: {
                if (payloadLen < 1 || payloadLen > 20) {
                    g_bleService->sendResponse("{\"ok\":false,\"err\":\"name 1-20 chars\"}", true);
                    break;
                }
                String newName((const char*)payload, payloadLen);
                Serial.printf("%s CMD SET_NAME: '%s'\r\n", TAG, newName.c_str());

                pm->setDeviceName(newName);

                Serial.printf("%s Name saved. Will apply after reboot.\r\n", TAG);
                g_bleService->sendResponse("{\"ok\":true,\"reboot\":true}");
                break;
            }

            case CMD_GET_NAME: {
                String name = pm->getDeviceName();
                String resp = "{\"ok\":true,\"name\":\"" + name + "\"}";
                Serial.printf("%s CMD GET_NAME: '%s'\r\n", TAG, name.c_str());
                g_bleService->sendResponse(resp);
                break;
            }

            case CMD_GET_HW_CONFIG: {
                // Get chip serial from eFuse MAC
                uint64_t mac = ESP.getEfuseMac();
                char serial[16];
                snprintf(serial, sizeof(serial), "%04X%08X",
                    (uint16_t)(mac >> 32), (uint32_t)mac);

                static const char* ORDER_NAMES[] = {"GRB","RGB","BRG","RBG","GBR","BGR"};
                uint8_t order = pm->getLedColorOrder();
                const char* orderName = (order < 6) ? ORDER_NAMES[order] : "GRB";

                char resp[224];
                snprintf(resp, sizeof(resp),
                    "{\"ok\":true,\"pin\":%u,\"width\":%u,\"height\":%u,\"zigzag\":%s,\"colorOrder\":%u,\"colorOrderName\":\"%s\",\"serial\":\"%s\"}",
                    pm->getLedPin(), pm->getLedWidth(), pm->getLedHeight(),
                    pm->getLedZigzag() ? "true" : "false", order, orderName, serial);
                Serial.printf("%s CMD GET_HW_CONFIG: %s\r\n", TAG, resp);
                g_bleService->sendResponse(String(resp));
                break;
            }

            case CMD_SET_HW_CONFIG: {
                if (payloadLen < 6) {
                    g_bleService->sendResponse("{\"ok\":false,\"err\":\"need 6+ bytes: pin(1)+width(2)+height(2)+zigzag(1)+[colorOrder(1)]\"}", true);
                    break;
                }
                uint8_t pin = payload[0];
                uint16_t w, h;
                memcpy(&w, payload + 1, 2);
                memcpy(&h, payload + 3, 2);
                bool zigzag = payload[5] != 0;
                uint8_t colorOrder = (payloadLen >= 7) ? payload[6] : pm->getLedColorOrder();

                if (pin > 48 || w == 0 || w > 1024 || h == 0 || h > 1024) {
                    g_bleService->sendResponse("{\"ok\":false,\"err\":\"invalid values\"}", true);
                    break;
                }
                if (colorOrder >= 6) colorOrder = 0;

                Serial.printf("%s CMD SET_HW_CONFIG: pin=%u, %ux%u, zigzag=%d, order=%u\r\n", TAG, pin, w, h, zigzag, colorOrder);
                pm->setHardwareConfig(pin, w, h, zigzag, colorOrder);
                g_bleService->sendResponse("{\"ok\":true,\"reboot\":true}");
                break;
            }

            case CMD_GET_META: {
                if (payloadLen < 1) {
                    g_bleService->sendResponse("{\"ok\":false,\"err\":\"missing program_id\"}", true);
                    break;
                }
                uint8_t progId = payload[0];
                String json = pm->getProgramMeta(progId);
                Serial.printf("%s CMD GET_META[%u] -> %u bytes\r\n", TAG, progId, json.length());
                g_bleService->sendResponse(json);
                break;
            }

            case CMD_SET_META: {
                if (payloadLen < 2) {
                    g_bleService->sendResponse("{\"ok\":false,\"err\":\"missing data\"}", true);
                    break;
                }
                uint8_t progId = payload[0];
                String json((const char*)(payload + 1), payloadLen - 1);

                if (json.length() > 2048) {
                    g_bleService->sendResponse("{\"ok\":false,\"err\":\"meta too large\"}", true);
                    break;
                }

                bool ok = pm->setProgramMeta(progId, json);
                if (ok) {
                    Serial.printf("%s CMD SET_META[%u] OK (%u bytes)\r\n", TAG, progId, json.length());
                    g_bleService->sendResponse("{\"ok\":true}");
                } else {
                    g_bleService->sendResponse("{\"ok\":false,\"err\":\"set_meta failed\"}", true);
                }
                break;
            }

            case CMD_REBOOT: {
                Serial.printf("%s CMD REBOOT\r\n", TAG);
                g_bleService->sendResponse("{\"ok\":true}");
                delay(500);
                ESP.restart();
                break;
            }

            case CMD_GET_POWER: {
                bool on = g_bleService->isPowerOn();
                char buf[32];
                snprintf(buf, sizeof(buf), "{\"ok\":true,\"power\":%s}", on ? "true" : "false");
                g_bleService->sendResponse(buf);
                break;
            }

            case CMD_SET_POWER: {
                if (payloadLen < 1) {
                    g_bleService->sendResponse("{\"ok\":false,\"err\":\"missing payload\"}", true);
                    break;
                }
                bool on = payload[0] != 0;
                g_bleService->setPower(on);
                Serial.printf("%s Power %s\r\n", TAG, on ? "ON" : "OFF");
                char buf[32];
                snprintf(buf, sizeof(buf), "{\"ok\":true,\"power\":%s}", on ? "true" : "false");
                g_bleService->sendResponse(buf);
                break;
            }

            case CMD_GET_STORAGE: {
                size_t total = LittleFS.totalBytes();
                size_t used = LittleFS.usedBytes();
                char resp[96];
                snprintf(resp, sizeof(resp),
                    "{\"ok\":true,\"used\":%u,\"total\":%u,\"free\":%u}",
                    (unsigned)used, (unsigned)total, (unsigned)(total - used));
                Serial.printf("%s CMD GET_STORAGE: %s\r\n", TAG, resp);
                g_bleService->sendResponse(String(resp));
                break;
            }

            case CMD_GET_ORDER: {
                String order = pm->getOrderJson();
                Serial.printf("%s CMD GET_ORDER: %s\r\n", TAG, order.c_str());
                g_bleService->sendResponse(order);
                break;
            }

            case CMD_SET_ORDER: {
                // Payload: JSON array of program IDs
                String json((const char*)payload, payloadLen);
                Serial.printf("%s CMD SET_ORDER: %s\r\n", TAG, json.c_str());
                if (pm->setOrder(json)) {
                    g_bleService->sendResponse("{\"ok\":true}");
                } else {
                    g_bleService->sendResponse("{\"ok\":false,\"err\":\"invalid order\"}", true);
                }
                break;
            }

            default:
                Serial.printf("%s Unknown command: 0x%02X\r\n", TAG, cmd);
                g_bleService->sendResponse("{\"ok\":false,\"err\":\"unknown command\"}", true);
                break;
        }
    }
};

// ── Active Program Characteristic Callbacks ────────────────────────────────

class ActiveProgramCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* pChar) override {
        if (!g_bleService) return;

        String val = pChar->getValue();
        if (val.length() < 1) return;

        uint8_t progId = (uint8_t)val[0];
        ProgramManager* pm = g_bleService->getProgramManager();

        Serial.printf("%s Active program write: %u\r\n", TAG, progId);

        // Queue async switch (processed in loop, BLE callback returns fast)
        pm->requestSwitch(progId);
    }

    void onRead(BLECharacteristic* pChar) override {
        if (!g_bleService) return;
        uint8_t activeId = g_bleService->getProgramManager()->getActiveId();
        pChar->setValue(&activeId, 1);
    }
};

// ── Upload Characteristic Callbacks ────────────────────────────────────────

class UploadCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* pChar) override {
        if (!g_bleService || !g_bleService->uploadInProgress || !g_bleService->uploadBuffer) {
            return;
        }

        String val = pChar->getValue();
        size_t chunkLen = val.length();
        if (chunkLen == 0) return;

        uint32_t remaining = g_bleService->uploadSize - g_bleService->uploadOffset;
        size_t toCopy = (chunkLen > remaining) ? remaining : chunkLen;

        memcpy(g_bleService->uploadBuffer + g_bleService->uploadOffset, val.c_str(), toCopy);
        g_bleService->uploadOffset += toCopy;

        // Log progress every 1KB
        if ((g_bleService->uploadOffset % 1024) < toCopy || g_bleService->uploadOffset == g_bleService->uploadSize) {
            Serial.printf("%s Upload: %u/%u bytes\r\n", TAG,
                          g_bleService->uploadOffset, g_bleService->uploadSize);
        }

        // Update LED progress bar (green fill from bottom to top)
        LedDriver* led = g_bleService->getLedDriver();
        if (led && g_bleService->uploadSize > 0) {
            uint16_t w = led->getWidth();
            uint16_t h = led->getHeight();
            uint32_t totalPixels = (uint32_t)w * h;
            uint32_t filledPixels = (uint32_t)((uint64_t)g_bleService->uploadOffset * totalPixels / g_bleService->uploadSize);

            led->clear();
            // Fill from bottom (y=0) upwards
            for (uint32_t i = 0; i < filledPixels && i < totalPixels; i++) {
                uint16_t x = i % w;
                uint16_t y = i / w;
                led->setPixel(x, y, 0, 255, 0);
            }
            led->show();
        }
    }
};

// ── BleService Implementation ──────────────────────────────────────────────

BleService::BleService(ProgramManager* pm, LedDriver* led)
    : _pm(pm)
    , _led(led)
    , _server(nullptr)
    , _service(nullptr)
    , _charCommand(nullptr)
    , _charResponse(nullptr)
    , _charActive(nullptr)
    , _charUpload(nullptr)
    , _charParamValues(nullptr)
    , _charEvents(nullptr)
    , _connectedClients(0)
    , _pendingEventCount(0)
    , _negotiatedMtu(23)
    , pausedByUpload(false)
    , powerOn(true)
    , uploadBuffer(nullptr)
    , uploadSize(0)
    , uploadOffset(0)
    , uploadInProgress(false)
    , uploadType(0)
    , uploadMetaProgId(0)
{
    _mutex = xSemaphoreCreateMutex();
    g_bleService = this;
}

void BleService::begin(const char* deviceName) {
    Serial.printf("%s Initializing BLE as '%s'...\r\n", TAG, deviceName);

    BLEDevice::init(deviceName);
    BLEDevice::setMTU(517);

    _server = BLEDevice::createServer();
    _server->setCallbacks(new ServerCallbacks());

    _service = _server->createService(SERVICE_UUID);

    // Command characteristic (WRITE)
    _charCommand = _service->createCharacteristic(
        CHAR_COMMAND_UUID,
        BLECharacteristic::PROPERTY_WRITE
    );
    _charCommand->setCallbacks(new CommandCallbacks());

    // Response characteristic (NOTIFY)
    _charResponse = _service->createCharacteristic(
        CHAR_RESPONSE_UUID,
        BLECharacteristic::PROPERTY_NOTIFY
    );
    _charResponse->addDescriptor(new BLE2902());

    // Active Program characteristic (READ | WRITE | NOTIFY)
    _charActive = _service->createCharacteristic(
        CHAR_ACTIVE_UUID,
        BLECharacteristic::PROPERTY_READ |
        BLECharacteristic::PROPERTY_WRITE |
        BLECharacteristic::PROPERTY_NOTIFY
    );
    _charActive->setCallbacks(new ActiveProgramCallbacks());
    _charActive->addDescriptor(new BLE2902());
    uint8_t activeId = _pm->getActiveId();
    _charActive->setValue(&activeId, 1);

    // Upload characteristic (WRITE_NR — no response for fast transfer)
    _charUpload = _service->createCharacteristic(
        CHAR_UPLOAD_UUID,
        BLECharacteristic::PROPERTY_WRITE_NR
    );
    _charUpload->setCallbacks(new UploadCallbacks());

    // Param Values characteristic (NOTIFY — device pushes param changes to clients)
    _charParamValues = _service->createCharacteristic(
        CHAR_PARAM_VALUES_UUID,
        BLECharacteristic::PROPERTY_NOTIFY
    );
    _charParamValues->addDescriptor(new BLE2902());

    // Events characteristic (NOTIFY — push program added/deleted events)
    _charEvents = _service->createCharacteristic(
        CHAR_EVENTS_UUID,
        BLECharacteristic::PROPERTY_NOTIFY
    );
    _charEvents->addDescriptor(new BLE2902());

    _service->start();

    // Start advertising
    BLEAdvertising* advertising = BLEDevice::getAdvertising();
    advertising->addServiceUUID(SERVICE_UUID);
    advertising->setScanResponse(true);
    advertising->setMinPreferred(0x06);  // help with iPhone connection
    advertising->setMinPreferred(0x12);
    BLEDevice::startAdvertising();

    Serial.printf("%s BLE advertising started as '%s'\r\n", TAG, deviceName);
}

uint16_t BleService::getChunkPayload() const {
    if (_negotiatedMtu <= 3 + CHUNK_HEADER_SIZE) return MIN_CHUNK_PAYLOAD;
    uint16_t payload = _negotiatedMtu - 3 - CHUNK_HEADER_SIZE; // 3 = ATT overhead
    if (payload > MAX_CHUNK_PAYLOAD) payload = MAX_CHUNK_PAYLOAD;
    return payload;
}

void BleService::sendResponse(const String& data, bool isError) {
    if (!_charResponse) return;

    const uint8_t* src = (const uint8_t*)data.c_str();
    size_t totalLen = data.length();
    uint8_t seq = 0;
    uint16_t chunkPayload = getChunkPayload();

    Serial.printf("%s sendResponse: %u bytes, chunk=%u (MTU=%u)\r\n",
                  TAG, totalLen, chunkPayload, _negotiatedMtu);

    // Send data in chunks
    uint8_t chunk[MAX_CHUNK_PAYLOAD + CHUNK_HEADER_SIZE];
    size_t offset = 0;
    while (offset < totalLen) {
        size_t remaining = totalLen - offset;
        size_t chunkSize = (remaining > chunkPayload) ? chunkPayload : remaining;
        bool isFinal = (offset + chunkSize >= totalLen);

        // Build chunk: [seq][flags][payload...]
        chunk[0] = seq;
        chunk[1] = 0;
        if (isFinal) chunk[1] |= CHUNK_FLAG_FINAL;
        if (isError) chunk[1] |= CHUNK_FLAG_ERROR;

        memcpy(chunk + CHUNK_HEADER_SIZE, src + offset, chunkSize);

        _charResponse->setValue(chunk, CHUNK_HEADER_SIZE + chunkSize);
        _charResponse->notify();

        offset += chunkSize;
        seq++;

        // Small delay between chunks to avoid BLE stack congestion
        if (!isFinal) {
            delay(30);
        }
    }

    // Handle empty response
    if (totalLen == 0) {
        chunk[0] = 0; // seq
        chunk[1] = CHUNK_FLAG_FINAL;
        if (isError) chunk[1] |= CHUNK_FLAG_ERROR;
        _charResponse->setValue(chunk, CHUNK_HEADER_SIZE);
        _charResponse->notify();
    }
}

void BleService::notifyActiveProgram(uint8_t id) {
    if (_charActive) {
        _charActive->setValue(&id, 1);
        _charActive->notify();
    }
}

void BleService::notifyEvent(uint8_t eventType, uint8_t programId) {
    if (_charEvents && _server && _server->getConnectedCount() > 0) {
        uint8_t buf[2] = { eventType, programId };
        _charEvents->setValue(buf, 2);
        _charEvents->notify();
        Serial.printf("%s notifyEvent: type=0x%02X id=%u\r\n", TAG, eventType, programId);
    }
}

void BleService::queueEvent(uint8_t eventType, uint8_t programId) {
    if (_pendingEventCount < MAX_PENDING_EVENTS) {
        int idx = _pendingEventCount;
        _pendingEvents[idx].type = eventType;
        _pendingEvents[idx].id = programId;
        _pendingEventCount = idx + 1;
    }
}

void BleService::processPendingEvents() {
    int count = _pendingEventCount;
    if (count == 0) return;
    _pendingEventCount = 0;

    for (int i = 0; i < count; i++) {
        uint8_t type = _pendingEvents[i].type;
        uint8_t id = _pendingEvents[i].id;
        notifyEvent(type, id);
        // After delete, also notify active program change
        if (type == EVT_PROGRAM_DELETED) {
            notifyActiveProgram(_pm->getActiveId());
        }
    }
}

void BleService::notifyParamValues() {
    if (!_charParamValues || !_server || _server->getConnectedCount() == 0) return;

    uint8_t activeId = _pm->getActiveId();
    String json = _pm->getParamValuesJson(activeId);

    const uint8_t* src = (const uint8_t*)json.c_str();
    size_t totalLen = json.length();
    uint8_t seq = 0;
    uint16_t chunkPayload = getChunkPayload();

    uint8_t chunk[MAX_CHUNK_PAYLOAD + CHUNK_HEADER_SIZE];
    size_t offset = 0;
    while (offset < totalLen) {
        size_t remaining = totalLen - offset;
        size_t chunkSize = (remaining > chunkPayload) ? chunkPayload : remaining;
        bool isFinal = (offset + chunkSize >= totalLen);

        chunk[0] = seq;
        chunk[1] = isFinal ? CHUNK_FLAG_FINAL : 0;

        memcpy(chunk + CHUNK_HEADER_SIZE, src + offset, chunkSize);

        _charParamValues->setValue(chunk, CHUNK_HEADER_SIZE + chunkSize);
        _charParamValues->notify();

        offset += chunkSize;
        seq++;

        if (!isFinal) {
            delay(30);
        }
    }

    if (totalLen == 0) {
        chunk[0] = 0;
        chunk[1] = CHUNK_FLAG_FINAL;
        _charParamValues->setValue(chunk, CHUNK_HEADER_SIZE);
        _charParamValues->notify();
    }
}

bool BleService::isConnected() const {
    return _server && _server->getConnectedCount() > 0;
}

void BleService::setPower(bool on) {
    powerOn = on;
    if (!on && _led) {
        _led->clear();
        _led->show();
    }
}
