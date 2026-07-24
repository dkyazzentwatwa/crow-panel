# Cypher Keys — Dual-Mode HID (USB + Bluetooth) Design

**Date:** 2026-07-23
**Project:** `projects/21-cypher-keys-hid-deck`
**Status:** design approved, ready for implementation plan

## Context

Project 21 is a USB-HID deck (touch keyboard, macro pad, trackpad) that works
over USB-OTG. This change adds a second output path — **Bluetooth LE HID via the
onboard ESP32-C6** — and a status-bar toggle to pick which one is active. Goal:
use the panel as a wireless keyboard/mouse for the same Mac without the cable.

### Proven on hardware (spike, 2026-07-23)

A throwaway BLE-HID-keyboard sketch (NimBLE host on the P4, esp_hosted VHCI to
the C6 controller) was flashed and **macOS discovered `CypherKeys BLE`, paired
with no passkey (Just Works), and received keystrokes into TextEdit.** So:

- The C6's `esp_hosted` slave firmware (v2.12.3) has Bluetooth enabled; **no C6
  reflash needed**.
- Core 3.3.8 enables the path: `CONFIG_BT_NIMBLE_ENABLED`,
  `CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE`, `CONFIG_ESP_HOSTED_NIMBLE_HCI_VHCI`. The
  Arduino `BLE` lib (incl. `BLEHIDDevice`) compiles for the P4 via
  `CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE`.
- `WiFi.setPins(18,19,17,16,15,14,32)` (the board's hosted SDIO pins) **must**
  precede `BLEDevice::init()`, or the hosted link hangs (same rule as Wi-Fi).
- The BLE build is large (~962 KB vs ~537 KB USB-only) but well within the 3 MB
  app partition.

## Decisions

- **Toggle = USB ↔ BLE**, one active output at a time (XOR). The inactive
  transport stays idle (BLE may remain paired but silent). No "Both" mode in v1.
- **No auth**: Just Works bonding (`ESP_LE_AUTH_BOND`, no passkey), as proven.
- **Flag-gated**: `USE_BLE_HID` (default 0). Full dual-mode build is
  `USBMode=default` + `USE_USB_HID=1` + `USE_BLE_HID=1`.

## Architecture

Both `USBHIDKeyboard` (USB, via `sendReport`) and `BLEHIDDevice` (BLE, via
characteristic notify) accept a **raw HID report**. So a keystroke is translated
to a HID report **once** in `HidBackend`, then routed to the active transport —
no per-transport keycode logic. This keeps `HidDeck`, `HidKeyboard`,
`MacroPresets`, and `Trackpad` unchanged; they still call `backend_.tapKey(...)`
etc.

```
touch/serial → HidDeck → HidBackend
                          ├── translate (Arduino mods+key) → HID report (once)
                          ├── deferred-release scheduling (existing service())
                          └── route by outputMode → { UsbTransport | BleTransport }
```

### Components

- **`src/HidTransport.h`** — a tiny interface both transports implement:
  `begin()`, `bool ready()` (USB always ready; BLE ready only when connected),
  `sendKeyReport(uint8_t mods, const uint8_t keys[6])`,
  `sendConsumer(uint16_t usage)`,
  `sendMouse(uint8_t buttons, int8_t dx, int8_t dy, int8_t wheel)`.
- **`src/UsbTransport.{h,cpp}`** — the existing TinyUSB path moved behind the
  interface. Keyboard via `USBHIDKeyboard::sendReport(KeyReport*)`, consumer via
  `USBHIDConsumerControl`, mouse via `USBHIDMouse`. Compiled only when
  `USE_USB_HID` and `ARDUINO_USB_MODE==0` (unchanged gate).
- **`src/BleTransport.{h,cpp}`** — `BLEHIDDevice` with a **combined report map**
  (keyboard report ID 1, mouse ID 2, consumer ID 3). Owns advertising, Just-Works
  security, `BLEServerCallbacks` for connect/disconnect, and connection state.
  `begin()` reads the board's hosted SDIO pins from
  `activeHardwareProfile().hostedSdio`, calls `WiFi.setPins(...)`, then
  `BLEDevice::init("Cypher Keys")`, builds the HID service, and advertises.
  Compiled only when `USE_BLE_HID`.
- **`src/HidBackend.{h,cpp}`** (refactor) — keeps its public API. Adds:
  - `OutputMode { kUsb, kBle }`, `setOutput()`, `output()`, persisted in NVS
    (`Preferences` ns `cypherkeys`, key `output`).
  - Translation `(uint8_t mods, uint8_t key) → (hidMods, hidUsage)` for ASCII and
    the `kKey*`/F-key constants, plus ASCII-shift handling for `typeText`. This
    is the single source of truth used to build reports for either transport.
  - Routing: `tapKey`/`typeText`/`consumer`/`mouse*` build the report and call
    the **active** transport. A key's press and its deferred release go to the
    **same** transport — the one active when the key went down (captured with the
    pending release), so a mid-press toggle can't strand a key down. `setOutput()`
    flushes any pending release to the previous transport before switching.
  - `bleReady()` / `bleConnected()` accessors for the UI.
- **`config/ProjectConfig.h`** — `USE_BLE_HID` default 0; BLE device name
  constant; optional advertised-appearance constant.

### Data flow (one keystroke)

1. `HidDeck` resolves a key/​macro → `backend_.tapKey(mods, key)`.
2. `HidBackend` translates to `(hidMods, usage)`, builds the press report, sends
   it to the active transport, schedules the release report ~24 ms later.
3. `service()` (each loop) sends the release report to the active transport.
4. If the active transport is **not ready** (BLE selected but unpaired /
   disconnected), the report is dropped and `HidBackend` records "BLE not
   connected" for the status bar (no crash, no queueing in v1).

### UI + control

- **Status-bar output toggle** (a button beside DICTATE/THEME/MODE): shows `USB`
  or `BLE`; in BLE mode it also shows a connection dot (advertising vs
  connected), fed by `backend_.bleConnected()`. Tapping switches mode and
  persists it. When switching to BLE, advertising is (re)started; when switching
  to USB, BLE stays connected but idle.
- **Serial commands** (added to the router): `out usb|ble` (switch + persist),
  `ble` (status: enabled/ready/connected/mode), `ble clear` (erase bonds so macOS
  can re-pair). `status`/`hid` extended to report the output mode and BLE state.

### Gating & builds

| Build | Flags (on `USBMode=default` unless noted) | Result |
|---|---|---|
| mock (matrix, `hwcdc`) | none / `USE_DISPLAY=1` | USB+BLE both absent; Serial mock |
| USB-only | `USE_DISPLAY=1 USE_USB_HID=1` | current behavior |
| BLE-only | `USE_DISPLAY=1 USE_BLE_HID=1` | wireless only (compiles under `hwcdc` too) |
| **dual-mode** | `USE_DISPLAY=1 USE_USB_HID=1 USE_BLE_HID=1` | the deliverable |

The shared flag matrix (single `hwcdc` FQBN) gets a `USE_BLE_HID=1` mock/compile
row. The dual-mode `USBMode=default` build stays a documented separate compile in
TECHNICAL.md (matrix runs one FQBN).

## Error handling

- **BLE init failure** (e.g., a future board with a Wi-Fi-only C6): `BleTransport`
  reports not-ready; the deck stays usable on USB and the toggle shows BLE
  unavailable. No hang (setPins-before-init is honored).
- **BLE selected but disconnected**: keystrokes drop with a status note; toggle
  dot shows "advertising". User pairs, then it flows.
- **Both stacks active** (`USBMode=default` + BLE): independent stacks; only the
  selected one emits. USB CDC serial keeps working for the command console.

## Testing / verification

1. **Compile:** flag-matrix `USE_BLE_HID` mock row green; dual-mode
   `USBMode=default USE_USB_HID=1 USE_BLE_HID=1` build green; other display
   projects still green (no shared changes here beyond reading `hostedSdio`).
2. **Hardware (dual-mode build):**
   - USB path unchanged: type/macros/trackpad land over the cable.
   - Toggle to BLE → pair `Cypher Keys` on macOS (no passkey) → type, fire
     macros, move the trackpad; confirm they land wirelessly.
   - Toggle back to USB mid-session; confirm output follows the toggle.
   - `ble clear` then re-pair works.
3. **Regression:** USB-only build (no `USE_BLE_HID`) is byte-comparable to today.

## Out of scope (v1)

- "Both" simultaneous output; multi-host switching (>1 bonded host UI).
- Passkey/MITM pairing; BLE battery/host-name management UI.
- Queuing keystrokes while BLE is disconnected.
- BLE for anything beyond keyboard + consumer + mouse.
