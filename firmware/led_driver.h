#ifndef LED_DRIVER_H
#define LED_DRIVER_H

#include <Arduino.h>
#include "driver/rmt_tx.h"
#include "driver/rmt_encoder.h"

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

// Multi-panel layout limits. Each distinct pin becomes one RMT TX channel;
// the ESP32-S3 has 4 of them. Panels sharing a pin are daisy-chained in
// config order, so more than 4 panels means chaining.
#define LED_MAX_PANELS 8
#define LED_MAX_STRIPS 4

// One physical LED panel placed on the logical canvas.
// (x, y) is the canvas cell of the panel's footprint corner; y=0 is the
// bottom row (canvas convention). (w, h) are the panel's own dimensions as
// wired, before rotation. rot rotates the panel clockwise on the canvas, so
// a 90/270 panel occupies an h*w footprint. zigzag means serpentine wiring:
// odd local rows run right-to-left. mirror flips the panel along its local X
// (applied after zigzag, before rot) — for wiring that is a mirror image of
// the standard layout; rotations alone cannot express it.
struct LedPanel {
    uint8_t  pin;
    uint16_t x;
    uint16_t y;
    uint16_t w;
    uint16_t h;
    uint16_t rot;      // 0, 90, 180, 270 (clockwise)
    bool     zigzag;
    bool     mirror;
};

class LedDriver {
public:
    // rotation (0/90/180/270, clockwise) and mirror orient the implicit
    // single full-canvas panel; ignored when setPanels() supplies a layout.
    // For 90/270 width/height are still the CANVAS dimensions (the panel's
    // wired dims are the swapped pair).
    LedDriver(uint8_t pin, uint16_t width, uint16_t height, bool zigzag = false, uint8_t colorOrder = LED_ORDER_GRB,
              uint16_t rotation = 0, bool mirror = false);
    ~LedDriver();

    // Optional multi-panel layout; must be called before begin(). Without it
    // a single full-canvas panel on the constructor pin/zigzag is assumed.
    // Returns false (and keeps the previous layout) on invalid input.
    bool setPanels(const LedPanel* panels, uint8_t count);

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

    // Global brightness scale applied in show() (8.8 fixed: 256 = full).
    // Used for host-side crossfade on program switch.
    void     setFadeScale(uint16_t scale256) { _fadeScale = scale256 > 256 ? 256 : scale256; }
    uint16_t getFadeScale() const { return _fadeScale; }

private:
    // One RMT TX channel: all panels on one pin, chained in config order.
    struct Strip {
        uint8_t  pin;
        uint32_t numPixels;
        uint32_t*            lut;      // chain index -> framebuffer pixel index (LED_LUT_OFF = off-canvas)
        uint8_t*             out;      // numPixels*3 wire-order bytes (internal RAM: read from RMT ISR)
        rmt_channel_handle_t channel;
        rmt_encoder_handle_t encoder;  // bytes+reset composite encoder (stateful, one per channel)
        bool     dma;                  // channel runs on the (single) DMA-capable slot
    };

    bool buildStrips();   // group panels by pin, allocate LUTs/output buffers
    bool initRmt(Strip& s, bool tryDma);

    uint8_t  _pin;
    uint16_t _width;
    uint16_t _height;
    uint16_t _numPixels;

    bool     _zigzag;
    uint16_t _rotation;             // orientation of the implicit single panel
    bool     _mirror;
    uint8_t  _colorOrder;
    uint8_t* _framebuffer;          // RGB framebuffer in PSRAM
    uint32_t _maxCurrentMa;         // 0 = no current limit
    uint16_t _fadeScale;            // 0..256 global brightness (crossfade)

    LedPanel _panels[LED_MAX_PANELS];
    uint8_t  _panelCount;
    Strip    _strips[LED_MAX_STRIPS];
    uint8_t  _stripCount;

    SemaphoreHandle_t  _mutex;
};

#endif // LED_DRIVER_H
