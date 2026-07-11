#include "led_driver.h"

#include "esp_heap_caps.h"
#include "soc/soc_caps.h"

#define TAG "[LED]"

// LUT sentinel: chain position maps to no canvas cell (panel sticks out of
// the canvas or layout gap) — transmitted as black.
#define LED_LUT_OFF 0xFFFFFFFFu

// RMT tick = 0.1 us (10 MHz). WS2812 bit timings in ticks below match the
// values this hardware ran on with the previous driver: bit0 = 0.4us H +
// 0.8us L, bit1 = 0.8us H + 0.4us L (1.2 us per bit, 800 kHz).
#define RMT_RESOLUTION_HZ (10 * 1000 * 1000)

#ifndef SOC_RMT_MEM_WORDS_PER_CHANNEL
#define SOC_RMT_MEM_WORDS_PER_CHANNEL 48
#endif

// ── LED strip RMT encoder ───────────────────────────────────────────────────
// Composite encoder (after the ESP-IDF led_strip example): streams the pixel
// bytes through a bytes-encoder, then appends one long low "reset" symbol so
// the strip latches. Stateful — each channel owns its own instance. The
// encode callback runs in ISR context, hence IRAM_ATTR and internal-RAM
// allocations only.

typedef struct {
    rmt_encoder_t base;            // must stay first: handle is cast back
    rmt_encoder_handle_t bytes_encoder;
    rmt_encoder_handle_t copy_encoder;
    int state;                     // 0 = pixel bytes, 1 = reset code
    rmt_symbol_word_t reset_code;
} led_strip_encoder_t;

static size_t IRAM_ATTR ledEncoderEncode(rmt_encoder_t* encoder, rmt_channel_handle_t channel,
                                         const void* primary_data, size_t data_size,
                                         rmt_encode_state_t* ret_state) {
    led_strip_encoder_t* enc = (led_strip_encoder_t*)encoder;
    rmt_encode_state_t session_state = RMT_ENCODING_RESET;
    int state = 0;
    size_t encoded = 0;

    switch (enc->state) {
    case 0: // pixel bytes
        encoded += enc->bytes_encoder->encode(enc->bytes_encoder, channel,
                                              primary_data, data_size, &session_state);
        if (session_state & RMT_ENCODING_COMPLETE) {
            enc->state = 1;
        }
        if (session_state & RMT_ENCODING_MEM_FULL) {
            state |= RMT_ENCODING_MEM_FULL;
            break;
        }
        [[fallthrough]];
    case 1: // reset (latch) code
        encoded += enc->copy_encoder->encode(enc->copy_encoder, channel,
                                             &enc->reset_code, sizeof(enc->reset_code),
                                             &session_state);
        if (session_state & RMT_ENCODING_COMPLETE) {
            enc->state = 0;
            state |= RMT_ENCODING_COMPLETE;
        }
        if (session_state & RMT_ENCODING_MEM_FULL) {
            state |= RMT_ENCODING_MEM_FULL;
        }
        break;
    }

    *ret_state = (rmt_encode_state_t)state;
    return encoded;
}

static esp_err_t ledEncoderReset(rmt_encoder_t* encoder) {
    led_strip_encoder_t* enc = (led_strip_encoder_t*)encoder;
    rmt_encoder_reset(enc->bytes_encoder);
    rmt_encoder_reset(enc->copy_encoder);
    enc->state = 0;
    return ESP_OK;
}

static esp_err_t ledEncoderDel(rmt_encoder_t* encoder) {
    led_strip_encoder_t* enc = (led_strip_encoder_t*)encoder;
    if (enc->bytes_encoder) rmt_del_encoder(enc->bytes_encoder);
    if (enc->copy_encoder)  rmt_del_encoder(enc->copy_encoder);
    free(enc);
    return ESP_OK;
}

static esp_err_t createLedEncoder(rmt_encoder_handle_t* ret) {
    led_strip_encoder_t* enc = (led_strip_encoder_t*)
        heap_caps_calloc(1, sizeof(led_strip_encoder_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!enc) return ESP_ERR_NO_MEM;

    enc->base.encode = ledEncoderEncode;
    enc->base.reset  = ledEncoderReset;
    enc->base.del    = ledEncoderDel;

    rmt_bytes_encoder_config_t bytes_cfg = {};
    bytes_cfg.bit0.level0 = 1; bytes_cfg.bit0.duration0 = 4;  // 0.4 us high
    bytes_cfg.bit0.level1 = 0; bytes_cfg.bit0.duration1 = 8;  // 0.8 us low
    bytes_cfg.bit1.level0 = 1; bytes_cfg.bit1.duration0 = 8;  // 0.8 us high
    bytes_cfg.bit1.level1 = 0; bytes_cfg.bit1.duration1 = 4;  // 0.4 us low
    bytes_cfg.flags.msb_first = 1;
    esp_err_t err = rmt_new_bytes_encoder(&bytes_cfg, &enc->bytes_encoder);
    if (err != ESP_OK) { free(enc); return err; }

    rmt_copy_encoder_config_t copy_cfg = {};
    err = rmt_new_copy_encoder(&copy_cfg, &enc->copy_encoder);
    if (err != ESP_OK) {
        rmt_del_encoder(enc->bytes_encoder);
        free(enc);
        return err;
    }

    // 2 x 150 us low = 300 us latch, comfortably above the WS2812 minimum.
    enc->reset_code.level0 = 0; enc->reset_code.duration0 = 1500;
    enc->reset_code.level1 = 0; enc->reset_code.duration1 = 1500;

    *ret = &enc->base;
    return ESP_OK;
}

// ── LedDriver ───────────────────────────────────────────────────────────────

LedDriver::LedDriver(uint8_t pin, uint16_t width, uint16_t height, bool zigzag, uint8_t colorOrder)
    : _pin(pin)
    , _width(width)
    , _height(height)
    , _numPixels(width * height)
    , _zigzag(zigzag)
    , _colorOrder(colorOrder < LED_ORDER_COUNT ? colorOrder : LED_ORDER_GRB)
    , _framebuffer(nullptr)
    , _maxCurrentMa(0)
    , _fadeScale(256)
    , _panelCount(0)
    , _stripCount(0)
{
    memset(_strips, 0, sizeof(_strips));
    _mutex = xSemaphoreCreateMutex();
}

LedDriver::~LedDriver() {
    for (uint8_t i = 0; i < _stripCount; i++) {
        Strip& s = _strips[i];
        if (s.channel) {
            rmt_tx_wait_all_done(s.channel, 100);
            rmt_disable(s.channel);
            rmt_del_channel(s.channel);
        }
        if (s.encoder) rmt_del_encoder(s.encoder);
        if (s.lut)     free(s.lut);
        if (s.out)     free(s.out);
    }
    if (_framebuffer) free(_framebuffer);
    if (_mutex)       vSemaphoreDelete(_mutex);
}

bool LedDriver::setPanels(const LedPanel* panels, uint8_t count) {
    if (_framebuffer) {
        Serial.printf("%s setPanels() must be called before begin()\r\n", TAG);
        return false;
    }
    if (!panels || count == 0 || count > LED_MAX_PANELS) {
        Serial.printf("%s Invalid panel count %u (max %u)\r\n", TAG, count, LED_MAX_PANELS);
        return false;
    }

    uint8_t pins[LED_MAX_STRIPS];
    uint8_t pinCount = 0;
    for (uint8_t i = 0; i < count; i++) {
        const LedPanel& p = panels[i];
        if (p.w == 0 || p.h == 0 || p.pin > 48 ||
            (p.rot != 0 && p.rot != 90 && p.rot != 180 && p.rot != 270)) {
            Serial.printf("%s Panel %u invalid (pin=%u %ux%u rot=%u)\r\n",
                          TAG, i, p.pin, p.w, p.h, p.rot);
            return false;
        }
        bool known = false;
        for (uint8_t j = 0; j < pinCount; j++) {
            if (pins[j] == p.pin) { known = true; break; }
        }
        if (!known) {
            if (pinCount >= LED_MAX_STRIPS) {
                Serial.printf("%s Too many distinct pins (max %u RMT TX channels)\r\n",
                              TAG, LED_MAX_STRIPS);
                return false;
            }
            pins[pinCount++] = p.pin;
        }
    }

    memcpy(_panels, panels, sizeof(LedPanel) * count);
    _panelCount = count;
    return true;
}

bool LedDriver::buildStrips() {
    // Group panels by pin, first-appearance order. Panels sharing a pin are
    // daisy-chained in config order.
    _stripCount = 0;
    for (uint8_t i = 0; i < _panelCount; i++) {
        const LedPanel& p = _panels[i];
        Strip* s = nullptr;
        for (uint8_t j = 0; j < _stripCount; j++) {
            if (_strips[j].pin == p.pin) { s = &_strips[j]; break; }
        }
        if (!s) {
            s = &_strips[_stripCount++];
            s->pin = p.pin;
            s->numPixels = 0;
        }
        s->numPixels += (uint32_t)p.w * p.h;
    }

    for (uint8_t i = 0; i < _stripCount; i++) {
        Strip& s = _strips[i];
        s.lut = (uint32_t*)ps_malloc(s.numPixels * sizeof(uint32_t));
        if (!s.lut) s.lut = (uint32_t*)malloc(s.numPixels * sizeof(uint32_t));
        // Output buffer is read by the RMT ISR (or GDMA) while the flash cache
        // may be disabled (LittleFS I/O) — internal, DMA-capable RAM only.
        s.out = (uint8_t*)heap_caps_malloc(s.numPixels * 3,
                                           MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (!s.lut || !s.out) {
            Serial.printf("%s Failed to allocate strip buffers (pin %u, %u px)\r\n",
                          TAG, s.pin, s.numPixels);
            return false;
        }
    }

    // Fill LUTs: chain position -> framebuffer pixel index.
    uint32_t chainOff[LED_MAX_STRIPS] = {0};
    for (uint8_t i = 0; i < _panelCount; i++) {
        const LedPanel& p = _panels[i];
        uint8_t si = 0;
        while (_strips[si].pin != p.pin) si++;
        Strip& s = _strips[si];

        uint32_t panelPixels = (uint32_t)p.w * p.h;
        for (uint32_t pos = 0; pos < panelPixels; pos++) {
            uint16_t ly = pos / p.w;
            uint16_t sx = pos % p.w;
            uint16_t lx = (p.zigzag && (ly & 1)) ? (p.w - 1 - sx) : sx;

            // Canvas is y-up (y=0 = bottom row), rot is the panel's physical
            // clockwise rotation as seen facing the lamp: CW 90 maps local
            // (lx, ly) -> (ly, w-1-lx).
            uint16_t cx, cy;
            switch (p.rot) {
                case 90:  cx = ly;           cy = p.w - 1 - lx;   break;
                case 180: cx = p.w - 1 - lx; cy = p.h - 1 - ly;   break;
                case 270: cx = p.h - 1 - ly; cy = lx;             break;
                default:  cx = lx;           cy = ly;             break;
            }
            uint32_t gx = (uint32_t)p.x + cx;
            uint32_t gy = (uint32_t)p.y + cy;
            s.lut[chainOff[si] + pos] =
                (gx < _width && gy < _height) ? (gy * _width + gx) : LED_LUT_OFF;
        }
        chainOff[si] += panelPixels;
    }
    return true;
}

bool LedDriver::initRmt(Strip& s, bool tryDma) {
    rmt_tx_channel_config_t cfg = {};
    cfg.gpio_num = (gpio_num_t)s.pin;
    cfg.clk_src = RMT_CLK_SRC_DEFAULT;
    cfg.resolution_hz = RMT_RESOLUTION_HZ;
    cfg.mem_block_symbols = SOC_RMT_MEM_WORDS_PER_CHANNEL;
    cfg.trans_queue_depth = 1;

    esp_err_t err = ESP_FAIL;

    // The S3 has exactly one DMA-capable RMT TX channel. With a DMA buffer
    // large enough for the whole frame, every symbol is encoded up front in
    // task context and the transmission streams from RAM with no mid-frame
    // ISR refills — immune to the flash-cache-off windows (LittleFS I/O on
    // any core) that starve the interrupt-fed channels and glitch the strip.
    if (tryDma) {
        // Prefer a buffer that fits the whole frame (fully pre-encoded, zero
        // mid-frame interrupts). If that fails — allocation or an absurdly
        // large frame — retry with 16K symbols, then fall back to irq mode.
        uint32_t fullFrame = s.numPixels * 24 + 64;  // frame bits + reset + margin
        uint32_t candidates[2] = { fullFrame, 16384 };
        for (int t = 0; t < 2 && err != ESP_OK; t++) {
            uint32_t symbols = candidates[t];
            if (symbols > 32768) continue;            // >128 KB of DMA buffer: not sane
            if (t == 1 && fullFrame <= 16384) break;  // smaller retry is pointless
            rmt_tx_channel_config_t dmaCfg = cfg;
            dmaCfg.flags.with_dma = 1;
            dmaCfg.mem_block_symbols = symbols;
            err = rmt_new_tx_channel(&dmaCfg, &s.channel);
            if (err == ESP_OK) s.dma = true;
        }
        if (err != ESP_OK) {
            Serial.printf("%s DMA channel (pin %u) unavailable (%s), using interrupt mode\r\n",
                          TAG, s.pin, esp_err_to_name(err));
        }
    }
    if (err != ESP_OK) {
        err = rmt_new_tx_channel(&cfg, &s.channel);
    }
    if (err != ESP_OK) {
        Serial.printf("%s rmt_new_tx_channel(pin %u) failed: %s\r\n", TAG, s.pin, esp_err_to_name(err));
        s.channel = nullptr;
        return false;
    }
    err = createLedEncoder(&s.encoder);
    if (err != ESP_OK) {
        Serial.printf("%s LED encoder alloc (pin %u) failed: %s\r\n", TAG, s.pin, esp_err_to_name(err));
        rmt_del_channel(s.channel);
        s.channel = nullptr;
        s.encoder = nullptr;
        return false;
    }
    err = rmt_enable(s.channel);
    if (err != ESP_OK) {
        Serial.printf("%s rmt_enable(pin %u) failed: %s\r\n", TAG, s.pin, esp_err_to_name(err));
        rmt_del_encoder(s.encoder);
        rmt_del_channel(s.channel);
        s.channel = nullptr;
        s.encoder = nullptr;
        return false;
    }
    return true;
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

    // No explicit layout: one full-canvas panel on the constructor pin.
    if (_panelCount == 0) {
        _panels[0].pin = _pin;
        _panels[0].x = 0;
        _panels[0].y = 0;
        _panels[0].w = _width;
        _panels[0].h = _height;
        _panels[0].rot = 0;
        _panels[0].zigzag = _zigzag;
        _panelCount = 1;
    }

    if (!buildStrips()) {
        Serial.printf("%s Strip layout build failed — halting\r\n", TAG);
        while (true) { delay(1000); }
    }
    for (uint8_t i = 0; i < _stripCount; i++) {
        // Only one TX channel has DMA; give it to the first strip. On failure
        // the strip is skipped in show().
        initRmt(_strips[i], i == 0);
    }

    static const char* ORDER_NAMES[] = {"GRB","RGB","BRG","RBG","GBR","BGR"};
    Serial.printf("%s Initialized %ux%u canvas, %u panel(s), order=%s\r\n",
                  TAG, _width, _height, _panelCount,
                  _colorOrder < LED_ORDER_COUNT ? ORDER_NAMES[_colorOrder] : "?");
    for (uint8_t i = 0; i < _stripCount; i++) {
        Serial.printf("%s   strip %u: GPIO %u, %u px, %s\r\n", TAG, i, _strips[i].pin,
                      _strips[i].numPixels,
                      !_strips[i].channel ? "RMT INIT FAILED" : (_strips[i].dma ? "RMT+DMA" : "RMT irq"));
    }

    show(); // latch all-black
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

    // Apply global crossfade brightness on top of current limiting.
    if (_fadeScale < 256) {
        scale256 = (uint16_t)(((uint32_t)scale256 * _fadeScale) >> 8);
    }

    // Position of R/G/B inside the wire triplet for each color order.
    static const uint8_t POS[LED_ORDER_COUNT][3] = {
        {1, 0, 2},  // GRB
        {0, 1, 2},  // RGB
        {1, 2, 0},  // BRG
        {0, 2, 1},  // RBG
        {2, 0, 1},  // GBR
        {2, 1, 0},  // BGR
    };
    const uint8_t rPos = POS[_colorOrder][0];
    const uint8_t gPos = POS[_colorOrder][1];
    const uint8_t bPos = POS[_colorOrder][2];

    // The previous frame transmits in the background while the caller renders;
    // only now do we need the output buffers back.
    for (uint8_t i = 0; i < _stripCount; i++) {
        if (_strips[i].channel) rmt_tx_wait_all_done(_strips[i].channel, 100);
    }

    for (uint8_t i = 0; i < _stripCount; i++) {
        Strip& s = _strips[i];
        if (!s.channel) continue;

        uint8_t* dst = s.out;
        for (uint32_t p = 0; p < s.numPixels; p++, dst += 3) {
            uint32_t fb = s.lut[p];
            if (fb == LED_LUT_OFF) {
                dst[0] = dst[1] = dst[2] = 0;
                continue;
            }
            const uint8_t* src = _framebuffer + fb * 3;
            uint8_t r = src[0], g = src[1], b = src[2];
            if (scale256 < 256) {
                r = (uint8_t)((r * scale256) >> 8);
                g = (uint8_t)((g * scale256) >> 8);
                b = (uint8_t)((b * scale256) >> 8);
            }
            dst[rPos] = r;
            dst[gPos] = g;
            dst[bPos] = b;
        }

        rmt_transmit_config_t txc = {};
        txc.loop_count = 0;
        esp_err_t err = rmt_transmit(s.channel, s.encoder, s.out, (size_t)s.numPixels * 3, &txc);
        if (err != ESP_OK) {
            Serial.printf("%s rmt_transmit(pin %u) failed: %s\r\n", TAG, s.pin, esp_err_to_name(err));
        }
    }

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
