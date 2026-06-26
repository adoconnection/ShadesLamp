#include "led_driver.h"

#define TAG "[LED]"

// Map color order index to Adafruit NeoPixel type constant
static neoPixelType colorOrderToNeoType(uint8_t order) {
    switch (order) {
        case LED_ORDER_RGB: return NEO_RGB + NEO_KHZ800;
        case LED_ORDER_BRG: return NEO_BRG + NEO_KHZ800;
        case LED_ORDER_RBG: return NEO_RBG + NEO_KHZ800;
        case LED_ORDER_GBR: return NEO_GBR + NEO_KHZ800;
        case LED_ORDER_BGR: return NEO_BGR + NEO_KHZ800;
        default:            return NEO_GRB + NEO_KHZ800;
    }
}

LedDriver::LedDriver(uint8_t pin, uint16_t width, uint16_t height, bool zigzag, uint8_t colorOrder)
    : _pin(pin)
    , _width(width)
    , _height(height)
    , _numPixels(width * height)
    , _zigzag(zigzag)
    , _colorOrder(colorOrder < LED_ORDER_COUNT ? colorOrder : LED_ORDER_GRB)
    , _framebuffer(nullptr)
    , _maxCurrentMa(0)
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

    // Initialize NeoPixel strip with configured color order
    _strip = new Adafruit_NeoPixel(_numPixels, _pin, colorOrderToNeoType(_colorOrder));
    _strip->begin();
    _strip->clear();
    _strip->show();

    static const char* ORDER_NAMES[] = {"GRB","RGB","BRG","RBG","GBR","BGR"};
    Serial.printf("%s Initialized %ux%u (%u pixels) on GPIO %u, order=%s\r\n",
                  TAG, _width, _height, _numPixels, _pin,
                  _colorOrder < LED_ORDER_COUNT ? ORDER_NAMES[_colorOrder] : "?");
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

void LedDriver::commit(const uint8_t* rgb) {
    if (!rgb) return;
    xSemaphoreTake(_mutex, portMAX_DELAY);
    memcpy(_framebuffer, rgb, (size_t)_numPixels * 3);
    xSemaphoreGive(_mutex);
}

void LedDriver::show() {
    xSemaphoreTake(_mutex, portMAX_DELAY);

    // Optional current limiting: estimate total draw and, if it exceeds the
    // configured budget, scale every channel down by a single fixed-point factor.
    // scale256 is 8.8 fixed point: 256 == 1.0 (no scaling).
    uint16_t scale256 = 256;
    if (_maxCurrentMa > 0) {
        uint64_t channelSum = 0;
        size_t total = (size_t)_numPixels * 3;
        for (size_t i = 0; i < total; i++) channelSum += _framebuffer[i];
        // Each channel at value 255 draws ~LED_MA_PER_CHANNEL mA.
        uint32_t estimatedMa = (uint32_t)(channelSum * LED_MA_PER_CHANNEL / 255);
        if (estimatedMa > _maxCurrentMa) {
            scale256 = (uint16_t)(((uint64_t)_maxCurrentMa * 256) / estimatedMa);
        }
    }

    // Copy framebuffer to NeoPixel strip with optional zigzag remapping
    for (uint16_t y = 0; y < _height; y++) {
        for (uint16_t x = 0; x < _width; x++) {
            uint32_t fbIdx = (uint32_t)(y * _width + x) * 3;
            uint16_t stripIdx;
            if (_zigzag && (y & 1)) {
                // Odd rows: reversed direction
                stripIdx = y * _width + (_width - 1 - x);
            } else {
                stripIdx = y * _width + x;
            }
            uint8_t r = _framebuffer[fbIdx];
            uint8_t g = _framebuffer[fbIdx + 1];
            uint8_t b = _framebuffer[fbIdx + 2];
            if (scale256 < 256) {
                r = (uint8_t)((r * scale256) >> 8);
                g = (uint8_t)((g * scale256) >> 8);
                b = (uint8_t)((b * scale256) >> 8);
            }
            _strip->setPixelColor(stripIdx, r, g, b);
        }
    }
    _strip->show();

    xSemaphoreGive(_mutex);
}

void LedDriver::setMaxCurrent(uint32_t maxMa) {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    _maxCurrentMa = maxMa;
    xSemaphoreGive(_mutex);
    Serial.printf("%s Max current limit: %u mA%s\r\n", TAG, maxMa, maxMa == 0 ? " (disabled)" : "");
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
