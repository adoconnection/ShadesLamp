#ifndef LED_DRIVER_H
#define LED_DRIVER_H

#include <Arduino.h>
#include <Adafruit_NeoPixel.h>

// Color order presets (index used in config/BLE protocol)
#define LED_ORDER_GRB  0  // default for WS2812
#define LED_ORDER_RGB  1
#define LED_ORDER_BRG  2
#define LED_ORDER_RBG  3
#define LED_ORDER_GBR  4
#define LED_ORDER_BGR  5
#define LED_ORDER_COUNT 6

// Estimated current draw of a single colour channel at full brightness (255).
// WS2812 draws ~20 mA per channel (R/G/B) when fully on at 5 V.
#define LED_MA_PER_CHANNEL 20

class LedDriver {
public:
    LedDriver(uint8_t pin, uint16_t width, uint16_t height, bool zigzag = false, uint8_t colorOrder = LED_ORDER_GRB);
    ~LedDriver();

    void begin();
    void setPixel(uint16_t x, uint16_t y, uint8_t r, uint8_t g, uint8_t b);
    // Bulk-copy a full RGB frame (numPixels*3 bytes, row-major) into the buffer.
    void commit(const uint8_t* rgb);
    void show();
    void clear();

    uint32_t bufferBytes() const { return (uint32_t)_numPixels * 3; }

    uint16_t getWidth() const;
    uint16_t getHeight() const;

    // Current limiting: cap the estimated total LED current.
    // maxMa == 0 disables limiting (full brightness, no scaling).
    void     setMaxCurrent(uint32_t maxMa);
    uint32_t getMaxCurrent() const { return _maxCurrentMa; }

private:
    uint8_t  _pin;
    uint16_t _width;
    uint16_t _height;
    uint16_t _numPixels;

    bool     _zigzag;
    uint8_t  _colorOrder;
    uint8_t* _framebuffer;          // RGB framebuffer in PSRAM
    uint32_t _maxCurrentMa;         // 0 = no current limit
    Adafruit_NeoPixel* _strip;
    SemaphoreHandle_t  _mutex;
};

#endif // LED_DRIVER_H
