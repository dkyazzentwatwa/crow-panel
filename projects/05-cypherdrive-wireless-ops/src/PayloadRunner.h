#ifndef CYPHERDRIVE_PAYLOAD_RUNNER_H
#define CYPHERDRIVE_PAYLOAD_RUNNER_H

#include "../config/ProjectConfig.h"
#include "HidPad.h"
#include "ScanLog.h"
#include <Arduino.h>

// A small, non-blocking DuckyScript player that types benign automation over the
// active HID output (via HidPad). Supports the common verbs: REM, STRING,
// STRINGLN, ENTER, TAB, ESCAPE, SPACE, DELAY, arrow keys, F-keys, and modifier
// combos (GUI/WINDOWS/COMMAND, CTRL, ALT/OPTION, SHIFT + a key). Payloads come
// from compiled-in presets or from SD (see SdStore); the operator picks one and
// taps run - there is no autorun.
//
// Deliberately benign: this plays operator-chosen automation. It is not a
// framework for the credential-theft / exfiltration / persistence / network-
// attack payloads that were kept out of this tool by design.
class PayloadRunner {
 public:
  void begin(HidPad *hid, ScanLog *log);

  // Compiled-in benign presets.
  uint8_t presetCount() const;
  const char *presetName(uint8_t index) const;
  const char *presetScript(uint8_t index) const;

  // Start playing a script. name is for the UI/log; script is DuckyScript text.
  void run(const String &name, const String &script);
  void stop();

  bool running() const { return running_; }
  const String &currentName() const { return name_; }
  uint8_t progressPct() const;

  // Advance the player; call every loop().
  void service(uint32_t nowMs);

 private:
  void processLine(const String &line);

  HidPad *hid_ = nullptr;
  ScanLog *log_ = nullptr;
  String script_;
  String name_;
  uint16_t pos_ = 0;      // char index into script_
  uint16_t total_ = 0;    // script length, for progress
  bool running_ = false;
  uint32_t nextMs_ = 0;
  uint16_t defaultDelayMs_ = 0;  // DEFAULT_DELAY between commands
};

#endif
