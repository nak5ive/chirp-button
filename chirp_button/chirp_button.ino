// Chirp Button firmware
// Seeed XIAO nRF52840 + Adafruit NeoKey BFF
// See SPEC.md for full behavioral specification.

#include <bluefruit.h>
#include <Adafruit_NeoPixel.h>
#include <math.h>
#include "config.h"

// ── BLE services ──────────────────────────────────────────────────────────────
BLEDis         bledis;
BLEHidAdafruit blehid;

// ── NeoPixel ──────────────────────────────────────────────────────────────────
Adafruit_NeoPixel pixel(NEO_COUNT, NEO_PIN, NEO_GRB + NEO_KHZ800);

// ── State machine (§5.1) ──────────────────────────────────────────────────────
enum State {
    STATE_ADVERTISING,
    STATE_CONNECTED_IDLE,
    STATE_CONNECTED_ACTIVE,
    STATE_BOND_CLEARING
};
static State state = STATE_ADVERTISING;

// ── BLE connection flag — written in callbacks, read in loop ──────────────────
static volatile bool bleConnected = false;

// ── Button debounce state (§5.7) ──────────────────────────────────────────────
static bool     buttonRaw        = false;
static bool     buttonState      = false;
static bool     lastButtonState  = false;
static uint32_t lastDebounceTime = 0;

// ── Bond-clear hold timer (§5.6) ──────────────────────────────────────────────
static bool     bondTimerActive = false;
static uint32_t bondTimerStart  = 0;

// ── Breathe phase anchor (§5.3) ───────────────────────────────────────────────
static uint32_t breatheStartTime = 0;


// ─────────────────────────────────────────────────────────────────────────────
// BLE callbacks
// ─────────────────────────────────────────────────────────────────────────────

void connectCallback(uint16_t conn_handle) {
    (void)conn_handle;
    bleConnected = true;
}

void disconnectCallback(uint16_t conn_handle, uint8_t reason) {
    (void)conn_handle;
    (void)reason;
    bleConnected = false;
}


// ─────────────────────────────────────────────────────────────────────────────
// NeoPixel helpers
// ─────────────────────────────────────────────────────────────────────────────

static void showColor(uint32_t color) {
    pixel.setPixelColor(0, color);
    pixel.show();
}

static void showOff() {
    pixel.setPixelColor(0, 0);
    pixel.show();
}

static void resetBreathePhase() {
    breatheStartTime = millis();
}

// Sine-curve breathe effect (§5.3). Call every loop tick while in a breathe state.
static void showBreathe(uint32_t color) {
    uint32_t elapsed = millis() - breatheStartTime;
    float t          = (float)(elapsed % BREATHE_PERIOD_MS) / (float)BREATHE_PERIOD_MS;
    float sine       = (sinf(t * TWO_PI - HALF_PI) + 1.0f) / 2.0f;
    float brightness = BREATHE_MIN + sine * (BREATHE_MAX - BREATHE_MIN);
    float scale      = brightness / 255.0f;

    uint8_t r = (uint8_t)(((color >> 16) & 0xFF) * scale);
    uint8_t g = (uint8_t)(((color >> 8)  & 0xFF) * scale);
    uint8_t b = (uint8_t)(( color        & 0xFF) * scale);

    pixel.setPixelColor(0, r, g, b);
    pixel.show();
}


// ─────────────────────────────────────────────────────────────────────────────
// Button debounce (§5.7)
// ─────────────────────────────────────────────────────────────────────────────

static void updateButton() {
    lastButtonState = buttonState;

    bool raw = (digitalRead(PTT_PIN) == LOW);  // active LOW
    if (raw != buttonRaw) {
        lastDebounceTime = millis();
        buttonRaw = raw;
    }
    if (millis() - lastDebounceTime >= DEBOUNCE_MS) {
        buttonState = buttonRaw;
    }
}

static bool buttonJustPressed()  { return  buttonState && !lastButtonState; }
static bool buttonJustReleased() { return !buttonState &&  lastButtonState; }


// ─────────────────────────────────────────────────────────────────────────────
// Bond-clear sequence — blocking; device resets at the end (§5.6)
// ─────────────────────────────────────────────────────────────────────────────

static void runBondClear() {
    state = STATE_BOND_CLEARING;

    uint32_t start      = millis();
    uint32_t lastToggle = start;
    bool     ledOn      = false;

    while (millis() - start < BOND_CLEAR_FLASH_MS) {
        uint32_t now = millis();
        if (now - lastToggle >= BOND_CLEAR_FLASH_PERIOD_MS) {
            ledOn      = !ledOn;
            lastToggle = now;
            ledOn ? showColor(COLOR_CLEAR) : showOff();
        }
        yield();  // keep RTOS/BLE stack ticking during the wait
    }

    showOff();
    Bluefruit.Periph.clearBonds();
    NVIC_SystemReset();
}


// ─────────────────────────────────────────────────────────────────────────────
// BLE advertising setup
// ─────────────────────────────────────────────────────────────────────────────

static void startAdv() {
    Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
    Bluefruit.Advertising.addTxPower();
    Bluefruit.Advertising.addAppearance(BLE_APPEARANCE_HID_KEYBOARD);  // 0x03C1
    Bluefruit.Advertising.addService(blehid);
    Bluefruit.ScanResponse.addName();

    // Resume advertising automatically after any disconnect
    Bluefruit.Advertising.restartOnDisconnect(true);

    // Fast interval (20 ms) for 30 s, then slow (152.5 ms); units of 0.625 ms
    Bluefruit.Advertising.setInterval(32, 244);
    Bluefruit.Advertising.setFastTimeout(30);
    Bluefruit.Advertising.start(0);  // 0 = advertise until connected
}


// ─────────────────────────────────────────────────────────────────────────────
// Setup (§5.5)
// ─────────────────────────────────────────────────────────────────────────────

void setup() {
    // Boot flash
    pixel.begin();
    pixel.setBrightness(NEO_BRIGHTNESS);
    showColor(COLOR_BOOT);
    delay(300);
    showOff();
    delay(100);

    // Button
    pinMode(PTT_PIN, INPUT_PULLUP);

    // BLE
    Bluefruit.begin();
    Bluefruit.setTxPower(4);
    Bluefruit.setName(BLE_DEVICE_NAME);
    Bluefruit.Periph.setConnectCallback(connectCallback);
    Bluefruit.Periph.setDisconnectCallback(disconnectCallback);

    // Device Information Service
    bledis.setManufacturer("Chirp");
    bledis.setModel("Chirp Button");
    bledis.begin();

    // HID keyboard service
    blehid.begin();

    // Start advertising → ADVERTISING state
    startAdv();
    resetBreathePhase();
    state = STATE_ADVERTISING;
}


// ─────────────────────────────────────────────────────────────────────────────
// Main loop (§5.1, §5.2)
// ─────────────────────────────────────────────────────────────────────────────

void loop() {
    updateButton();

    // ── BLE-driven state transitions ──────────────────────────────────────────

    if (bleConnected && state == STATE_ADVERTISING) {
        // Host connected — cancel any pending bond-clear timer, enter idle
        bondTimerActive = false;
        state = STATE_CONNECTED_IDLE;
        resetBreathePhase();

    } else if (!bleConnected &&
               (state == STATE_CONNECTED_IDLE || state == STATE_CONNECTED_ACTIVE)) {
        // Connection lost (including mid-press) — return to advertising
        state = STATE_ADVERTISING;
        resetBreathePhase();
    }

    // ── Per-state behaviour ───────────────────────────────────────────────────

    switch (state) {

        // ── ADVERTISING: breathe blue, watch for bond-clear hold (§5.6) ───────
        case STATE_ADVERTISING:
            showBreathe(COLOR_ADV);

            if (buttonJustPressed()) {
                bondTimerActive = true;
                bondTimerStart  = millis();
            }
            if (buttonJustReleased()) {
                bondTimerActive = false;
            }
            if (bondTimerActive && (millis() - bondTimerStart >= BOND_CLEAR_HOLD_MS)) {
                runBondClear();  // blocking — resets device inside
            }
            break;

        // ── CONNECTED_IDLE: breathe green, watch for button press ─────────────
        case STATE_CONNECTED_IDLE:
            showBreathe(COLOR_IDLE);

            if (buttonJustPressed()) {
                state = STATE_CONNECTED_ACTIVE;
                uint8_t keys[6] = { 0, 0, 0, 0, 0, 0 };
                blehid.keyboardReport(HID_MODIFIER, keys);
                showColor(COLOR_ACTIVE);
            }
            break;

        // ── CONNECTED_ACTIVE: solid red, wait for release ─────────────────────
        case STATE_CONNECTED_ACTIVE:
            if (buttonJustReleased()) {
                state = STATE_CONNECTED_IDLE;
                blehid.keyRelease();
                resetBreathePhase();
                showBreathe(COLOR_IDLE);
            }
            break;

        // ── BOND_CLEARING: entered and exited inside runBondClear() ───────────
        case STATE_BOND_CLEARING:
            break;
    }
}
