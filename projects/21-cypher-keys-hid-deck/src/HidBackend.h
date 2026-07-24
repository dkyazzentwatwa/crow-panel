#ifndef CYPHER_KEYS_HID_BACKEND_H
#define CYPHER_KEYS_HID_BACKEND_H

#include "../config/ProjectConfig.h"
#include "HidTypes.h"
#include <Arduino.h>

// Real USB HID is only possible on a USB-OTG (TinyUSB) build: ARDUINO_USB_MODE
// is 0 for USBMode=default and 1 for USBMode=hwcdc (native USB is CDC/JTAG,
// no HID). If USE_USB_HID is set under hwcdc we cannot create a HID device, so
// the backend stays in MOCK and warns at compile time.
#if USE_USB_HID && defined(ARDUINO_USB_MODE) && (ARDUINO_USB_MODE == 0)
#define CYPHER_KEYS_HID_LIVE 1
#else
#define CYPHER_KEYS_HID_LIVE 0
#endif

class Print;
class EventLog;

// One API for the three HID surfaces (keyboard, consumer control, mouse).
// LIVE sends real USB reports; MOCK prints the intended report to Serial and
// the event log so the whole deck is smoke-testable with no host attached.
class HidBackend {
 public:
  void begin(Print *log, EventLog *events);

  bool live() const;              // true when a real USB HID device is active
  const char *modeLabel() const;  // "LIVE" or "MOCK"
  uint32_t reportsSent() const { return reports_; }
  const String &lastAction() const { return lastAction_; }

  // Keyboard.
  void typeText(const String &text);
  void tapKey(uint8_t mods, uint8_t key);  // hold mods, tap key, release all

  // Consumer control (media / brightness usages).
  void consumer(uint16_t usage);

  // Relative mouse / trackpad.
  void mouseMove(int16_t dx, int16_t dy);
  void mouseButton(uint8_t button, bool pressed);  // 1=left, 2=right
  void mouseClick(uint8_t button);
  void mouseScroll(int8_t amount);

  // Fire a macro slot by kind (Combo / Consumer / Text).
  void fireMacro(const MacroSlot &slot);

  // Call every loop: performs any due key/consumer releases. Presses are held
  // for a short, non-blocking window instead of a blocking delay() so the loop
  // stays responsive AND the host is guaranteed to poll between press/release.
  void service(uint32_t nowMs);

 private:
  void record(const String &action);
  void releaseKeyNow();  // release a still-held key immediately (anti-ghost)

  // A press is held at least this long before release. >= 2 USB HID poll
  // intervals so the host reliably sees the key down then up.
  static const uint32_t kHoldMs = 24;

  Print *log_ = nullptr;
  EventLog *events_ = nullptr;
  uint32_t reports_ = 0;
  uint32_t moves_ = 0;  // mouse-move count (kept out of reports_ so the status
                        // bar does not repaint on every trackpad move)
  String lastAction_ = "(none)";

  bool keyHeld_ = false;
  uint32_t keyReleaseDueMs_ = 0;
  bool consumerHeld_ = false;
  uint32_t consumerReleaseDueMs_ = 0;
};

// Human-readable modifier prefix, e.g. "Cmd+Shift+". Shared by the backend and
// the UI so mock logs and on-screen labels agree.
String hidModPrefix(uint8_t mods);
// Printable name for a main key byte (letter, "Space", "Tab", "Up", ...).
String hidKeyName(uint8_t key);

#endif
