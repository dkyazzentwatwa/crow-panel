#ifndef CYPHER_KEYS_HID_BACKEND_H
#define CYPHER_KEYS_HID_BACKEND_H

#include "../config/ProjectConfig.h"
#include "HidTypes.h"
#include "UsbTransport.h"
#include "BleTransport.h"
#include <Arduino.h>

class Print;
class EventLog;

// Which transport receives HID actions.
enum HidOutput : uint8_t { kOutputUsb = 0, kOutputBle = 1 };

// One API for the three HID surfaces (keyboard, consumer control, mouse).
// Actions are routed to the active transport (USB or BLE). When the active
// transport cannot deliver a report (MOCK / not connected) the action is still
// logged to Serial and the event log so the whole deck is smoke-testable with
// no host attached.
class HidBackend {
 public:
  void begin(Print *log, EventLog *events);

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
  HidTransport *active();          // current transport, or nullptr
  void loadOutput();
  void persistOutput() const;
  void launchApp(const char *appName);  // Spotlight: Cmd+Space, type, Return

  // A press is held at least this long before release. >= 2 USB HID poll
  // intervals so the host reliably sees the key down then up.
  static const uint32_t kHoldMs = 24;

  // Mouse move/scroll reports are coalesced and emitted at most this often. The
  // BLE transport additionally drops reports when its buffer pool is low (flow
  // control), so this can stay smooth (~66/s) without risking a crash: excess
  // reports are dropped gracefully rather than exhausting the stack. USB sends all.
  static const uint32_t kMouseIntervalMs = 15;

  Print *log_ = nullptr;
  EventLog *events_ = nullptr;
  uint32_t reports_ = 0;
  uint32_t moves_ = 0;  // mouse-move count (kept out of reports_ so the status
                        // bar does not repaint on every trackpad move)
  int16_t pendingDx_ = 0;      // accumulated mouse movement awaiting a send
  int16_t pendingDy_ = 0;
  int16_t pendingWheel_ = 0;   // accumulated scroll awaiting a send
  uint32_t lastMouseSendMs_ = 0;
  String lastAction_ = "(none)";
  HidOutput output_ = kOutputUsb;

  UsbTransport usb_;
  BleTransport ble_;

  bool keyHeld_ = false;
  uint32_t keyReleaseDueMs_ = 0;
  HidTransport *keyHeldOn_ = nullptr;  // transport that owns the pending release
  bool consumerHeld_ = false;
  uint32_t consumerReleaseDueMs_ = 0;
  HidTransport *consumerHeldOn_ = nullptr;
};

// Human-readable modifier prefix, e.g. "Cmd+Shift+". Shared by the backend and
// the UI so mock logs and on-screen labels agree.
String hidModPrefix(uint8_t mods);
// Printable name for a main key byte (letter, "Space", "Tab", "Up", ...).
String hidKeyName(uint8_t key);

#endif
