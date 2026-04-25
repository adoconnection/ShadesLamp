#include "led_driver.h"

#define TAG "[LED]"

LedDriver::LedDriver(uint8_t pin, uint16_t width, uint16_t height)
    : _pin(pin)
    , _width(width)
    , _height(height)
    , _numPixels(width * height)
    , _framebuffer(nullptr)
    , _strip(nullptr)
{
    _mutex = xSemaphoreCreateMutex();
}

LedDriver::~LedDriver() {
    if (_framebuffer) free(_framebuffer);
    if (_strip)       delete _strip;
    if (_mutex)       vSemaphoreDelete(_mutex);
}

void LedDriver::begin() {
    // Allocate framebuffer in PSRAM (3 bytes per pixel: R, G, B)
    size_t bufSize = (size_t)_numPixels * 3;
    _framebuffer = (uint8_t*)ps_malloc(bufSize);
    if (!_framebuffer) {
        Serial.printf("%s Failed to allocate framebuffer (%u bytes) in PSRAM\r\n", TAG, bufSize);
        // Fallback to regular malloc
        _framebuffer = (uint8_t*)malloc(bufSize);
        if (!_framebuffer) {
            Serial.printf("%s Failed to allocate framebuffer in RAM — halting\r\n", TAG);
            while (true) { delay(1000); }
        }
    }
    memset(_framebuffer, 0, bufSize);

    // Initialize NeoPixel strip
    _strip = new Adafruit_NeoPixel(_numPixels, _pin, NEO_GRB + NEO_KHZ800);
    _strip->begin();
    _strip->clear();
    _strip->show();

    Serial.printf("%s Initialized %ux%u (%u pixels) on GPIO %u\r\n",
                  TAG, _width, _height, _numPixels, _pin);
}

void LedDriver::setPixel(uint16_t x, uint16_t y, uint8_t r, uint8_t g, uint8_t b) {
    if (x >= _width || y >= _height) return;

    xSemaphoreTake(_mutex, portMAX_DELAY);

    uint32_t idx = (uint32_t)(y * _width + x) * 3;
    _framebuffer[idx + 0] = r;
    _framebuffer[idx + 1] = g;
    _framebuffer[idx + 2] = b;

    xSemaphoreGive(_mutex);
}

void LedDriver::show() {
    xSemaphoreTake(_mutex, portMAX_DELAY);

    // Copy framebuffer to NeoPixel strip
    for (uint16_t i = 0; i < _numPixels; i++) {
        uint32_t idx = (uint32_t)i * 3;
        _strip->setPixelColor(i, _framebuffer[idx], _framebuffer[idx + 1], _framebuffer[idx + 2]);
    }
    _strip->show();

    xSemaphoreGive(_mutex);
}

void LedDriver::clear() {
    xSemaphoreTake(_mutex, portMAX_DELAY);

    memset(_framebuffer, 0, (size_t)_numPixels * 3);

    xSemaphoreGive(_mutex);
}

uint16_t LedDriver::getWidth() const {
    return _width;
}

uint16_t LedDriver::getHeight() const {
    return _height;
}
