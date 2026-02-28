# Chirp Button — Firmware Specification

## 1. Purpose

Chirp Button is a wireless, battery-powered BLE HID device that acts as a dedicated push-to-talk trigger for recording software. When held, it sends a configurable keyboard key over BLE; when released, it releases the key. USB-C is used solely for power and battery charging — no USB data connection is established. It provides visual feedback via an onboard NeoPixel LED.

---

## 2. Hardware

| Component | Detail |
|---|---|
| MCU | Seeed XIAO nRF52840 |
| Daughter board | Adafruit NeoKey BFF |
| Switch | Cherry MX-compatible (normally open, active LOW) |
| Switch pin | `A2` |
| NeoPixel | WS2812B × 1 (onboard NeoKey BFF) |
| NeoPixel pin | `A3` |
| Battery | 650 mAh LiPo, connected to XIAO BAT+/BAT− pads |
| USB-C | Power and battery charging only — no USB HID or serial data |

### 2.1 Pin Notes

- `A2` is configured as `INPUT_PULLUP`; the switch pulls it LOW when pressed.
- `A3` drives the NeoPixel data line directly (3.3 V logic is sufficient for WS2812B at short distances).
- The XIAO nRF52840 charges the LiPo automatically when USB-C is connected. No firmware intervention is required for charging.

---

## 3. Firmware Platform

| Item | Choice |
|---|---|
| Framework | Arduino (Seeed nRF52840 Arduino core, based on Adafruit nRF52 core) |
| BLE HID library | `bluefruit.h` + `BLEHidAdafruit` (included in Adafruit/Seeed nRF52 core) |
| NeoPixel library | `Adafruit_NeoPixel` |
| Language | C++ |

USB data is not used. `Adafruit_TinyUSB` is not initialised.

---

## 4. Configuration Constants

All user-tunable values live in a dedicated `config.h` header so they can be changed without touching logic files.

| Constant | Default | Description |
|---|---|---|
| `PTT_PIN` | `A2` | Switch input pin |
| `NEO_PIN` | `A3` | NeoPixel data pin |
| `NEO_COUNT` | `1` | Number of NeoPixels |
| `NEO_BRIGHTNESS` | `80` | Global brightness (0–255) |
| `HID_MODIFIER` | `0x40` (Right Option) | HID modifier byte sent while button is held; keycode array is all zeros |
| `BLE_DEVICE_NAME` | `"Chirp Button"` | BLE advertised device name |
| `COLOR_ADV` | `0x00, 0x00, 0xFF` (blue) | Hue used while advertising (breathing) |
| `COLOR_IDLE` | `0x00, 0xFF, 0x00` (green) | Hue used while connected idle (breathing) |
| `COLOR_ACTIVE` | `0xFF, 0x00, 0x00` (red) | NeoPixel color while button is held (solid) |
| `COLOR_BOOT` | `0x00, 0x00, 0xFF` (blue) | NeoPixel color during boot flash |
| `DEBOUNCE_MS` | `20` | Button debounce window in milliseconds |
| `BREATHE_PERIOD_MS` | `3000` | Duration of one full breathe cycle (fade in + fade out), in ms |
| `BREATHE_MIN` | `5` | Minimum per-channel brightness at the bottom of the breathe curve (0–255) |
| `BREATHE_MAX` | `180` | Maximum per-channel brightness at the peak of the breathe curve (0–255) |
| `COLOR_CLEAR` | `0x80, 0x00, 0x80` (purple) | NeoPixel color during bond-clear flash sequence |
| `BOND_CLEAR_HOLD_MS` | `5000` | Duration button must be held in `ADVERTISING` state to trigger bond clear |
| `BOND_CLEAR_FLASH_MS` | `3000` | Duration of the purple flash confirmation sequence before reset |
| `BOND_CLEAR_FLASH_PERIOD_MS` | `100` | On+off period of rapid purple flash (10 Hz) |
| `ADV_SLEEP_TIMEOUT_MS` | `300000` (5 min) | Battery-only: time spent in `ADVERTISING` with no connection before entering deep sleep; disabled when USB is connected |
| `SLEEP_TIMEOUT_MS` | `3600000` (1 hour) | Battery-only idle duration in `CONNECTED_IDLE` before entering deep sleep (System OFF); disabled when USB is connected |
| `LED_IDLE_TIMEOUT_MS` | `300000` (5 min) | Battery-only idle duration in `CONNECTED_IDLE` before LED fade-out begins; disabled when USB is connected |
| `LED_FADE_DURATION_MS` | `2000` (2 s) | Duration of the LED fade-out animation in `CONNECTED_IDLE` before the LED is fully off |

---

## 5. Behavior

### 5.1 States

The firmware operates as a four-state machine driven by BLE connection status and button input.

```
  ┌──────────────┐  BLE connected   ┌──────────────┐
  │ ADVERTISING  │ ───────────────► │ CONNECTED    │
  │              │ ◄─────────────── │   IDLE       │
  └──────────────┘  BLE dropped     └──────────────┘
         │                               │  ▲
         │ button held                   │  │ button
         │ BOND_CLEAR_HOLD_MS   pressed  │  │ released
         │ (see §5.6)                    ▼  │
         ▼                         ┌──────────────┐
  ┌──────────────┐                 │ CONNECTED    │
  │    BOND      │                 │   ACTIVE     │
  │   CLEARING   │                 └──────────────┘
  └──────────────┘
         │ after BOND_CLEAR_FLASH_MS
         ▼
  [erase bonds → software reset]

  (battery only)
  ADVERTISING ──── no connection for ADV_SLEEP_TIMEOUT_MS (5 min) ──► ┌─────────────┐
  CONNECTED_IDLE ─── no button press for SLEEP_TIMEOUT_MS (1 hr) ───► │ DEEP_SLEEP  │
                                                                        │ (System OFF)│
                                                                        └─────────────┘
                                                                               │ button press
                                                                               ▼ (full reset → setup())
                                                                        [ADVERTISING]
```

| State | NeoPixel | BLE HID output |
|---|---|---|
| `ADVERTISING` | `COLOR_ADV` breathing (§5.3) | None |
| `CONNECTED_IDLE` | `COLOR_IDLE` breathing (§5.3); fades out and turns off after `LED_IDLE_TIMEOUT_MS` on battery (§5.8) | No key held |
| `CONNECTED_ACTIVE` | `COLOR_ACTIVE` solid | `HID_MODIFIER` held |
| `BOND_CLEARING` | `COLOR_CLEAR` rapid flash (§5.6) | None |
| `DEEP_SLEEP` | Off | None — device in System OFF (< 2 µA) |

### 5.2 State Transitions

- **Any state → ADVERTISING**: BLE connection dropped, or device boots with no bond stored.
- **ADVERTISING → CONNECTED_IDLE**: bonded host connects and HID service is ready.
- **ADVERTISING → BOND_CLEARING**: button held continuously for `BOND_CLEAR_HOLD_MS` (see §5.6).
- **CONNECTED_IDLE → CONNECTED_ACTIVE**: debounced LOW edge on `PTT_PIN`
  - Send BLE HID report with modifier byte `HID_MODIFIER` and all-zero keycodes (Right Option held).
  - Set NeoPixel to `COLOR_ACTIVE`.
- **CONNECTED_ACTIVE → CONNECTED_IDLE**: debounced HIGH edge on `PTT_PIN`
  - Send BLE HID key-up report (empty report).
  - Set NeoPixel to `COLOR_IDLE`.
- **CONNECTED_ACTIVE → ADVERTISING** (connection lost mid-press):
  - Send no further HID reports (connection is gone).
  - Transition to `ADVERTISING` state immediately.
- **BOND_CLEARING → [reset]**: after `BOND_CLEAR_FLASH_MS` elapses, erase bond data and perform a software reset. Device reboots into `ADVERTISING` with no bond stored.
- **ADVERTISING → DEEP_SLEEP**: USB absent and no BLE connection established within `ADV_SLEEP_TIMEOUT_MS` (5 min) since last activity (typically since advertising began). Device enters nRF52840 System OFF (see §5.8).
- **CONNECTED_IDLE → DEEP_SLEEP**: USB absent and no button activity for `SLEEP_TIMEOUT_MS` (1 hour). Device enters System OFF (see §5.8).
- **DEEP_SLEEP → ADVERTISING**: button press exits System OFF via a full hardware reset. `setup()` reruns and advertising begins; any bonded host reconnects automatically.

**LED idle timeout (battery-only, `CONNECTED_IDLE` only):** After `LED_IDLE_TIMEOUT_MS` of inactivity on battery, the green breathe LED fades out over `LED_FADE_DURATION_MS` and turns off. The BLE connection is maintained; the LED resumes breathing immediately on the next button press.

Button presses in `ADVERTISING` are tracked only for the bond-clear hold timer (§5.6). No HID reports are sent.

### 5.3 NeoPixel Breathe Effect

The breathing effect is used in both `ADVERTISING` and `CONNECTED_IDLE` states. It continuously fades the NeoPixel in and out using a sine curve to produce a smooth, natural-looking pulse.

**Algorithm** (runs every main-loop tick):

1. Compute `t = (millis() % BREATHE_PERIOD_MS) / (float)BREATHE_PERIOD_MS` — a 0.0–1.0 ramp that repeats every `BREATHE_PERIOD_MS` ms.
2. Compute `sine = (sin(t × 2π − π/2) + 1.0) / 2.0` — maps the ramp to a 0.0–1.0 sine wave (starts at 0, peaks at 0.5, returns to 0 at 1.0).
3. Compute `brightness = BREATHE_MIN + sine × (BREATHE_MAX − BREATHE_MIN)`.
4. Scale each channel of the active colour by `brightness / 255.0` and write to the NeoPixel.

The active colour (`COLOR_ADV` or `COLOR_IDLE`) sets the hue; `brightness` modulates only the intensity. Switching states (e.g. BLE connects while advertising) resets the breathe phase so the LED does not jump.

When transitioning into `CONNECTED_ACTIVE`, the breathe loop is suspended and the NeoPixel is set to solid `COLOR_ACTIVE` immediately.
When leaving `CONNECTED_ACTIVE`, the breathe loop resumes from phase 0.

### 5.4 BLE Pairing and Bonding

- The device uses BLE bonding (authenticated pairing with key storage). Bond data is stored in the nRF52840's non-volatile flash.
- **First power-up (no bond stored):** advertise generally with `BLE_DEVICE_NAME`. Accept a pairing and bonding request from any host. Once bonded, store the bond and enter `CONNECTED_IDLE`.
- **Subsequent power-ups (bond stored):** begin advertising immediately. The bonded host's OS will recognise the device and reconnect automatically without user interaction. The device remains in `ADVERTISING` (blue breathe) until the host connects.
- Only one bond is stored at a time. If a second host initiates pairing, it overwrites the existing bond.
- To deliberately clear the bond and pair with a new host, use the bond-clear gesture (§5.7).

### 5.5 Boot Sequence

On power-up, before entering the main loop:

1. Set NeoPixel to `COLOR_BOOT`.
2. Wait 300 ms.
3. Turn NeoPixel off.
4. Wait 100 ms.
5. Initialise BLE and begin advertising → enter `ADVERTISING` state.

### 5.6 Bond Clear Mode

Bond clear is a deliberate gesture that erases stored pairing data and resets the device, allowing it to pair with a new host.

**Trigger condition:** button held continuously for `BOND_CLEAR_HOLD_MS` (5 s) while in `ADVERTISING` state.

**Hold timer rules:**
- The timer starts when the debounced button press is first detected in `ADVERTISING` state.
- If the button is released before `BOND_CLEAR_HOLD_MS` elapses, the timer is cancelled and nothing happens.
- If BLE connects while the button is still held (before the 5 s elapse), the timer is cancelled and the device transitions normally to `CONNECTED_IDLE`. The button press is discarded — no HID key is sent.
- If the button remains held for the full `BOND_CLEAR_HOLD_MS`, enter `BOND_CLEARING` immediately.

**BOND_CLEARING sequence:**
1. Flash NeoPixel at `COLOR_CLEAR` (purple) with period `BOND_CLEAR_FLASH_PERIOD_MS` (100 ms on / 100 ms off) for `BOND_CLEAR_FLASH_MS` (3 s).
2. Turn NeoPixel off.
3. Erase all stored bond data from flash.
4. Perform a software reset (`NVIC_SystemReset()`).
5. Device reboots and enters `ADVERTISING` with no bond stored (general advertising, ready to pair with any host).

### 5.7 Debounce

Software debounce is applied to both press and release edges:

- Record the timestamp of any pin state change.
- Only accept the new state if it has been stable for `DEBOUNCE_MS` milliseconds.
- Do not send HID events for transitions that fail the debounce check.

### 5.8 Power Management

#### USB detection

The firmware detects VBUS (USB-C power) by reading the nRF52840 power-management register directly:

```cpp
bool usbPresent = (NRF_POWER->USBREGSTATUS & POWER_USBREGSTATUS_VBUSDETECT_Msk) != 0;
```

No TinyUSB or USB data stack is required. VBUS is present whenever a charger or host supplies 5 V on the USB-C connector, regardless of whether a data connection is established.

#### Idle tracking

`lastActivityTime` is updated on every:
- Debounced button press
- Debounced button release
- BLE connect event
- BLE disconnect event

#### Behaviour when USB is connected

All sleep and LED idle timeout logic is disabled. The device breathes normally in all states indefinitely.

#### Behaviour on battery only

**5-minute advertising timeout → deep sleep (`ADV_SLEEP_TIMEOUT_MS`)**

Checked in `ADVERTISING` each main-loop tick. If USB is absent and `millis() - lastActivityTime ≥ ADV_SLEEP_TIMEOUT_MS` (5 min) with no connection established, enter deep sleep (steps below). `lastActivityTime` is reset when advertising starts (via disconnect callback or `setup()`), so the timer measures time spent advertising without a connection.

**1-hour connected idle → deep sleep (`SLEEP_TIMEOUT_MS`)**

Checked in `CONNECTED_IDLE` each main-loop tick. If USB is absent and `millis() - lastActivityTime ≥ SLEEP_TIMEOUT_MS` (1 hour) with no button activity:

1. Turn NeoPixel off.
2. Configure `PTT_PIN` as SENSE_LOW GPIO wakeup source (with pull-up).
3. Call `sd_power_system_off()` (SoftDevice-safe System OFF entry; < 2 µA quiescent).
4. Device wakes on button press via a full nRF52840 reset → `setup()` reruns → advertising begins.

**5-minute idle → LED fade-out (`LED_IDLE_TIMEOUT_MS`)**

Checked in `CONNECTED_IDLE` only. If USB is absent and `millis() - lastActivityTime ≥ LED_IDLE_TIMEOUT_MS`:

1. Begin a linear fade-out of the green breathe LED over `LED_FADE_DURATION_MS` (2 s) using `showBreatheScaled()`.
2. After the fade completes, call `showOff()` and mark `idleLedOff = true`.
3. BLE connection is unaffected; the device remains in `CONNECTED_IDLE`.
4. On the next button press, `idleLedOff` and `idleLedFading` are cleared and the green breathe resumes immediately.

---

## 6. BLE Profile

| Attribute | Value |
|---|---|
| Role | BLE Peripheral (HID device) |
| Transport | BLE only — USB-C is power/charging only |
| HID type | Keyboard (Boot-compatible keyboard descriptor) |
| Device name | `BLE_DEVICE_NAME` (`"Chirp Button"`) |
| Appearance | Generic HID keyboard (`0x03C1`) |
| Security | Bonded, authenticated (MITM protection optional in v1) |

No mouse, consumer, or gamepad HID endpoints are needed.

---

## 7. Power and Charging

- The 650 mAh LiPo is charged automatically by the XIAO nRF52840's onboard charger whenever USB-C is connected.
- Firmware does not manage charging or monitor battery voltage.
- The device operates normally while charging.
- Firmware detects USB-C VBUS via `NRF_POWER->USBREGSTATUS` and uses this to disable sleep and LED idle timeouts while USB is connected (see §5.8).

---

## 8. Implementation Steps

- [x] **Step 1 — Project scaffold**: Create Arduino sketch structure (`chirp_button.ino`, `config.h`).
- [x] **Step 2 — NeoPixel init**: Initialise `Adafruit_NeoPixel`, run boot sequence (§5.5).
- [x] **Step 3 — Button init**: Configure `PTT_PIN` as `INPUT_PULLUP`.
- [x] **Step 4 — BLE HID init**: Configure `Bluefruit`, `BLEHidAdafruit`, and `BLEDis` (device info service). Set device name and appearance. Begin advertising.
- [x] **Step 5 — Main loop**: Read `PTT_PIN` with debounce (§5.7), implement state machine (§5.1–5.2), drive NeoPixel breathe effect (§5.3) during `ADVERTISING` and `CONNECTED_IDLE`, implement bond-clear hold timer and `BOND_CLEARING` flash sequence (§5.6).
- [x] **Step 6 — Power management**: Implement USB VBUS detection, deep sleep (System OFF) after 1-hour battery idle, 5-minute LED fade-out in `CONNECTED_IDLE` on battery, and 5-minute advertising timeout (§5.8).
- [ ] **Step 7 — Validate**: Pair with host OS, confirm key events arrive in OS key viewer, confirm NeoPixel transitions match spec, confirm button ignored when disconnected, confirm bond-clear gesture erases bond and reboots into general advertising. Validate all power management scenarios from §5.8 verification checklist.

---

## 9. Out of Scope (v1)

The following are explicitly not part of v1 and must not be implemented without a spec update:

- USB HID (USB-C is power/charging only)
- Battery voltage monitoring or low-battery indication
- Multiple bonded hosts
- Multiple key macros or layers
- Runtime key remapping
- Firmware update over the air (OTA)
