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

// Response chunk flags
#define CHUNK_FLAG_FINAL    0x01
#define CHUNK_FLAG_ERROR    0x02

class BleService {
public:
    BleService(ProgramManager* pm, LedDriver* led = nullptr);

    // Initialize BLE stack, create service and characteristics, start advertising
    void begin(const char* deviceName);

    // Send a chunked response on the Response characteristic
    void sendResponse(const String& data, bool isError = false);

    // Update the Active Program characteristic value and notify
    void notifyActiveProgram(uint8_t id);

    // Notify all clients with current parameter values JSON (chunked)
    void notifyParamValues();

    // Notify clients about program list changes (added/deleted)
    void notifyEvent(uint8_t eventType, uint8_t programId);

    // Check if a client is connected
    bool isConnected() const;

    // Upload progress: pause/resume rendering
    bool isPausedByUpload() const { return pausedByUpload; }

    // Power on/off (LEDs off but BLE still active)
    bool isPowerOn() const { return powerOn; }
    void setPower(bool on);

    ProgramManager* getProgramManager() const { return _pm; }
    LedDriver* getLedDriver() const { return _led; }

    // Called when BLE MTU is negotiated with a client
    void setNegotiatedMtu(uint16_t mtu) { _negotiatedMtu = mtu; }

    // Upload state
    uint8_t*  uploadBuffer;
    uint32_t  uploadSize;
    uint32_t  uploadOffset;
    bool      uploadInProgress;
    volatile bool pausedByUpload;
    volatile bool powerOn;

private:
    uint16_t getChunkPayload() const;

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

    SemaphoreHandle_t _mutex;
};

// Global pointer for BLE callbacks
extern BleService* g_bleService;

#endif // BLE_SERVICE_H
