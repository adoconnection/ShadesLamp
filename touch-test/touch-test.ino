/*
 * Touch lamp test for ESP32-S3 (interrupt-driven)
 *
 * Hardware:
 *   - Touch sensor (digital, active-HIGH) on GPIO 8
 *   - 1x WS2812 on GPIO 48
 *
 * Behavior:
 *   - OFF state: any tap turns lamp ON at current color
 *   - ON state:
 *       * single tap   -> next color
 *       * double tap   -> previous color
 *       * long press   -> turn OFF
 *
 * Color cycle (Russian rainbow КЖЗСФ): Red, Yellow, Green, Blue, Violet.
 */

#include <Adafruit_NeoPixel.h>

// ---- Pins ----
static const uint8_t SENSOR_PIN = 8;
static const uint8_t LED_PIN    = 48;
static const uint8_t LED_COUNT  = 1;
static const bool    ACTIVE_LOW = false;

// ---- Timing ----
static const uint32_t LONG_PRESS_MS = 500;
static const uint32_t DOUBLE_TAP_MS = 300;
static const uint32_t DEBOUNCE_MS   = 15;

// ---- Colors ----
struct RGB { uint8_t r, g, b; };
static const RGB COLORS[] = {
    {255,   0,   0},   // К — красный
    {255, 200,   0},   // Ж — жёлтый
    {  0, 255,   0},   // З — зелёный
    {  0,   0, 255},   // С — синий
    {180,   0, 255},   // Ф — фиолетовый
};
static const uint8_t NUM_COLORS = sizeof(COLORS) / sizeof(COLORS[0]);

// ---- Lamp state ----
Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

bool     isOn          = false;
uint8_t  colorIdx      = 0;

// ---- Gesture state machine ----
bool     touching      = false;
bool     longFired     = false;
uint32_t touchStartMs  = 0;
uint32_t lastChangeMs  = 0;
uint32_t pendingTapMs  = 0;
uint8_t  tapCount      = 0;

// ---- ISR shared state ----
volatile bool     g_hasEdge   = false;
volatile bool     g_lastLevel = false;
volatile uint32_t g_edgeMs    = 0;

void IRAM_ATTR isrSensor() {
    g_lastLevel = (digitalRead(SENSOR_PIN) == HIGH);
    g_edgeMs    = millis();
    g_hasEdge   = true;
}

void applyOutput() {
    if (!isOn) {
        strip.setPixelColor(0, 0);
    } else {
        const RGB& c = COLORS[colorIdx];
        strip.setPixelColor(0, strip.Color(c.r, c.g, c.b));
    }
    strip.show();
}

void onSingleTap() {
    if (!isOn) {
        isOn = true;
    } else {
        colorIdx = (colorIdx + 1) % NUM_COLORS;
    }
    applyOutput();
}

void onDoubleTap() {
    if (!isOn) {
        isOn = true;
    } else {
        colorIdx = (colorIdx + NUM_COLORS - 1) % NUM_COLORS;
    }
    applyOutput();
}

void onLongPress() {
    if (isOn) {
        isOn = false;
        applyOutput();
    }
}

void handleEdge(bool pressed, uint32_t edgeMs) {
    if (pressed == touching) return;
    if ((edgeMs - lastChangeMs) < DEBOUNCE_MS) return;
    lastChangeMs = edgeMs;

    if (pressed) {
        touching     = true;
        longFired    = false;
        touchStartMs = edgeMs;
    } else {
        touching = false;
        if (!longFired) {
            if (pendingTapMs && (edgeMs - pendingTapMs) <= DOUBLE_TAP_MS) {
                pendingTapMs = 0;
                tapCount     = 0;
                onDoubleTap();
            } else {
                pendingTapMs = edgeMs;
                tapCount     = 1;
            }
        }
    }
}

void setup() {
    pinMode(SENSOR_PIN, ACTIVE_LOW ? INPUT_PULLUP : INPUT_PULLDOWN);
    strip.begin();
    strip.setBrightness(80);
    applyOutput();

    attachInterrupt(digitalPinToInterrupt(SENSOR_PIN), isrSensor, CHANGE);
}

void loop() {
    // Drain the latest edge, if any
    if (g_hasEdge) {
        noInterrupts();
        bool     level = g_lastLevel;
        uint32_t ems   = g_edgeMs;
        g_hasEdge = false;
        interrupts();

        bool pressed = ACTIVE_LOW ? !level : level;
        handleEdge(pressed, ems);
    }

    uint32_t now = millis();

    // Long-press fires while still held
    if (touching && !longFired && (now - touchStartMs) >= LONG_PRESS_MS) {
        longFired    = true;
        pendingTapMs = 0;
        tapCount     = 0;
        onLongPress();
    }

    // Pending single tap commits if no second tap arrived in time
    if (pendingTapMs && (now - pendingTapMs) > DOUBLE_TAP_MS) {
        pendingTapMs = 0;
        if (tapCount == 1) {
            tapCount = 0;
            onSingleTap();
        }
    }
}
