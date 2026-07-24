#include "HidBackend.h"

#include <CrowPanelShared.h>  // EventLog

#if CYPHER_KEYS_HID_LIVE
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
#elif USE_USB_HID
#warning "USE_USB_HID=1 but this is not a USB-OTG build (need USBMode=default / ARDUINO_USB_MODE==0). Building the MOCK backend."
#endif

namespace {
int8_t clamp8(int16_t v) {
  if (v > 127) return 127;
  if (v < -127) return -127;
  return (int8_t)v;
}
}  // namespace

String hidModPrefix(uint8_t mods) {
  String s;
  if (mods & kModCmd) s += "Cmd+";
  if (mods & kModCtrl) s += "Ctrl+";
  if (mods & kModOpt) s += "Opt+";
  if (mods & kModShift) s += "Shift+";
  return s;
}

String hidKeyName(uint8_t key) {
  switch (key) {
    case kKeyReturn: return "Return";
    case kKeyEsc: return "Esc";
    case kKeyBackspace: return "Backspace";
    case kKeyTab: return "Tab";
    case kKeyRightArrow: return "Right";
    case kKeyLeftArrow: return "Left";
    case kKeyDownArrow: return "Down";
    case kKeyUpArrow: return "Up";
    case ' ': return "Space";
    default: break;
  }
  if (key >= 0x20 && key < 0x7F) return String((char)key);
  return "0x" + String(key, HEX);
}

void HidBackend::begin(Print *log, EventLog *events) {
  log_ = log;
  events_ = events;
#if CYPHER_KEYS_HID_LIVE
  gKeyboard.begin();
  gMouse.begin();
  gConsumer.begin();
  USB.begin();
  if (log_) log_->println("[hid] USB-OTG HID started (keyboard + consumer + mouse)");
#else
  if (log_) log_->println("[hid] MOCK backend: reports are logged, not sent");
#endif
}

bool HidBackend::live() const {
#if CYPHER_KEYS_HID_LIVE
  return true;
#else
  return false;
#endif
}

const char *HidBackend::modeLabel() const { return live() ? "LIVE" : "MOCK"; }

void HidBackend::record(const String &action) {
  lastAction_ = action;
  ++reports_;
  if (log_) {
    log_->print(live() ? "[hid] sent: " : "[hid] mock: ");
    log_->println(action);
  }
  if (events_) events_->add(action.c_str());
}

void HidBackend::typeText(const String &text) {
  if (text.length() == 0) return;
#if CYPHER_KEYS_HID_LIVE
  for (size_t i = 0; i < text.length(); ++i) gKeyboard.write((uint8_t)text[i]);
#endif
  String preview = text;
  if (preview.length() > 40) preview = preview.substring(0, 40) + "...";
  record("type \"" + preview + "\"");
}

void HidBackend::releaseKeyNow() {
#if CYPHER_KEYS_HID_LIVE
  if (keyHeld_) {
    gKeyboard.releaseAll();
    keyHeld_ = false;
  }
#endif
}

void HidBackend::service(uint32_t nowMs) {
#if CYPHER_KEYS_HID_LIVE
  if (keyHeld_ && (int32_t)(nowMs - keyReleaseDueMs_) >= 0) {
    gKeyboard.releaseAll();
    keyHeld_ = false;
  }
  if (consumerHeld_ && (int32_t)(nowMs - consumerReleaseDueMs_) >= 0) {
    gConsumer.release();
    consumerHeld_ = false;
  }
#else
  (void)nowMs;
#endif
}

void HidBackend::tapKey(uint8_t mods, uint8_t key) {
#if CYPHER_KEYS_HID_LIVE
  // Release a still-held previous key first so fast typing never ghosts, then
  // press and schedule a non-blocking release.
  releaseKeyNow();
  pressMods(mods);
  if (key) gKeyboard.press(key);
  keyHeld_ = true;
  keyReleaseDueMs_ = millis() + kHoldMs;
#endif
  record("key " + hidModPrefix(mods) + hidKeyName(key));
}

void HidBackend::consumer(uint16_t usage) {
#if CYPHER_KEYS_HID_LIVE
  if (consumerHeld_) gConsumer.release();
  gConsumer.press(usage);
  consumerHeld_ = true;
  consumerReleaseDueMs_ = millis() + kHoldMs;
#endif
  record("media 0x" + String(usage, HEX));
}

void HidBackend::mouseMove(int16_t dx, int16_t dy) {
  if (dx == 0 && dy == 0) return;
#if CYPHER_KEYS_HID_LIVE
  gMouse.move(clamp8(dx), clamp8(dy));
#endif
  // High-frequency: no heap churn, no event-log spam, and NOT counted in
  // reports_ so the status bar does not repaint on every move.
  ++moves_;
}

void HidBackend::mouseButton(uint8_t button, bool pressed) {
#if CYPHER_KEYS_HID_LIVE
  if (pressed) {
    gMouse.press(button);
  } else {
    gMouse.release(button);
  }
#endif
  record(String("mouse ") + (button == 2 ? "right " : "left ") +
         (pressed ? "down" : "up"));
}

void HidBackend::mouseClick(uint8_t button) {
#if CYPHER_KEYS_HID_LIVE
  gMouse.click(button);
#endif
  record(String("mouse ") + (button == 2 ? "right" : "left") + " click");
}

void HidBackend::mouseScroll(int8_t amount) {
  if (amount == 0) return;
#if CYPHER_KEYS_HID_LIVE
  gMouse.move(0, 0, amount);
#endif
  record("mouse scroll " + String(amount));
}

void HidBackend::fireMacro(const MacroSlot &slot) {
  switch (slot.kind) {
    case kMacroCombo:
      tapKey(slot.mods, slot.key);
      break;
    case kMacroConsumer:
      consumer(slot.usage);
      break;
    case kMacroText:
      if (slot.text) typeText(String(slot.text));
      break;
    case kMacroNone:
    default:
      break;
  }
}
