#ifndef CYPHERDRIVE_HID_PAD_H
#define CYPHERDRIVE_HID_PAD_H

#include "../config/ProjectConfig.h"
#include "ScanLog.h"
#include <Arduino.h>
#include <CrowHid.h>

class EventLog;

// The HID surface of the field tool: an operator-driven macro/keystroke pad the
// panel sends to a host it is plugged (USB) or paired (BLE) to. Built on the
// shared CrowHid backend (project 21's transport stack, extracted). Mock-first:
// with no live transport every action is logged to Serial / EventLog / ScanLog
// so the pad is fully exercisable with no host attached.
//
// This is a keyboard, not an implant: the operator taps a tile and it types.
// No autorun payloads, no unattended exfil.
class HidPad {
 public:
  static const uint8_t kSlots = 8;  // 2 rows x 4 tiles

  void begin(EventLog *events, ScanLog *log);
  void service(uint32_t nowMs);

  // Actions (each mirrors a serial command and a touch tile).
  void fireSlot(uint8_t index);
  void typeText(const String &text);
  void tapKey(uint8_t mods, uint8_t key);
  void media(uint16_t usage);
  void toggleOutput();
  void setOutput(HidOutput out);
  void clearBonds();

  // State for the UI / status.
  const MacroSlot &slot(uint8_t index) const;
  const char *modeLabel() const { return backend_.modeLabel(); }
  bool live() const { return backend_.live(); }
  HidOutput output() const { return backend_.output(); }
  bool bleAdvertising() const { return backend_.bleAdvertising(); }
  uint32_t reportsSent() const { return backend_.reportsSent(); }
  const String &lastAction() const { return backend_.lastAction(); }

 private:
  void logAction(const String &summary);
  HidBackend backend_;
  ScanLog *log_ = nullptr;
};

#endif
