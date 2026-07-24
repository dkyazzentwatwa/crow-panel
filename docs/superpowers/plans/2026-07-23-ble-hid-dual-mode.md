# Dual-Mode HID (USB + Bluetooth) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add Bluetooth-LE HID output (via the onboard ESP32-C6) alongside the existing USB-OTG HID in Project 21, with a status-bar USB↔BLE toggle, so the panel works as a wireless keyboard/mouse/macropad for a Mac.

**Architecture:** Introduce a `HidTransport` interface with `UsbTransport` (the existing, proven TinyUSB path, moved behind the interface unchanged) and `BleTransport` (`BLEHIDDevice` via the C6). `HidBackend` keeps its public API, gains an `OutputMode`, and routes each action to the active transport. USB keeps its native Arduino keycode path; only BLE translates Arduino key bytes → raw HID usages, so the proven USB behavior is not disturbed. Everything is gated behind a new `USE_BLE_HID` flag (default off).

**Tech Stack:** Arduino-ESP32 core 3.3.8, ESP32-P4, NimBLE-over-esp_hosted (C6 controller), Arduino `BLE` library (`BLEHIDDevice`), `USBHIDKeyboard/Mouse/ConsumerControl`, `Preferences` (NVS).

**Verification note:** This is embedded firmware with no host unit-test harness. Each task is verified by (a) an exact `arduino-cli` compile that must stay green, and (b) Serial smoke commands in mock mode where logic is observable. On-device behavior is proven in the final hardware-acceptance task. This mirrors the repo's compile-verified → hardware-proven convention.

**Branch:** Work on a branch off `main` (repo currently has uncommitted Project 21 work; do not disturb it). Before Task 1:
```bash
cd /Users/cypher/Documents/GitHub/crow-panel
git checkout -b ble-hid-dual-mode
```

**Common build commands used below** (run from repo root):
```bash
# MOCK build (suite default hwcdc). Must always stay green.
CTAGS_WORKAROUND=1 EXTRA_FLAGS="-DUSE_DISPLAY=1" ./scripts/compile-all.sh   # (or the single-project compile below)

# Single-project MOCK compile (faster):
ROOT=/Users/cypher/Documents/GitHub/crow-panel
HWCDC="esp32:esp32:esp32p4:USBMode=hwcdc,PSRAM=enabled,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,UploadSpeed=921600"
CK(){ arduino-cli compile --fqbn "$1" --libraries "$ROOT/shared" \
  --build-path "$ROOT/_arduino-build/21-$3" \
  --build-property "tools.ctags.cmd.path=/usr/bin/true" \
  ${2:+--build-property "compiler.cpp.extra_flags=$2"} \
  "$ROOT/projects/21-cypher-keys-hid-deck"; }

# DUAL-MODE real build (USB-OTG + BLE):
OTG="esp32:esp32:esp32p4:USBMode=default,PSRAM=enabled,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,UploadSpeed=921600"
# CK "$OTG" "-DUSE_DISPLAY=1 -DUSE_USB_HID=1 -DUSE_BLE_HID=1" dual
```

---

## File Structure

Create under `projects/21-cypher-keys-hid-deck/src/`:
- `HidTransport.h` — abstract transport interface (keyboard/consumer/mouse primitives + `ready()`).
- `HidKeycodes.{h,cpp}` — pure translation: Arduino key byte + mods bitmask → HID usage + HID modifier byte (BLE only).
- `UsbTransport.{h,cpp}` — the existing TinyUSB path behind the interface (behavior unchanged).
- `BleTransport.{h,cpp}` — `BLEHIDDevice` keyboard+mouse+consumer, advertising, Just-Works security, connection state.

Modify:
- `config/ProjectConfig.h` — `USE_BLE_HID` (default 0), `CYPHER_KEYS_BLE_NAME`.
- `src/HidBackend.{h,cpp}` — hold transports, `OutputMode`, routing, NVS persist, ble accessors; deferred release becomes per-transport.
- `src/HidDeck.{h,cpp}` — status-bar output toggle button + hit test + status display + `commandOutput`/`commandBle`.
- `21-cypher-keys-hid-deck.ino` — register `out` and `ble` serial commands.
- `scripts/check-flag-matrix.sh` — add a `USE_BLE_HID` compile row.
- `README.md`, `TECHNICAL.md`, `docs/full-port-proof-matrix.md`, `docs/hardware-risk-register.md` — document dual-mode.

---

## Task 1: Add the `USE_BLE_HID` flag and BLE name

**Files:**
- Modify: `projects/21-cypher-keys-hid-deck/config/ProjectConfig.h`

- [ ] **Step 1: Add the flag + name** near `USE_USB_HID`:

```c
// Bluetooth-LE HID output via the onboard ESP32-C6 (NimBLE host on the P4,
// esp_hosted VHCI to the C6 radio). Off by default: it adds ~425 KB and needs
// the C6. Does NOT require USB-OTG (compiles under hwcdc too), but the full
// dual-mode deliverable is USBMode=default + USE_USB_HID=1 + USE_BLE_HID=1.
#ifndef USE_BLE_HID
#define USE_BLE_HID 0
#endif

// Name shown in the host's Bluetooth device list.
#ifndef CYPHER_KEYS_BLE_NAME
#define CYPHER_KEYS_BLE_NAME "Cypher Keys"
#endif
```

- [ ] **Step 2: Compile mock (nothing uses it yet; must stay green)**

Run: `CK "$HWCDC" "-DUSE_DISPLAY=1" display`
Expected: `Sketch uses ... bytes` (no error).

- [ ] **Step 3: Commit**

```bash
git add projects/21-cypher-keys-hid-deck/config/ProjectConfig.h
git commit -m "feat(21): add USE_BLE_HID flag + BLE device name"
```

---

## Task 2: HidTransport interface

**Files:**
- Create: `projects/21-cypher-keys-hid-deck/src/HidTransport.h`

- [ ] **Step 1: Write the interface**

```cpp
#ifndef CYPHER_KEYS_HID_TRANSPORT_H
#define CYPHER_KEYS_HID_TRANSPORT_H

#include <Arduino.h>

// One HID output path (USB or BLE). HidBackend builds semantic actions and
// routes them to the active transport. `key` bytes are the same space the rest
// of the app uses: ASCII (<0x80) or the kKey*/Arduino KEY_* constants (>=0x80).
// UsbTransport consumes them via the Arduino Keyboard API; BleTransport
// translates them to raw HID usages. Mouse buttons are the MOUSE_LEFT/RIGHT
// bitmask (1=left, 2=right).
class HidTransport {
 public:
  virtual ~HidTransport() {}
  virtual void begin() {}
  virtual bool ready() const = 0;  // can it deliver a report right now?
  virtual const char *name() const = 0;

  virtual void keyDown(uint8_t mods, uint8_t key) = 0;  // hold mods+key
  virtual void keyUp() = 0;                             // release all keys

  virtual void consumerDown(uint16_t usage) = 0;
  virtual void consumerUp() = 0;

  virtual void mouseMove(int8_t dx, int8_t dy) = 0;
  virtual void mouseDown(uint8_t button) = 0;
  virtual void mouseUp(uint8_t button) = 0;
  virtual void mouseWheel(int8_t wheel) = 0;
};

#endif
```

- [ ] **Step 2: Commit**

```bash
git add projects/21-cypher-keys-hid-deck/src/HidTransport.h
git commit -m "feat(21): add HidTransport interface"
```

---

## Task 3: HidKeycodes translation (Arduino key → HID usage)

**Files:**
- Create: `projects/21-cypher-keys-hid-deck/src/HidKeycodes.{h,cpp}`

This is BLE-only translation. It must agree with `HidTypes.h` constants and the ASCII characters the on-screen keyboard emits.

- [ ] **Step 1: Header**

```cpp
#ifndef CYPHER_KEYS_HID_KEYCODES_H
#define CYPHER_KEYS_HID_KEYCODES_H

#include "HidTypes.h"
#include <Arduino.h>

// Translate one app key (ASCII or kKey*/KEY_* constant) to a USB HID usage and
// whether it needs Shift. Returns false if the key has no mapping.
bool hidUsageForKey(uint8_t key, uint8_t &usage, bool &needsShift);

// Convert the app's HidMod bitmask (kModCmd/Shift/Opt/Ctrl) to a HID modifier
// byte (left GUI/Shift/Alt/Ctrl bits).
uint8_t hidModifierByte(uint8_t mods);

#endif
```

- [ ] **Step 2: Implementation** (complete table)

```cpp
#include "HidKeycodes.h"

uint8_t hidModifierByte(uint8_t mods) {
  uint8_t b = 0;
  if (mods & kModCtrl) b |= 0x01;   // Left Ctrl
  if (mods & kModShift) b |= 0x02;  // Left Shift
  if (mods & kModOpt) b |= 0x04;    // Left Alt/Option
  if (mods & kModCmd) b |= 0x08;    // Left GUI/Command
  return b;
}

bool hidUsageForKey(uint8_t key, uint8_t &usage, bool &needsShift) {
  needsShift = false;

  // Letters.
  if (key >= 'a' && key <= 'z') { usage = 0x04 + (key - 'a'); return true; }
  if (key >= 'A' && key <= 'Z') { usage = 0x04 + (key - 'A'); needsShift = true; return true; }

  // Digits (1-9 then 0).
  if (key >= '1' && key <= '9') { usage = 0x1E + (key - '1'); return true; }
  if (key == '0') { usage = 0x27; return true; }

  // Unshifted punctuation.
  switch (key) {
    case ' ': usage = 0x2C; return true;
    case '-': usage = 0x2D; return true;
    case '=': usage = 0x2E; return true;
    case '[': usage = 0x2F; return true;
    case ']': usage = 0x30; return true;
    case '\\': usage = 0x31; return true;
    case ';': usage = 0x33; return true;
    case '\'': usage = 0x34; return true;
    case '`': usage = 0x35; return true;
    case ',': usage = 0x36; return true;
    case '.': usage = 0x37; return true;
    case '/': usage = 0x38; return true;
  }

  // Shifted punctuation (base usage + Shift).
  switch (key) {
    case '!': usage = 0x1E; needsShift = true; return true;
    case '@': usage = 0x1F; needsShift = true; return true;
    case '#': usage = 0x20; needsShift = true; return true;
    case '$': usage = 0x21; needsShift = true; return true;
    case '%': usage = 0x22; needsShift = true; return true;
    case '^': usage = 0x23; needsShift = true; return true;
    case '&': usage = 0x24; needsShift = true; return true;
    case '*': usage = 0x25; needsShift = true; return true;
    case '(': usage = 0x26; needsShift = true; return true;
    case ')': usage = 0x27; needsShift = true; return true;
    case '_': usage = 0x2D; needsShift = true; return true;
    case '+': usage = 0x2E; needsShift = true; return true;
    case '{': usage = 0x2F; needsShift = true; return true;
    case '}': usage = 0x30; needsShift = true; return true;
    case '|': usage = 0x31; needsShift = true; return true;
    case ':': usage = 0x33; needsShift = true; return true;
    case '"': usage = 0x34; needsShift = true; return true;
    case '~': usage = 0x35; needsShift = true; return true;
    case '<': usage = 0x36; needsShift = true; return true;
    case '>': usage = 0x37; needsShift = true; return true;
    case '?': usage = 0x38; needsShift = true; return true;
  }

  // Special keys (kKey* / Arduino KEY_* constants from HidTypes.h).
  switch (key) {
    case kKeyReturn: usage = 0x28; return true;
    case kKeyEsc: usage = 0x29; return true;
    case kKeyBackspace: usage = 0x2A; return true;
    case kKeyTab: usage = 0x2B; return true;
    case kKeyRightArrow: usage = 0x4F; return true;
    case kKeyLeftArrow: usage = 0x50; return true;
    case kKeyDownArrow: usage = 0x51; return true;
    case kKeyUpArrow: usage = 0x52; return true;
    case kKeyF1: usage = 0x3A; return true;
    case kKeyF2: usage = 0x3B; return true;
    case kKeyF3: usage = 0x3C; return true;
    case kKeyF4: usage = 0x3D; return true;
    case kKeyF5: usage = 0x3E; return true;
    case kKeyF6: usage = 0x3F; return true;
    case kKeyF7: usage = 0x40; return true;
    case kKeyF8: usage = 0x41; return true;
    case kKeyF9: usage = 0x42; return true;
    case kKeyF10: usage = 0x43; return true;
    case kKeyF11: usage = 0x44; return true;
    case kKeyF12: usage = 0x45; return true;
  }
  return false;
}
```

- [ ] **Step 3: Compile mock** (translation is unused yet; just ensure it builds when referenced later — for now compile the project to confirm no syntax error is introduced by the new TU once it's referenced; skip until Task 6). For now:

Run: `CK "$HWCDC" "-DUSE_DISPLAY=1" display`
Expected: green (the new files are not yet included anywhere, so this only proves nothing broke).

- [ ] **Step 4: Commit**

```bash
git add projects/21-cypher-keys-hid-deck/src/HidKeycodes.h projects/21-cypher-keys-hid-deck/src/HidKeycodes.cpp
git commit -m "feat(21): add HID keycode translation for BLE"
```

---

## Task 4: UsbTransport (extract the proven USB path)

**Files:**
- Create: `projects/21-cypher-keys-hid-deck/src/UsbTransport.{h,cpp}`

Move the current `HidBackend` USB calls here **verbatim** (same TinyUSB globals, same behavior). Gate identically to today (`USE_USB_HID && ARDUINO_USB_MODE==0`). When not a USB-OTG build, `ready()` returns false and the ops are no-ops.

- [ ] **Step 1: Header**

```cpp
#ifndef CYPHER_KEYS_USB_TRANSPORT_H
#define CYPHER_KEYS_USB_TRANSPORT_H

#include "../config/ProjectConfig.h"
#include "HidTransport.h"

#if USE_USB_HID && defined(ARDUINO_USB_MODE) && (ARDUINO_USB_MODE == 0)
#define CYPHER_KEYS_USB_LIVE 1
#else
#define CYPHER_KEYS_USB_LIVE 0
#endif

class UsbTransport : public HidTransport {
 public:
  void begin() override;
  bool ready() const override { return CYPHER_KEYS_USB_LIVE; }
  const char *name() const override { return "USB"; }
  void keyDown(uint8_t mods, uint8_t key) override;
  void keyUp() override;
  void consumerDown(uint16_t usage) override;
  void consumerUp() override;
  void mouseMove(int8_t dx, int8_t dy) override;
  void mouseDown(uint8_t button) override;
  void mouseUp(uint8_t button) override;
  void mouseWheel(int8_t wheel) override;
};

#endif
```

- [ ] **Step 2: Implementation** (uses the exact Arduino calls the current HidBackend uses)

```cpp
#include "UsbTransport.h"
#include "HidTypes.h"

#if CYPHER_KEYS_USB_LIVE
#include "USB.h"
#include "USBHIDConsumerControl.h"
#include "USBHIDKeyboard.h"
#include "USBHIDMouse.h"
namespace {
USBHIDKeyboard gKeyboard;
USBHIDMouse gMouse;
USBHIDConsumerControl gConsumer;
void pressMods(uint8_t mods) {
  if (mods & kModCmd) gKeyboard.press(KEY_LEFT_GUI);
  if (mods & kModShift) gKeyboard.press(KEY_LEFT_SHIFT);
  if (mods & kModOpt) gKeyboard.press(KEY_LEFT_ALT);
  if (mods & kModCtrl) gKeyboard.press(KEY_LEFT_CTRL);
}
}  // namespace
#endif

void UsbTransport::begin() {
#if CYPHER_KEYS_USB_LIVE
  gKeyboard.begin();
  gMouse.begin();
  gConsumer.begin();
  USB.begin();
#endif
}

void UsbTransport::keyDown(uint8_t mods, uint8_t key) {
#if CYPHER_KEYS_USB_LIVE
  pressMods(mods);
  if (key) gKeyboard.press(key);
#else
  (void)mods; (void)key;
#endif
}
void UsbTransport::keyUp() {
#if CYPHER_KEYS_USB_LIVE
  gKeyboard.releaseAll();
#endif
}
void UsbTransport::consumerDown(uint16_t usage) {
#if CYPHER_KEYS_USB_LIVE
  gConsumer.press(usage);
#else
  (void)usage;
#endif
}
void UsbTransport::consumerUp() {
#if CYPHER_KEYS_USB_LIVE
  gConsumer.release();
#endif
}
void UsbTransport::mouseMove(int8_t dx, int8_t dy) {
#if CYPHER_KEYS_USB_LIVE
  gMouse.move(dx, dy);
#else
  (void)dx; (void)dy;
#endif
}
void UsbTransport::mouseDown(uint8_t button) {
#if CYPHER_KEYS_USB_LIVE
  gMouse.press(button);
#else
  (void)button;
#endif
}
void UsbTransport::mouseUp(uint8_t button) {
#if CYPHER_KEYS_USB_LIVE
  gMouse.release(button);
#else
  (void)button;
#endif
}
void UsbTransport::mouseWheel(int8_t wheel) {
#if CYPHER_KEYS_USB_LIVE
  gMouse.move(0, 0, wheel);
#else
  (void)wheel;
#endif
}
```

- [ ] **Step 3: Commit** (not wired yet)

```bash
git add projects/21-cypher-keys-hid-deck/src/UsbTransport.h projects/21-cypher-keys-hid-deck/src/UsbTransport.cpp
git commit -m "feat(21): add UsbTransport wrapping the proven TinyUSB path"
```

---

## Task 5: BleTransport

**Files:**
- Create: `projects/21-cypher-keys-hid-deck/src/BleTransport.{h,cpp}`

Combined keyboard(ID 1) + mouse(ID 2) + consumer(ID 3) HID device. Sets the hosted SDIO pins before `BLEDevice::init`. Just-Works bonding. When `USE_BLE_HID==0`, the whole class is a stub returning `ready()==false`.

- [ ] **Step 1: Header**

```cpp
#ifndef CYPHER_KEYS_BLE_TRANSPORT_H
#define CYPHER_KEYS_BLE_TRANSPORT_H

#include "../config/ProjectConfig.h"
#include "HidTransport.h"

class BleTransport : public HidTransport {
 public:
  void begin() override;
  bool ready() const override;  // true only while a host is connected
  const char *name() const override { return "BLE"; }
  bool advertising() const;     // stack up and advertising (may be unpaired)
  void clearBonds();            // erase pairings so a host can re-pair

  void keyDown(uint8_t mods, uint8_t key) override;
  void keyUp() override;
  void consumerDown(uint16_t usage) override;
  void consumerUp() override;
  void mouseMove(int8_t dx, int8_t dy) override;
  void mouseDown(uint8_t button) override;
  void mouseUp(uint8_t button) override;
  void mouseWheel(int8_t wheel) override;

 private:
  uint8_t mouseButtons_ = 0;    // held button bitmask for report assembly
};

#endif
```

- [ ] **Step 2: Implementation**

```cpp
#include "BleTransport.h"

#include "HidKeycodes.h"

#if USE_BLE_HID
#include <WiFi.h>  // WiFi.setPins() configures the shared esp_hosted SDIO link
#include <BLEDevice.h>
#include <BLEHIDDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <CrowPanelShared.h>  // activeHardwareProfile() for hosted SDIO pins

// NimBLE host API to erase all bonds (this core is NimBLE, not Bluedroid).
extern "C" int ble_store_clear(void);

namespace {
BLEHIDDevice *gHid = nullptr;
BLECharacteristic *gKbd = nullptr;       // report ID 1
BLECharacteristic *gMouse = nullptr;     // report ID 2
BLECharacteristic *gConsumer = nullptr;  // report ID 3
volatile bool gConnected = false;
bool gStarted = false;

// Combined report descriptor: keyboard (1), mouse (2), consumer (3).
const uint8_t kReportMap[] = {
    // Keyboard, Report ID 1
    0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x85, 0x01, 0x05, 0x07, 0x19, 0xE0,
    0x29, 0xE7, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02,
    0x95, 0x01, 0x75, 0x08, 0x81, 0x03, 0x95, 0x06, 0x75, 0x08, 0x15, 0x00,
    0x25, 0x65, 0x05, 0x07, 0x19, 0x00, 0x29, 0x65, 0x81, 0x00, 0xC0,
    // Mouse, Report ID 2 (3 buttons, X, Y, wheel)
    0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x85, 0x02, 0x09, 0x01, 0xA1, 0x00,
    0x05, 0x09, 0x19, 0x01, 0x29, 0x03, 0x15, 0x00, 0x25, 0x01, 0x95, 0x03,
    0x75, 0x01, 0x81, 0x02, 0x95, 0x01, 0x75, 0x05, 0x81, 0x03, 0x05, 0x01,
    0x09, 0x30, 0x09, 0x31, 0x09, 0x38, 0x15, 0x81, 0x25, 0x7F, 0x75, 0x08,
    0x95, 0x03, 0x81, 0x06, 0xC0, 0xC0,
    // Consumer, Report ID 3 (16-bit usage)
    0x05, 0x0C, 0x09, 0x01, 0xA1, 0x01, 0x85, 0x03, 0x15, 0x00, 0x26, 0xFF,
    0x03, 0x19, 0x00, 0x2A, 0xFF, 0x03, 0x75, 0x10, 0x95, 0x01, 0x81, 0x00,
    0xC0};

class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer *) override { gConnected = true; }
  void onDisconnect(BLEServer *s) override {
    gConnected = false;
    s->getAdvertising()->start();
  }
};

void sendKbd(uint8_t mods, uint8_t usage) {
  if (!gConnected || gKbd == nullptr) return;
  uint8_t report[8] = {mods, 0, usage, 0, 0, 0, 0, 0};
  gKbd->setValue(report, 8);
  gKbd->notify();
}
void sendMouse(uint8_t buttons, int8_t dx, int8_t dy, int8_t wheel) {
  if (!gConnected || gMouse == nullptr) return;
  uint8_t report[4] = {buttons, (uint8_t)dx, (uint8_t)dy, (uint8_t)wheel};
  gMouse->setValue(report, 4);
  gMouse->notify();
}
void sendConsumer(uint16_t usage) {
  if (!gConnected || gConsumer == nullptr) return;
  uint8_t report[2] = {(uint8_t)(usage & 0xFF), (uint8_t)(usage >> 8)};
  gConsumer->setValue(report, 2);
  gConsumer->notify();
}
}  // namespace
#endif  // USE_BLE_HID

void BleTransport::begin() {
#if USE_BLE_HID
  const HostedSdioPins &p = activeHardwareProfile().hostedSdio;
  WiFi.setPins(p.clk, p.cmd, p.d0, p.d1, p.d2, p.d3, p.reset);  // before init!
  BLEDevice::init(CYPHER_KEYS_BLE_NAME);

  BLEServer *server = BLEDevice::createServer();
  server->setCallbacks(new ServerCallbacks());

  gHid = new BLEHIDDevice(server);
  gKbd = gHid->inputReport(1);
  gMouse = gHid->inputReport(2);
  gConsumer = gHid->inputReport(3);
  gHid->manufacturer()->setValue("Cypher");
  gHid->pnp(0x02, 0xE502, 0xA111, 0x0210);
  gHid->hidInfo(0x00, 0x01);

  BLESecurity *sec = new BLESecurity();
  sec->setAuthenticationMode(ESP_LE_AUTH_BOND);  // Just Works, no passkey

  gHid->reportMap((uint8_t *)kReportMap, sizeof(kReportMap));
  gHid->startServices();
  gHid->setBatteryLevel(100);

  BLEAdvertising *adv = server->getAdvertising();
  adv->setAppearance(HID_KEYBOARD);
  adv->addServiceUUID(gHid->hidService()->getUUID());
  adv->start();
  gStarted = true;
#endif
}

bool BleTransport::ready() const {
#if USE_BLE_HID
  return gConnected;
#else
  return false;
#endif
}
bool BleTransport::advertising() const {
#if USE_BLE_HID
  return gStarted;
#else
  return false;
#endif
}
void BleTransport::clearBonds() {
#if USE_BLE_HID
  // The P4 core is NimBLE-backed (not Bluedroid), so esp_ble_* bond APIs are
  // unavailable. ble_store_clear() wipes all NimBLE bonds so the host re-pairs.
  extern "C" int ble_store_clear(void);
  ble_store_clear();
#endif
}

void BleTransport::keyDown(uint8_t mods, uint8_t key) {
#if USE_BLE_HID
  uint8_t usage = 0;
  bool needsShift = false;
  if (!hidUsageForKey(key, usage, needsShift)) return;
  uint8_t modByte = hidModifierByte(mods);
  if (needsShift) modByte |= 0x02;  // add Left Shift
  sendKbd(modByte, usage);
#else
  (void)mods; (void)key;
#endif
}
void BleTransport::keyUp() {
#if USE_BLE_HID
  sendKbd(0, 0);
#endif
}
void BleTransport::consumerDown(uint16_t usage) {
#if USE_BLE_HID
  sendConsumer(usage);
#else
  (void)usage;
#endif
}
void BleTransport::consumerUp() {
#if USE_BLE_HID
  sendConsumer(0);
#endif
}
void BleTransport::mouseMove(int8_t dx, int8_t dy) {
#if USE_BLE_HID
  sendMouse(mouseButtons_, dx, dy, 0);
#else
  (void)dx; (void)dy;
#endif
}
void BleTransport::mouseDown(uint8_t button) {
#if USE_BLE_HID
  mouseButtons_ |= button;
  sendMouse(mouseButtons_, 0, 0, 0);
#else
  (void)button;
#endif
}
void BleTransport::mouseUp(uint8_t button) {
#if USE_BLE_HID
  mouseButtons_ &= ~button;
  sendMouse(mouseButtons_, 0, 0, 0);
#else
  (void)button;
#endif
}
void BleTransport::mouseWheel(int8_t wheel) {
#if USE_BLE_HID
  sendMouse(mouseButtons_, 0, 0, wheel);
#else
  (void)wheel;
#endif
}
```

- [ ] **Step 3: Compile the BLE build in isolation** (proves the transport + BLE libs link; not yet wired):

Add a temporary reference is unnecessary — an unreferenced .cpp still compiles as part of the sketch. Compile:

Run: `CK "$OTG" "-DUSE_DISPLAY=1 -DUSE_USB_HID=1 -DUSE_BLE_HID=1" dual`
Expected: green, `Sketch uses ~9xx KB`. (First BLE compile is slow — allow several minutes.)

- [ ] **Step 4: Commit**

```bash
git add projects/21-cypher-keys-hid-deck/src/BleTransport.h projects/21-cypher-keys-hid-deck/src/BleTransport.cpp
git commit -m "feat(21): add BleTransport (BLEHIDDevice via C6)"
```

---

## Task 6: Route HidBackend through transports

**Files:**
- Modify: `projects/21-cypher-keys-hid-deck/src/HidBackend.h`
- Modify: `projects/21-cypher-keys-hid-deck/src/HidBackend.cpp`

Replace the direct TinyUSB calls with transport routing. Add `OutputMode`, NVS persistence, BLE accessors, and per-transport deferred release.

- [ ] **Step 1: Header changes** — add includes, enum, members, methods. Replace the old `CYPHER_KEYS_HID_LIVE` block usage. New relevant header content:

```cpp
#include "UsbTransport.h"
#include "BleTransport.h"

enum HidOutput : uint8_t { kOutputUsb = 0, kOutputBle = 1 };

class HidBackend {
 public:
  void begin(Print *log, EventLog *events);
  void service(uint32_t nowMs);

  // Output selection.
  HidOutput output() const { return output_; }
  void setOutput(HidOutput out);   // flushes pending release, persists to NVS
  const char *modeLabel() const;   // "USB" / "BLE" / "MOCK"
  bool usbLive() const;
  bool bleReady() const;           // BLE compiled AND a host connected
  bool bleAdvertising() const;
  void bleClearBonds();

  bool live() const;               // active transport ready
  uint32_t reportsSent() const { return reports_; }
  const String &lastAction() const { return lastAction_; }

  void typeText(const String &text);
  void tapKey(uint8_t mods, uint8_t key);
  void consumer(uint16_t usage);
  void mouseMove(int16_t dx, int16_t dy);
  void mouseButton(uint8_t button, bool pressed);
  void mouseClick(uint8_t button);
  void mouseScroll(int8_t amount);
  void fireMacro(const MacroSlot &slot);

 private:
  void record(const String &action);
  HidTransport *active();          // current transport, or nullptr
  void loadOutput();
  void persistOutput() const;

  Print *log_ = nullptr;
  EventLog *events_ = nullptr;
  uint32_t reports_ = 0;
  uint32_t moves_ = 0;
  String lastAction_ = "(none)";
  HidOutput output_ = kOutputUsb;

  UsbTransport usb_;
  BleTransport ble_;

  static const uint32_t kHoldMs = 24;
  bool keyHeld_ = false;
  uint32_t keyReleaseDueMs_ = 0;
  HidTransport *keyHeldOn_ = nullptr;   // transport that owns the pending release
  bool consumerHeld_ = false;
  uint32_t consumerReleaseDueMs_ = 0;
  HidTransport *consumerHeldOn_ = nullptr;
};
```

Keep the existing free functions `hidModPrefix` / `hidKeyName` declarations.

- [ ] **Step 2: Implementation changes** — replace the body. Key methods:

```cpp
#include "HidBackend.h"
#include <Preferences.h>
#include <CrowPanelShared.h>  // EventLog

// ... keep clamp8, hidModPrefix, hidKeyName unchanged ...

void HidBackend::begin(Print *log, EventLog *events) {
  log_ = log;
  events_ = events;
  usb_.begin();
  ble_.begin();
  loadOutput();
  if (log_) {
    log_->print("[hid] output=");
    log_->print(modeLabel());
    log_->println(usb_.ready() || ble_.ready() ? " (live)" : " (mock: logging only)");
  }
}

void HidBackend::loadOutput() {
  Preferences prefs;
  if (prefs.begin(CYPHER_KEYS_NVS_NAMESPACE, true)) {
    uint32_t v = prefs.getUInt("output", 0);
    prefs.end();
    if (v == kOutputBle) output_ = kOutputBle;
  }
}
void HidBackend::persistOutput() const {
  Preferences prefs;
  if (prefs.begin(CYPHER_KEYS_NVS_NAMESPACE, false)) {
    prefs.putUInt("output", (uint32_t)output_);
    prefs.end();
  }
}

HidTransport *HidBackend::active() {
  HidTransport *t = (output_ == kOutputBle) ? (HidTransport *)&ble_ : (HidTransport *)&usb_;
  return t;
}

void HidBackend::setOutput(HidOutput out) {
  if (out == output_) return;
  // Flush any pending release on the transport that owns it before switching.
  if (keyHeld_ && keyHeldOn_) { keyHeldOn_->keyUp(); keyHeld_ = false; keyHeldOn_ = nullptr; }
  if (consumerHeld_ && consumerHeldOn_) { consumerHeldOn_->consumerUp(); consumerHeld_ = false; consumerHeldOn_ = nullptr; }
  output_ = out;
  persistOutput();
  record(String("output -> ") + modeLabel());
}

bool HidBackend::usbLive() const { return usb_.ready(); }
bool HidBackend::bleReady() const { return ble_.ready(); }
bool HidBackend::bleAdvertising() const { return ble_.advertising(); }
void HidBackend::bleClearBonds() { ble_.clearBonds(); record("ble bonds cleared"); }

bool HidBackend::live() const {
  return (output_ == kOutputBle) ? ble_.ready() : usb_.ready();
}
const char *HidBackend::modeLabel() const {
  if (output_ == kOutputBle) return ble_.ready() ? "BLE" : "BLE?";
  return usb_.ready() ? "USB" : "MOCK";
}

void HidBackend::service(uint32_t nowMs) {
  if (keyHeld_ && (int32_t)(nowMs - keyReleaseDueMs_) >= 0) {
    if (keyHeldOn_) keyHeldOn_->keyUp();
    keyHeld_ = false; keyHeldOn_ = nullptr;
  }
  if (consumerHeld_ && (int32_t)(nowMs - consumerReleaseDueMs_) >= 0) {
    if (consumerHeldOn_) consumerHeldOn_->consumerUp();
    consumerHeld_ = false; consumerHeldOn_ = nullptr;
  }
}

void HidBackend::tapKey(uint8_t mods, uint8_t key) {
  HidTransport *t = active();
  if (t) {
    if (keyHeld_ && keyHeldOn_) keyHeldOn_->keyUp();  // flush previous
    t->keyDown(mods, key);
    keyHeld_ = true;
    keyHeldOn_ = t;
    keyReleaseDueMs_ = millis() + kHoldMs;
  }
  record("key " + hidModPrefix(mods) + hidKeyName(key));
}

void HidBackend::typeText(const String &text) {
  if (text.length() == 0) return;
  HidTransport *t = active();
  if (t) {
    for (size_t i = 0; i < text.length(); ++i) {
      t->keyDown(0, (uint8_t)text[i]);
      t->keyUp();
      delay(5);
    }
  }
  String preview = text;
  if (preview.length() > 40) preview = preview.substring(0, 40) + "...";
  record("type \"" + preview + "\"");
}

void HidBackend::consumer(uint16_t usage) {
  HidTransport *t = active();
  if (t) {
    if (consumerHeld_ && consumerHeldOn_) consumerHeldOn_->consumerUp();
    t->consumerDown(usage);
    consumerHeld_ = true;
    consumerHeldOn_ = t;
    consumerReleaseDueMs_ = millis() + kHoldMs;
  }
  record("media 0x" + String(usage, HEX));
}

void HidBackend::mouseMove(int16_t dx, int16_t dy) {
  if (dx == 0 && dy == 0) return;
  HidTransport *t = active();
  if (t) t->mouseMove(clamp8(dx), clamp8(dy));
  ++moves_;
}
void HidBackend::mouseButton(uint8_t button, bool pressed) {
  HidTransport *t = active();
  if (t) { if (pressed) t->mouseDown(button); else t->mouseUp(button); }
  record(String("mouse ") + (button == 2 ? "right " : "left ") + (pressed ? "down" : "up"));
}
void HidBackend::mouseClick(uint8_t button) {
  HidTransport *t = active();
  if (t) { t->mouseDown(button); t->mouseUp(button); }
  record(String("mouse ") + (button == 2 ? "right" : "left") + " click");
}
void HidBackend::mouseScroll(int8_t amount) {
  if (amount == 0) return;
  HidTransport *t = active();
  if (t) t->mouseWheel(amount);
  record("mouse scroll " + String(amount));
}

// fireMacro unchanged (dispatches to tapKey/consumer/typeText).
```

Delete the old `CYPHER_KEYS_HID_LIVE` gKeyboard/gMouse/gConsumer block and the old `#warning` (that gating now lives in UsbTransport).

- [ ] **Step 3: Compile mock + dual**

Run: `CK "$HWCDC" "-DUSE_DISPLAY=1" display` → green.
Run: `CK "$OTG" "-DUSE_DISPLAY=1 -DUSE_USB_HID=1 -DUSE_BLE_HID=1" dual` → green.

- [ ] **Step 4: Serial smoke (mock build, no hardware)** — flash-free logic check is not possible without a board; defer behavioral smoke to Task 10. Confirm the two compiles are green, then commit.

- [ ] **Step 5: Commit**

```bash
git add projects/21-cypher-keys-hid-deck/src/HidBackend.h projects/21-cypher-keys-hid-deck/src/HidBackend.cpp
git commit -m "feat(21): route HidBackend through USB/BLE transports + output mode"
```

---

## Task 7: HidDeck output toggle button + status

**Files:**
- Modify: `projects/21-cypher-keys-hid-deck/src/HidDeck.h`
- Modify: `projects/21-cypher-keys-hid-deck/src/HidDeck.cpp`

Add an `OUT` button to the status bar (left of DICTATE), a hit test, `commandOutput`/`commandBle`, and show the active output + BLE dot in `drawStatusBar` and `printStatus`.

- [ ] **Step 1: Header** — add public methods and a hit-test:

```cpp
  void commandOutput(const String &arg);  // "usb" | "ble" | "toggle"
  void commandBle(const String &arg);     // "status" | "clear"
```
and in the display-guarded private section:
```cpp
  bool hitOutputButton(int16_t x, int16_t y) const;
```

- [ ] **Step 2: Button geometry + hit test** — the status bar right cluster becomes `[OUT][DICTATE][THEME][MODE]`. Shift the existing three left and add OUT. In `HidDeck.cpp` replace the four hit-test functions' x-ranges and the button draws so all four fit in `x 548..1002` (each ~ (1002-548-3*6)/4 ≈ 109 wide). Use:
- OUT: `x 548..654`
- DICTATE: `x 660..766`
- THEME: `x 772..878`
- MODE: `x 884..1002`

Add:
```cpp
bool HidDeck::hitOutputButton(int16_t x, int16_t y) const {
  return x >= 548 && x < 654 && y >= 4 && y < 36;
}
```
and update `hitDictButton`/`hitThemeButton`/`hitModeButton` ranges to the new x-bands above.

- [ ] **Step 3: Route the tap** — in `tick()`, before the dictation check, add:
```cpp
  if (touch_.releasedEdge() && hitOutputButton(rx, ry)) {
    commandOutput("toggle");
  } else if (touch_.releasedEdge() && hitDictButton(rx, ry)) {
    // ... existing chain continues ...
```

- [ ] **Step 4: Commands**
```cpp
void HidDeck::commandOutput(const String &arg) {
  String a = arg; a.trim(); a.toLowerCase();
  if (a == "ble") backend_.setOutput(kOutputBle);
  else if (a == "usb") backend_.setOutput(kOutputUsb);
  else backend_.setOutput(backend_.output() == kOutputUsb ? kOutputBle : kOutputUsb);
  dirtyAll_ = true;
  Serial.println(String("output: ") + backend_.modeLabel());
}
void HidDeck::commandBle(const String &arg) {
  String a = arg; a.trim(); a.toLowerCase();
  if (a == "clear") { backend_.bleClearBonds(); Serial.println("ble bonds cleared"); return; }
  Serial.print("ble advertising="); Serial.print(backend_.bleAdvertising() ? 1 : 0);
  Serial.print(" connected="); Serial.println(backend_.bleReady() ? 1 : 0);
}
```

- [ ] **Step 5: Draw the OUT button + BLE dot** — in `drawStatusBar`, add before the DICTATE panel:
```cpp
  bool ble = (backend_.output() == kOutputBle);
  Widgets::panel(g, 548, 4, 106, 32, 8, t.surfaceHi, 1, ble ? t.good : t.accent);
  Widgets::text(g, 601, 12, ble ? "BLE" : "USB", Widgets::fontS(), t.ink, Widgets::kCenter);
  if (ble) {  // connection dot
    uint16_t dot = backend_.bleReady() ? t.good : t.warn;
    g->fillCircle(645, 12, 4, dot);
  }
```
and move the DICTATE/THEME/MODE panel+text x-origins to the new bands from Step 2.

- [ ] **Step 6: printStatus** — add after the theme block:
```cpp
  out.print("output: "); out.println(backend_.modeLabel());
  out.print("ble: advertising="); out.print(backend_.bleAdvertising() ? 1 : 0);
  out.print(" connected="); out.println(backend_.bleReady() ? 1 : 0);
```

- [ ] **Step 7: Compile mock + dual** — both green (commands `CK "$HWCDC" ...` and `CK "$OTG" ...`).

- [ ] **Step 8: Commit**
```bash
git add projects/21-cypher-keys-hid-deck/src/HidDeck.h projects/21-cypher-keys-hid-deck/src/HidDeck.cpp
git commit -m "feat(21): status-bar USB/BLE output toggle + ble status"
```

---

## Task 8: Wire serial commands in the sketch

**Files:**
- Modify: `projects/21-cypher-keys-hid-deck/21-cypher-keys-hid-deck.ino`

- [ ] **Step 1: Add handlers + registrations**
```cpp
void cmdOutput(const String &args) { deck.commandOutput(args); }
void cmdBle(const String &args) { deck.commandBle(args); }
```
and in `setup()` after the `theme` registration:
```cpp
  router.on("out", "output: out usb|ble|toggle", cmdOutput);
  router.on("ble", "bluetooth: ble status|clear", cmdBle);
```

- [ ] **Step 2: Compile mock + dual** — both green.

- [ ] **Step 3: Commit**
```bash
git add "projects/21-cypher-keys-hid-deck/21-cypher-keys-hid-deck.ino"
git commit -m "feat(21): register out + ble serial commands"
```

---

## Task 9: Flag matrix + docs

**Files:**
- Modify: `scripts/check-flag-matrix.sh`
- Modify: `projects/21-cypher-keys-hid-deck/README.md`, `TECHNICAL.md`
- Modify: `docs/full-port-proof-matrix.md`, `docs/hardware-risk-register.md`
- Modify: `README.md` (root flags table)

- [ ] **Step 1: Flag matrix row** — after the P21 `usb-hid-mock` row add:
```bash
  "$P21|ble-hid-mock|-DUSE_DISPLAY=1 -DUSE_BLE_HID=1|GFX Library for Arduino,SensorLib,BLE"
```
(The `BLE` lib ships with the core; if `have_lib "BLE"` is flaky, drop it from the libs list — it needs no install.)

- [ ] **Step 2: TECHNICAL.md** — add a "Bluetooth (dual mode)" section: the `USE_BLE_HID` flag, the dual-mode build/upload command (`USBMode=default` + `USE_USB_HID=1 -DUSE_BLE_HID=1`), the `out`/`ble` commands, pairing (Just Works, no passkey), and the `ble clear` re-pair path. Note the proven fact: C6 esp_hosted 2.12.3 has BT; `WiFi.setPins` precedes `BLEDevice::init`.

- [ ] **Step 3: README.md (project)** — add BLE to "What you get" and a short "Wireless (Bluetooth)" note; update Status to "USB host-proven; BLE hardware-proven for the spike, dual-mode integration pending on-device acceptance."

- [ ] **Step 4: proof matrix + risk register + root README** — add BLE/`USE_BLE_HID` to the Project 21 rows and the root flags table (needs `USBMode=default`; C6 BT confirmed on the tested board).

- [ ] **Step 5: Compile the flag-matrix P21 rows only** (sanity):
```bash
CTAGS_WORKAROUND=1 ./scripts/check-flag-matrix.sh 2>&1 | grep -E "21-cypher-keys|FAIL|PASS  .*21" | tail
```
Expected: P21 baseline/display/usb-hid-mock/ble-hid-mock all PASS.

- [ ] **Step 6: Commit**
```bash
git add scripts/check-flag-matrix.sh projects/21-cypher-keys-hid-deck/README.md projects/21-cypher-keys-hid-deck/TECHNICAL.md docs/full-port-proof-matrix.md docs/hardware-risk-register.md README.md
git commit -m "docs(21): document dual-mode BLE + flag-matrix row"
```

---

## Task 10: Hardware acceptance (dual-mode)

**Files:** none (verification).

- [ ] **Step 1: Build + flash dual-mode**
```bash
ROOT=/Users/cypher/Documents/GitHub/crow-panel
P=$(ls /dev/cu.usbmodem* 2>/dev/null | head -1)   # if empty: reseat / BOOT+RESET
CTAGS_WORKAROUND=1 \
FQBN="esp32:esp32:esp32p4:USBMode=default,PSRAM=enabled,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,UploadSpeed=921600" \
EXTRA_FLAGS="-DUSE_DISPLAY=1 -DUSE_USB_HID=1 -DUSE_BLE_HID=1" \
"$ROOT/scripts/upload-project.sh" projects/21-cypher-keys-hid-deck "$P"
```
Expected: `Hash of data verified` + `Hard resetting`.

- [ ] **Step 2: USB path unchanged** — with the cable in, in USB mode (default), type into TextEdit, fire a macro, use the trackpad. All land. (Regression check.)

- [ ] **Step 3: Switch to BLE** — tap the `OUT` button (or `out ble`). Pair `Cypher Keys` in macOS Bluetooth (no passkey). The status dot turns green (connected).

- [ ] **Step 4: BLE HID works** — type letters + a shifted symbol (e.g. `!`), fire a macro (combo + a text snippet), a media key (volume), and move/click the trackpad. Confirm each lands on the Mac wirelessly and the correct characters appear (validates the keycode table).

- [ ] **Step 5: Toggle back to USB** mid-session; confirm output follows the toggle with no stuck keys. Run `ble clear`, forget the device on macOS, re-pair — confirm it works again.

- [ ] **Step 6: Update proof state** — set the Project 21 proof-matrix/README lines to reflect BLE dual-mode host-proven (typed chars, macros, media, cursor observed over BLE). Commit.
```bash
git add docs/full-port-proof-matrix.md projects/21-cypher-keys-hid-deck/README.md projects/21-cypher-keys-hid-deck/TECHNICAL.md
git commit -m "docs(21): mark BLE dual-mode host-proven"
```

---

## Self-Review

**Spec coverage:** transport abstraction (Tasks 2,4,5), translate-for-BLE-only (Task 3, keyDown in Task 5) — a deliberate improvement over the spec's "translate once", keeping the proven USB path on the Arduino API; output mode + NVS + routing + per-transport deferred release incl. flush-on-toggle (Task 6); toggle UI + dot + serial `out`/`ble` (Tasks 7,8); `USE_BLE_HID` gating + builds (Tasks 1,9); Just-Works pairing + `ble clear` (Task 5); error handling (drop when not ready — `active()` sends only if the transport exists, `ready()` gates BLE notifies); verification (Tasks 9,10). All spec sections mapped.

**Placeholder scan:** none — every code step shows complete code; doc tasks name exact sections and content.

**Type consistency:** `HidTransport` primitives (`keyDown/keyUp/consumerDown/consumerUp/mouseMove/mouseDown/mouseUp/mouseWheel/ready/name`) are identical across `HidTransport.h`, `UsbTransport`, `BleTransport`, and `HidBackend` call sites. `HidOutput { kOutputUsb, kOutputBle }` used consistently. `hidUsageForKey`/`hidModifierByte` signatures match between `HidKeycodes.h` and their `BleTransport` callers.

**Note / deviation from spec:** the spec proposed "translate once, both transports consume raw reports." This plan keeps USB on its proven Arduino `press()` path and translates only for BLE — lower risk to the already-host-proven USB behavior, same user-visible result. Flagged here intentionally.
