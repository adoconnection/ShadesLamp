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

class LedDriver {
public:
    LedDriver(uint8_t pin, uint16_t width, uint16_t height, bool zigzag = false, uint8_t colorOrder = LED_ORDER_GRB);
    ~LedDriver();

    void begin();
    void setPixel(uint16_t x, uint16_t y, uint8_t r, uint8_t g, uint8_t b);
    void show();
    void clear();

    uint16_t getWidth() const;
    uint16_t getHeight() const;

private:
    uint8_t  _pin;
    uint16_t _width;
    uint16_t _height;
    uint16_t _numPixels;

    bool     _zigzag;
    uint8_t  _colorOrder;
    uint8_t* _framebuffer;          // RGB framebuffer in PSRAM
    Adafruit_NeoPixel* _strip;
    SemaphoreHandle_t  _mutex;
};

#endif // LED_DRIVER_H
