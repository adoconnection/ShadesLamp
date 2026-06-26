#ifndef BLE_SERVICE_H
#define BLE_SERVICE_H

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// Forward declarations
class ProgramManager;
class LedDriver;

// BLE UUIDs
#define SERVICE_UUID        "0000ff00-0000-1000-8000-00805f9b34fb"
#define CHAR_COMMAND_UUID   "0000ff01-0000-1000-8000-00805f9b34fb"
#define CHAR_RESPONSE_UUID  "0000ff02-0000-1000-8000-00805f9b34fb"
#define CHAR_ACTIVE_UUID    "0000ff03-0000-1000-8000-00805f9b34fb"
#define CHAR_UPLOAD_UUID    "0000ff04-0000-1000-8000-00805f9b34fb"
#define CHAR_PARAM_VALUES_UUID "0000ff05-0000-1000-8000-00805f9b34fb"
#define CHAR_EVENTS_UUID       "0000ff06-0000-1000-8000-00805f9b34fb"

// Event types for CHAR_EVENTS notifications
#define EVT_PROGRAM_ADDED   0x01
#define EVT_PROGRAM_DELETED 0x02
#define EVT_PL_ADVANCE      0x03   // lamp auto-advanced a playlist; byte = new index
#define EVT_PL_STOPPED      0x04   // lamp left playlist mode (e.g. manual/touch switch)

// Command codes
#define CMD_GET_PROGRAMS    0x01
#define CMD_GET_PARAMS      0x02
#define CMD_SET_PARAM       0x03
#define CMD_GET_PARAM_VALUES 0x04
#define CMD_UPLOAD_START    0x10
#define CMD_UPLOAD_FINISH   0x11
#define CMD_DELETE_PROGRAM  0x12
#define CMD_SET_NAME        0x20
#define CMD_GET_NAME        0x21
#define CMD_GET_HW_CONFIG   0x22
#define CMD_SET_HW_CONFIG   0x23
#define CMD_REBOOT          0x24
#define CMD_GET_META        0x25
#define CMD_SET_META        0x26
#define CMD_GET_POWER       0x27
#define CMD_SET_POWER       0x28
#define CMD_GET_STORAGE     0x29
#define CMD_SET_ORDER       0x2A
#define CMD_GET_ORDER       0x2B
#define CMD_CLEAR_STORAGE   0x2C   // wipe all programs (keeps device config)
#define CMD_GET_FILE        0x2D   // read a file from flash: payload = path string
#define CMD_LIST_FILES      0x2E   // list a directory: payload = path; returns JSON array
#define CMD_WRITE_FILE      0x2F   // write a file: payload = pathLen(1)+path+data
#define CMD_DELETE_FILE     0x30   // delete a file or directory: payload = path
#define CMD_APPEND_FILE     0x31   // append to a file: payload = pathLen(1)+path+data

// Playlist management (firmware owns /playlists/{id}.json)
#define CMD_PL_LIST         0x32   // -> JSON [{id,name,mode,interval,count}]
#define CMD_PL_GET          0x33   // id(1) -> full playlist JSON
#define CMD_PL_CREATE       0x34   // name(string) -> {ok,id}
#define CMD_PL_RENAME       0x35   // id(1)+name -> {ok}
#define CMD_PL_DELETE       0x36   // id(1) -> {ok}
#define CMD_PL_SET_ROTATION 0x37   // id(1)+mode(1)+interval(2 LE) -> {ok}
#define CMD_PL_ADD_POS      0x38   // id(1)+position JSON -> {ok,index}
#define CMD_PL_DEL_POS      0x39   // id(1)+index(1) -> {ok}
#define CMD_PL_REORDER      0x3A   // id(1)+JSON array of indices -> {ok}
#define CMD_APPLY_POS       0x3B   // progId(1)+params JSON [{id,value,f}] -> switch w/ crossfade + params
#define CMD_PL_PLAY         0x3C   // id(1)[+index(1)] -> {ok,index}; lamp starts rotating
#define CMD_PL_STOP         0x3D   // -> {ok}; lamp stops rotating (current program stays)
#define CMD_PL_STATE        0x3E   // -> {playing:id|-1,index}; current rotation state
// Firmware OTA is streamed over the existing upload pipeline (UPLOAD_START with
// type=2), not a dedicated command.

// Response chunk flags
#define CHUNK_FLAG_FINAL    0x01
#define CHUNK_FLAG_ERROR    0x02

class BleService {
public:
    BleService(ProgramManager* pm, LedDriver* led = nullptr);

    // Initialize BLE stack, create service and characteristics, start advertising
    void begin(const char* deviceName);

    // Queue a chunked response on the Response characteristic. Safe to call
    // from BLE command callbacks: it does NOT transmit inline (that would stall
    // the BLE stack task and drop notifications), it hands the data to the
    // render task which transmits it via flushPendingResponse().
    void sendResponse(const String& data, bool isError = false);

    // Binary-safe queued response (handles embedded NULs, e.g. WASM files)
    void sendRawResponse(const uint8_t* data, size_t len, bool isError = false);

    // Transmit any queued response. MUST be called from the render task (not a
    // BLE callback) so the BLE stack can drain notification tx-buffers.
    void flushPendingResponse();

    // Ask the render task to reboot right after the current response is sent.
    void rebootAfterResponse() { _rebootAfterTx = true; }

    // Update the Active Program characteristic value and notify
    void notifyActiveProgram(uint8_t id);

    // Notify all clients with current parameter values JSON (chunked)
    void notifyParamValues();

    // Notify clients about program list changes (added/deleted)
    // Called from render task (large stack), NOT from BLE callbacks
    void notifyEvent(uint8_t eventType, uint8_t programId);

    // Queue a deferred event (safe to call from BLE callbacks on small stack)
    void queueEvent(uint8_t eventType, uint8_t programId);

    // Process queued events — call from render task
    void processPendingEvents();

    // Check if a client is connected
    bool isConnected() const;

    // Upload progress: pause/resume rendering
    bool isPausedByUpload() const { return pausedByUpload; }

    // Render-freeze during BLE bursts. Refreshing the WS2812 strip (show()
    // disables interrupts for the bit-stream) while the radio is busy causes
    // visible flicker/tearing, so the render task holds the last latched frame
    // until BLE has been quiet for BLE_QUIET_MS.
    static const uint32_t BLE_QUIET_MS = 120;
    void markBleActivity() { _lastBleActivityMs = millis(); }
    bool isBleBusy() const { return (uint32_t)(millis() - _lastBleActivityMs) < BLE_QUIET_MS; }

    // After a WASM upload, the green progress bar fades out on the BLE task;
    // this asks the render task to fade the freshly uploaded program in.
    void requestFadeIn() { _fadeInRequest = true; }
    bool consumeFadeIn() { if (_fadeInRequest) { _fadeInRequest = false; return true; } return false; }

    // Refresh the advertised chip temperature (manufacturer data in the scan
    // response) so scanners can read it without connecting. Call periodically.
    void updateAdvertisedTemp();

    // Power on/off (LEDs off but BLE still active)
    bool isPowerOn() const { return powerOn; }
    void setPower(bool on);

    // Firmware OTA over BLE. The full image is streamed into a PSRAM buffer via
    // the upload pipeline (type=2); UPLOAD_FINISH hands it off here, and loop()
    // (large stack, can feed the watchdog) flashes it into the inactive OTA slot
    // and reboots. Ownership of `img` transfers to the consumer, which frees it.
    void requestOtaFlash(uint8_t* img, size_t size) {
        _otaImage = img; _otaImageSize = size; _otaFlashPending = true;
    }
    bool consumeOtaFlash(uint8_t** img, size_t* size) {
        if (!_otaFlashPending) return false;
        *img = _otaImage; *size = _otaImageSize;
        _otaImage = nullptr; _otaImageSize = 0;
        _otaFlashPending = false;
        return true;
    }

    ProgramManager* getProgramManager() const { return _pm; }
    LedDriver* getLedDriver() const { return _led; }

    // Called when BLE MTU is negotiated with a client
    void setNegotiatedMtu(uint16_t mtu) { _negotiatedMtu = mtu; }

    // Upload state
    uint8_t*  uploadBuffer;
    uint32_t  uploadSize;
    uint32_t  uploadOffset;
    bool      uploadInProgress;
    uint8_t   uploadType;         // 0=WASM, 1=META, 2=FIRMWARE
    uint8_t   uploadMetaProgId;   // program ID for META upload
    uint32_t  uploadLastFilledPixels; // throttle progress redraw to pixel changes
    volatile bool pausedByUpload;
    volatile bool powerOn;

private:
    uint16_t getChunkPayload() const;

    // Actual chunked notify on _charResponse. Private: only flushPendingResponse
    // (render task) may call it, so the BLE stack task stays free to drain.
    void transmitRaw(const uint8_t* data, size_t len, bool isError);

    // Deferred response slot (commands are serialized, so one slot suffices)
    uint8_t*       _txBuf = nullptr;
    size_t         _txLen = 0;
    bool           _txErr = false;
    volatile bool  _txPending = false;
    volatile bool  _rebootAfterTx = false;

    ProgramManager* _pm;
    LedDriver*      _led;

    BLEServer*          _server;
    BLEService*         _service;
    BLECharacteristic*  _charCommand;
    BLECharacteristic*  _charResponse;
    BLECharacteristic*  _charActive;
    BLECharacteristic*  _charUpload;
    BLECharacteristic*  _charParamValues;
    BLECharacteristic*  _charEvents;

    uint16_t _connectedClients;
    uint16_t _negotiatedMtu;

    volatile uint32_t _lastBleActivityMs;   // millis() of last BLE RX/TX
    volatile bool      _fadeInRequest;       // render task should fade new program in
    String             _deviceName;          // for rebuilding advertising payload

    // OTA flash request (set on BLE task at UPLOAD_FINISH, consumed by loop())
    volatile bool      _otaFlashPending = false;
    uint8_t*           _otaImage = nullptr;
    size_t             _otaImageSize = 0;

    SemaphoreHandle_t _mutex;

    // Deferred event queue (max 4 pending events)
    static const int MAX_PENDING_EVENTS = 4;
    struct PendingEvent { uint8_t type; uint8_t id; };
    volatile PendingEvent _pendingEvents[4];
    volatile int _pendingEventCount;
};

// Global pointer for BLE callbacks
extern BleService* g_bleService;

#endif // BLE_SERVICE_H
