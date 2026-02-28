#pragma once

// ── Pin assignments ────────────────────────────────────────────────────────────
#define PTT_PIN             A2    // Cherry MX switch (active LOW, INPUT_PULLUP)
#define NEO_PIN             A3    // WS2812B data line
#define NEO_COUNT           1
#define NEO_BRIGHTNESS      80    // 0–255 global brightness

// ── BLE ───────────────────────────────────────────────────────────────────────
#define BLE_DEVICE_NAME     "Chirp Button"

// ── HID keycode ───────────────────────────────────────────────────────────────
// Right Option = bit 6 of the HID modifier byte (USB HID Usage Tables §10)
#define HID_MODIFIER        0x40

// ── Colours — packed 0x00RRGGBB (Adafruit_NeoPixel format) ───────────────────
#define COLOR_ADV           0x000000FFul  // blue   — advertising breathe hue
#define COLOR_IDLE          0x0000FF00ul  // green  — connected idle breathe hue
#define COLOR_ACTIVE        0x00FF0000ul  // red    — button held (solid)
#define COLOR_BOOT          0x000000FFul  // blue   — boot flash
#define COLOR_CLEAR         0x00800080ul  // purple — bond-clear flash

// ── Timing (milliseconds) ─────────────────────────────────────────────────────
#define DEBOUNCE_MS                 20UL
#define BREATHE_PERIOD_MS           3000UL
#define BREATHE_MIN                 5.0f    // minimum brightness during breathe
#define BREATHE_MAX                 180.0f  // peak brightness during breathe
#define BOND_CLEAR_HOLD_MS          5000UL
#define BOND_CLEAR_FLASH_MS         3000UL
#define BOND_CLEAR_FLASH_PERIOD_MS  100UL

// ── Power management (milliseconds) ──────────────────────────────────────────
// USB-C VBUS detection uses NRF_POWER->USBREGSTATUS — no TinyUSB required.
// Sleep and LED idle only apply when USB is absent (battery-only operation).
#define ADV_SLEEP_TIMEOUT_MS     300000UL   // 5 min   — battery-only: advertising with no connection before sleep
#define SLEEP_TIMEOUT_MS        3600000UL   // 1 hour  — battery-only idle in CONNECTED_IDLE before System OFF (deep sleep)
#define LED_IDLE_TIMEOUT_MS      300000UL   // 5 min   — battery-only idle in CONNECTED_IDLE before LED fade-out
#define LED_FADE_DURATION_MS       2000UL   // 2 s     — duration of LED fade-out animation
