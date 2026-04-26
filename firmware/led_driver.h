#ifndef LED_DRIVER_H
#define LED_DRIVER_H

#include <Arduino.h>
#include <Adafruit_NeoPixel.h>

class LedDriver {
public:
    LedDriver(uint8_t pin, uint16_t width, uint16_t height, bool zigzag = false);
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
    uint8_t* _framebuffer;          // RGB framebuffer in PSRAM
    Adafruit_NeoPixel* _strip;
    SemaphoreHandle_t  _mutex;
};

#endif // LED_DRIVER_H
