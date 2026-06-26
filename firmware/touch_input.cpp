#include "touch_input.h"
#include "ble_service.h"
#include "program_manager.h"
#include "playlists.h"

#define TAG "[TOUCH]"

static const uint32_t TOUCH_DEBOUNCE_MS  = 15;
static const uint32_t TOUCH_LONGPRESS_MS = 500;
static const uint32_t TOUCH_DOUBLETAP_MS = 300;

TouchInput* g_touch = nullptr;

// ── ISR-shared state ───────────────────────────────────────────────────────
// Single-instance: ISR signature has no userdata, so we use file-scope statics.
static volatile bool     s_hasEdge   = false;
static volatile bool     s_lastLevel = false;
static volatile uint32_t s_edgeMs    = 0;
static uint8_t           s_pin       = 0;

static void IRAM_ATTR isrTouch() {
    s_lastLevel = (digitalRead(s_pin) == HIGH);
    s_edgeMs    = millis();
    s_hasEdge   = true;
}

// ── TouchInput ─────────────────────────────────────────────────────────────

TouchInput::TouchInput(uint8_t pin, bool activeLow)
    : _pin(pin)
    , _activeLow(activeLow)
    , _ble(nullptr)
    , _pm(nullptr)
    , _touching(false)
    , _longFired(false)
    , _touchStartMs(0)
    , _lastChangeMs(0)
    , _pendingTapMs(0)
    , _tapCount(0)
{}

void TouchInput::begin(BleService* ble, ProgramManager* pm) {
    _ble = ble;
    _pm  = pm;
    pinMode(_pin, _activeLow ? INPUT_PULLUP : INPUT_PULLDOWN);
    s_pin = _pin;
    attachInterrupt(digitalPinToInterrupt(_pin), isrTouch, CHANGE);
    Serial.printf("%s Initialized on GPIO %u (active-%s)\r\n",
                  TAG, _pin, _activeLow ? "LOW" : "HIGH");
}

void TouchInput::tick() {
    // Drain the latest edge captured by the ISR
    if (s_hasEdge) {
        noInterrupts();
        bool     level = s_lastLevel;
        uint32_t ems   = s_edgeMs;
        s_hasEdge = false;
        interrupts();

        bool pressed = _activeLow ? !level : level;
        handleEdge(pressed, ems);
    }

    uint32_t now = millis();

    if (_touching && !_longFired && (now - _touchStartMs) >= TOUCH_LONGPRESS_MS) {
        _longFired    = true;
        _pendingTapMs = 0;
        _tapCount     = 0;
        onLongPress();
    }

    if (_pendingTapMs && (now - _pendingTapMs) > TOUCH_DOUBLETAP_MS) {
        _pendingTapMs = 0;
        if (_tapCount == 1) {
            _tapCount = 0;
            onSingleTap();
        }
    }
}

void TouchInput::handleEdge(bool pressed, uint32_t edgeMs) {
    if (pressed == _touching) return;
    if ((edgeMs - _lastChangeMs) < TOUCH_DEBOUNCE_MS) return;
    _lastChangeMs = edgeMs;

    if (pressed) {
        _touching     = true;
        _longFired    = false;
        _touchStartMs = edgeMs;
    } else {
        _touching = false;
        if (!_longFired) {
            if (_pendingTapMs && (edgeMs - _pendingTapMs) <= TOUCH_DOUBLETAP_MS) {
                _pendingTapMs = 0;
                _tapCount     = 0;
                onDoubleTap();
            } else {
                _pendingTapMs = edgeMs;
                _tapCount     = 1;
            }
        }
    }
}

void TouchInput::onSingleTap() {
    if (!_ble->isPowerOn()) {
        Serial.printf("%s single tap -> power ON\r\n", TAG);
        _ble->setPower(true);
    } else {
        Serial.printf("%s single tap -> next program\r\n", TAG);
        switchRelative(+1);
    }
}

void TouchInput::onDoubleTap() {
    if (!_ble->isPowerOn()) {
        Serial.printf("%s double tap -> power ON\r\n", TAG);
        _ble->setPower(true);
    } else {
        Serial.printf("%s double tap -> previous program\r\n", TAG);
        switchRelative(-1);
    }
}

void TouchInput::onLongPress() {
    if (_ble->isPowerOn()) {
        Serial.printf("%s long press -> power OFF\r\n", TAG);
        _ble->setPower(false);
    }
}

void TouchInput::switchRelative(int delta) {
    std::vector<uint8_t> ids = _pm->getOrderedIds();
    if (ids.empty()) return;

    uint8_t cur = _pm->getActiveId();
    int curIdx = -1;
    for (size_t i = 0; i < ids.size(); i++) {
        if (ids[i] == cur) { curIdx = (int)i; break; }
    }

    int n = (int)ids.size();
    int nextIdx = (curIdx < 0) ? 0 : ((curIdx + delta % n + n) % n);
    // Hardware program change leaves playlist mode (no override on next tick).
    Playlists::stop();
    _pm->requestSwitch(ids[nextIdx]);
}
