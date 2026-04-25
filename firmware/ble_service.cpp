#include "ble_service.h"
#include "program_manager.h"

#define TAG "[BLE]"

// Safe MTU payload size (MTU 20 - 2 bytes header = 18 bytes payload per chunk)
#define CHUNK_HEADER_SIZE 2
#define SAFE_MTU          20
#define CHUNK_PAYLOAD     (SAFE_MTU - CHUNK_HEADER_SIZE)

// Maximum WASM upload size (64 KB)
#define MAX_UPLOAD_SIZE   (64 * 1024)

// Global pointer for BLE callbacks
BleService* g_bleService = nullptr;

// ── BLE Server Callbacks ───────────────────────────────────────────────────

class ServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) override {
        Serial.printf("%s Client connected\r\n", TAG);
    }

    void onDisconnect(BLEServer* pServer) override {
        Serial.printf("%s Client disconnected\r\n", TAG);
        // Restart advertising so new clients can connect
        BLEDevice::startAdvertising();
    }
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

                if (totalSize == 0 || totalSize > MAX_UPLOAD_SIZE) {
                    Serial.printf("%s UPLOAD_START: invalid size %u\r\n", TAG, totalSize);
                    g_bleService->sendResponse("{\"ok\":false,\"err\":\"invalid size\"}", true);
                    break;
                }

                // Free previous upload buffer if any
                if (g_bleService->uploadBuffer) {
                    free(g_bleService->uploadBuffer);
                    g_bleService->uploadBuffer = nullptr;
                }

                // Allocate upload buffer in PSRAM
                g_bleService->uploadBuffer = (uint8_t*)ps_malloc(totalSize);
                if (!g_bleService->uploadBuffer) {
                    g_bleService->uploadBuffer = (uint8_t*)malloc(totalSize);
                }
                if (!g_bleService->uploadBuffer) {
                    Serial.printf("%s UPLOAD_START: alloc failed for %u bytes\r\n", TAG, totalSize);
                    g_bleService->sendResponse("{\"ok\":false,\"err\":\"alloc failed\"}", true);
                    break;
                }

                g_bleService->uploadSize = totalSize;
                g_bleService->uploadOffset = 0;
                g_bleService->uploadInProgress = true;

                Serial.printf("%s UPLOAD_START: %u bytes\r\n", TAG, totalSize);
                g_bleService->sendResponse("{\"ok\":true}");
                break;
            }

            case CMD_UPLOAD_FINISH: {
                if (!g_bleService->uploadInProgress || !g_bleService->uploadBuffer) {
                    g_bleService->sendResponse("{\"ok\":false,\"err\":\"no upload in progress\"}", true);
                    break;
                }

                Serial.printf("%s UPLOAD_FINISH: received %u/%u bytes\r\n", TAG,
                              g_bleService->uploadOffset, g_bleService->uploadSize);

                if (g_bleService->uploadOffset != g_bleService->uploadSize) {
                    Serial.printf("%s UPLOAD_FINISH: incomplete upload\r\n", TAG);
                    free(g_bleService->uploadBuffer);
                    g_bleService->uploadBuffer = nullptr;
                    g_bleService->uploadInProgress = false;
                    g_bleService->sendResponse("{\"ok\":false,\"err\":\"incomplete upload\"}", true);
                    break;
                }

                // Try to upload the program
                int8_t newId = pm->uploadProgram(g_bleService->uploadBuffer, g_bleService->uploadSize);

                free(g_bleService->uploadBuffer);
                g_bleService->uploadBuffer = nullptr;
                g_bleService->uploadInProgress = false;

                if (newId < 0) {
                    g_bleService->sendResponse("{\"ok\":false,\"err\":\"invalid WASM\"}", true);
                } else {
                    char resp[64];
                    snprintf(resp, sizeof(resp), "{\"ok\":true,\"id\":%d}", newId);
                    Serial.printf("%s UPLOAD_FINISH: saved as ID %d\r\n", TAG, newId);
                    g_bleService->sendResponse(String(resp));
                }
                break;
            }

            case CMD_DELETE_PROGRAM: {
                if (payloadLen < 1) {
                    g_bleService->sendResponse("{\"ok\":false,\"err\":\"missing program_id\"}", true);
                    break;
                }
                uint8_t progId = payload[0];
                bool ok = pm->deleteProgram(progId);

                if (ok) {
                    Serial.printf("%s CMD DELETE prog=%u OK\r\n", TAG, progId);
                    g_bleService->sendResponse("{\"ok\":true}");
                    // Notify active program change if it changed
                    g_bleService->notifyActiveProgram(pm->getActiveId());
                } else {
                    Serial.printf("%s CMD DELETE prog=%u FAIL\r\n", TAG, progId);
                    g_bleService->sendResponse("{\"ok\":false,\"err\":\"delete failed\"}", true);
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

        if (pm->switchProgram(progId)) {
            pm->saveState();
            g_bleService->notifyActiveProgram(progId);
        }
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
    }
};

// ── BleService Implementation ──────────────────────────────────────────────

BleService::BleService(ProgramManager* pm)
    : _pm(pm)
    , _server(nullptr)
    , _service(nullptr)
    , _charCommand(nullptr)
    , _charResponse(nullptr)
    , _charActive(nullptr)
    , _charUpload(nullptr)
    , _connectedClients(0)
    , uploadBuffer(nullptr)
    , uploadSize(0)
    , uploadOffset(0)
    , uploadInProgress(false)
{
    _mutex = xSemaphoreCreateMutex();
    g_bleService = this;
}

void BleService::begin() {
    Serial.printf("%s Initializing BLE...\r\n", TAG);

    BLEDevice::init("WasmLED");

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

    _service->start();

    // Start advertising
    BLEAdvertising* advertising = BLEDevice::getAdvertising();
    advertising->addServiceUUID(SERVICE_UUID);
    advertising->setScanResponse(true);
    advertising->setMinPreferred(0x06);  // help with iPhone connection
    advertising->setMinPreferred(0x12);
    BLEDevice::startAdvertising();

    Serial.printf("%s BLE advertising started as 'WasmLED'\r\n", TAG);
}

void BleService::sendResponse(const String& data, bool isError) {
    if (!_charResponse) return;

    const uint8_t* src = (const uint8_t*)data.c_str();
    size_t totalLen = data.length();
    uint8_t seq = 0;

    // Send data in chunks
    size_t offset = 0;
    while (offset < totalLen) {
        size_t remaining = totalLen - offset;
        size_t chunkSize = (remaining > CHUNK_PAYLOAD) ? CHUNK_PAYLOAD : remaining;
        bool isFinal = (offset + chunkSize >= totalLen);

        // Build chunk: [seq][flags][payload...]
        uint8_t chunk[SAFE_MTU];
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
            delay(20);
        }
    }

    // Handle empty response
    if (totalLen == 0) {
        uint8_t chunk[CHUNK_HEADER_SIZE];
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

bool BleService::isConnected() const {
    return _server && _server->getConnectedCount() > 0;
}
