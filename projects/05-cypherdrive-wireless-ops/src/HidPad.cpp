#include "HidPad.h"
#include <CrowPanelShared.h>  // EventLog

namespace {
// A compact, field-useful macro pad. macOS-oriented (Cmd), like project 21.
const MacroSlot kMacroSlots[HidPad::kSlots] = {
    MACRO_COMBO("Lock", kModCmd | kModCtrl, 'q'),   // lock screen
    MACRO_COMBO("Screenshot", kModCmd | kModShift, '4'),
    MACRO_APP("Terminal", "Terminal"),
    MACRO_COMBO("Spotlight", kModCmd, ' '),
    MACRO_MEDIA("Play/Pause", kCcPlayPause),
    MACRO_MEDIA("Vol +", kCcVolumeUp),
    MACRO_MEDIA("Vol -", kCcVolumeDown),
    MACRO_MEDIA("Mute", kCcMute),
};
}  // namespace

void HidPad::begin(EventLog *events, ScanLog *log) {
  log_ = log;
  backend_.begin(&Serial, events, "CypherDrive HID", "cypherdrive-hid");
}

void HidPad::service(uint32_t nowMs) { backend_.service(nowMs); }

void HidPad::logAction(const String &summary) {
  if (log_) log_->recordHid(summary, String("out=") + backend_.modeLabel());
}

void HidPad::fireSlot(uint8_t index) {
  if (index >= kSlots) return;
  backend_.fireMacro(kMacroSlots[index]);
  logAction(String("slot ") + (index + 1) + " " + kMacroSlots[index].label);
}

void HidPad::typeText(const String &text) {
  backend_.typeText(text);
  String preview = text.length() > 24 ? text.substring(0, 24) + "..." : text;
  logAction("type \"" + preview + "\"");
}

void HidPad::tapKey(uint8_t mods, uint8_t key) {
  backend_.tapKey(mods, key);
  logAction("key " + hidModPrefix(mods) + hidKeyName(key));
}

void HidPad::media(uint16_t usage) {
  backend_.consumer(usage);
  logAction("media 0x" + String(usage, HEX));
}

void HidPad::toggleOutput() {
  setOutput(backend_.output() == kOutputUsb ? kOutputBle : kOutputUsb);
}

void HidPad::setOutput(HidOutput out) {
  backend_.setOutput(out);
  logAction(String("output -> ") + backend_.modeLabel());
}

void HidPad::clearBonds() {
  backend_.bleClearBonds();
  logAction("ble bonds cleared");
}

const MacroSlot &HidPad::slot(uint8_t index) const {
  return kMacroSlots[index < kSlots ? index : 0];
}
