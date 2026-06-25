#ifndef TOUCH_INPUT_H
#define TOUCH_INPUT_H

#include <Arduino.h>

class BleService;
class ProgramManager;

class TouchInput {
public:
    TouchInput(uint8_t pin, bool activeLow = false);

    // Wire up callbacks and attach interrupt
    void begin(BleService* ble, ProgramManager* pm);

    // Pump the gesture state machine — call frequently from loop()
    void tick();

private:
    void handleEdge(bool pressed, uint32_t edgeMs);
    void onSingleTap();
    void onDoubleTap();
    void onLongPress();
    void switchRelative(int delta);

    uint8_t _pin;
    bool    _activeLow;

    BleService*     _ble;
    ProgramManager* _pm;

    // Gesture state machine
    bool     _touching;
    bool     _longFired;
    uint32_t _touchStartMs;
    uint32_t _lastChangeMs;
    uint32_t _pendingTapMs;
    uint8_t  _tapCount;
};

extern TouchInput* g_touch;

#endif // TOUCH_INPUT_H
