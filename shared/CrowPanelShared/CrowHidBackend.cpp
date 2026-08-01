#include "CrowHidBackend.h"

#include <Preferences.h>
#include <CrowPanelShared.h>  // EventLog

#if CROW_GAMEPAD_USB_LIVE
#include <USB.h>
#endif

namespace {
int8_t clamp8(int16_t v) {
  if (v > 127) return 127;
  if (v < -127) return -127;
  return (int8_t)v;
}

#if CROW_GAMEPAD_USB_LIVE
// USB.onEvent()'s callback is a plain esp_event_handler_t free-function
// pointer, not a member pointer, and ESPUSB::onEvent() always registers the
// USB singleton itself as the handler arg -- there is no way to thread a
// HidBackend* through that registration call. This file-static pointer is
// the accepted workaround: set once in HidBackend::begin(), read once here.
// It is only ever non-null on a native-USB-gamepad build, where exactly one
// HidBackend is expected to own the gamepad.
HidBackend *gGamepadResyncTarget = nullptr;

// Fires from tud_mount_cb() (see cores/esp32/USB.cpp) whenever the USB
// device re-enumerates with the host -- including a cable jiggle or a host
// resume-from-sleep, not just first boot. Clears the cached gamepad state so
// the next gamepadState() call resends unconditionally, even if the panel's
// own idea of the state did not change, because the host's idea of it did.
void onGamepadUsbMounted(void *, esp_event_base_t, int32_t, void *) {
  if (gGamepadResyncTarget) gGamepadResyncTarget->resyncGamepad();
}
#endif
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

void HidBackend::begin(Print *log, EventLog *events, const char *bleName,
                       const char *nvsNamespace) {
  log_ = log;
  events_ = events;
  if (nvsNamespace != nullptr && nvsNamespace[0] != '\0') {
    nvsNamespace_ = nvsNamespace;
  }
  ble_.setDeviceName(bleName);
  usb_.begin();
  ble_.begin();
#if CROW_GAMEPAD_USB_LIVE
  // Register before gamepad_.begin() starts the USB stack below, not after:
  // gamepad_.begin() calls USB.begin(), which can fire the mount event from
  // the TinyUSB task as soon as the host enumerates. Registering first closes
  // the (largely theoretical, since enumeration is slow) window where that
  // first event could fire before a handler exists to catch it.
  gGamepadResyncTarget = this;
  USB.onEvent(ARDUINO_USB_STARTED_EVENT, onGamepadUsbMounted);
#endif
  // Eager, not lazy: gamepad_.begin() -> USBHID::begin() -> USB.begin() does
  // heap allocation (semaphores/mutex) and spins up the TinyUSB device task.
  // That is fine here on the setup-time caller, and must never happen on
  // gamepadState()'s hot path (a high-priority FreeRTOS task where heap
  // allocation is forbidden) -- gamepadState() only keeps a belt-and-braces
  // guard in case begin() was skipped. In mock mode this is a no-op.
  gamepad_.begin();
  gamepadBegun_ = true;
  loadOutput();
  if (log_) {
    log_->print("[hid] output=");
    log_->print(modeLabel());
    log_->println(usb_.ready() || ble_.ready() ? " (live)"
                                               : " (mock: logging only)");
  }
}

void HidBackend::loadOutput() {
  Preferences prefs;
  if (prefs.begin(nvsNamespace_, true)) {
    uint32_t v = prefs.getUInt("output", 0);
    prefs.end();
    if (v == kOutputBle) output_ = kOutputBle;
  }
}

void HidBackend::persistOutput() const {
  Preferences prefs;
  if (prefs.begin(nvsNamespace_, false)) {
    prefs.putUInt("output", (uint32_t)output_);
    prefs.end();
  }
}

HidTransport *HidBackend::active() {
  HidTransport *t =
      (output_ == kOutputBle) ? (HidTransport *)&ble_ : (HidTransport *)&usb_;
  return t;
}

void HidBackend::setOutput(HidOutput out) {
  if (out == output_) return;
  // Flush any pending release on the transport that owns it before switching.
  if (keyHeld_ && keyHeldOn_) {
    keyHeldOn_->keyUp();
    keyHeld_ = false;
    keyHeldOn_ = nullptr;
  }
  if (consumerHeld_ && consumerHeldOn_) {
    consumerHeldOn_->consumerUp();
    consumerHeld_ = false;
    consumerHeldOn_ = nullptr;
  }
  output_ = out;
  persistOutput();
  record(String("output -> ") + modeLabel());
}

bool HidBackend::usbLive() const { return usb_.ready(); }
bool HidBackend::bleReady() const { return ble_.ready(); }
bool HidBackend::bleAdvertising() const { return ble_.advertising(); }
void HidBackend::bleClearBonds() {
  ble_.clearBonds();
  record("ble bonds cleared");
}

bool HidBackend::live() const {
  return (output_ == kOutputBle) ? ble_.ready() : usb_.ready();
}

const char *HidBackend::modeLabel() const {
  if (output_ == kOutputBle) return ble_.ready() ? "BLE" : "BLE?";
  return usb_.ready() ? "USB" : "MOCK";
}

void HidBackend::record(const String &action) {
  lastAction_ = action;
  ++reports_;
  if (log_) {
    log_->print(live() ? "[hid] sent: " : "[hid] mock: ");
    log_->println(action);
  }
  if (events_) events_->add(action.c_str());
}

void HidBackend::service(uint32_t nowMs) {
  if (keyHeld_ && (int32_t)(nowMs - keyReleaseDueMs_) >= 0) {
    if (keyHeldOn_) keyHeldOn_->keyUp();
    keyHeld_ = false;
    keyHeldOn_ = nullptr;
  }
  if (consumerHeld_ && (int32_t)(nowMs - consumerReleaseDueMs_) >= 0) {
    if (consumerHeldOn_) consumerHeldOn_->consumerUp();
    consumerHeld_ = false;
    consumerHeldOn_ = nullptr;
  }
  // Flush accumulated mouse movement/scroll at a capped rate (rate-limits BLE
  // notifies so a fast trackpad drag can't flood the stack).
  if ((pendingDx_ || pendingDy_ || pendingWheel_) &&
      (int32_t)(nowMs - lastMouseSendMs_) >= (int32_t)kMouseIntervalMs) {
    HidTransport *t = active();
    if (t) {
      if (pendingDx_ || pendingDy_) t->mouseMove(clamp8(pendingDx_), clamp8(pendingDy_));
      if (pendingWheel_) t->mouseWheel(clamp8(pendingWheel_));
    }
    pendingDx_ = 0;
    pendingDy_ = 0;
    pendingWheel_ = 0;
    lastMouseSendMs_ = nowMs;
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

void HidBackend::gamepadState(uint8_t hat, uint32_t buttons) {
  // Belt-and-braces only: begin() already calls gamepad_.begin() eagerly at
  // setup time, specifically so this never has to run on this call's own
  // task (the stick task is high-priority FreeRTOS, where the heap
  // allocation inside a live begin() is forbidden). This guard exists only
  // to avoid a dropped state if a caller ever invokes gamepadState() without
  // going through HidBackend::begin() first -- it must never be the path
  // that actually fires in production.
  if (!gamepadBegun_) {
    gamepad_.begin();
    gamepadBegun_ = true;
  }
  // Change detection: the stick task calls this every poll, and an unchanged
  // state must not cost a USB report. resyncGamepad() (wired to the USB
  // mount event) clears gamepadStateValid_ so a held input still gets
  // resent after the host re-enumerates and forgets it.
  if (gamepadStateValid_ && hat == lastHat_ && buttons == lastButtons_) return;
  lastHat_ = hat;
  lastButtons_ = buttons;
  gamepadStateValid_ = true;
  gamepad_.sendState(hat, buttons);
  reports_++;
}

bool HidBackend::gamepadLive() const { return gamepad_.ready(); }

uint32_t HidBackend::gamepadReports() const { return gamepad_.reports(); }

void HidBackend::resyncGamepad() { gamepadStateValid_ = false; }

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
  // Accumulate; service() flushes at kMouseIntervalMs. Do not send per-call:
  // over BLE that floods NimBLE and reboots the panel.
  pendingDx_ += dx;
  pendingDy_ += dy;
  ++moves_;
}

void HidBackend::mouseButton(uint8_t button, bool pressed) {
  HidTransport *t = active();
  if (t) {
    if (pressed)
      t->mouseDown(button);
    else
      t->mouseUp(button);
  }
  record(String("mouse ") + (button == 2 ? "right " : "left ") +
         (pressed ? "down" : "up"));
}

void HidBackend::mouseClick(uint8_t button) {
  HidTransport *t = active();
  if (t) {
    t->mouseDown(button);
    t->mouseUp(button);
  }
  record(String("mouse ") + (button == 2 ? "right" : "left") + " click");
}

void HidBackend::mouseScroll(int8_t amount) {
  if (amount == 0) return;
  // Accumulate; flushed with movement in service() (same anti-flood reason).
  pendingWheel_ += amount;
}

void HidBackend::launchApp(const char *appName) {
  if (appName == nullptr || appName[0] == '\0') return;
  HidTransport *t = active();
  if (t) {
    // Flush any pending key release so no modifier is stuck during the sequence.
    if (keyHeld_ && keyHeldOn_) {
      keyHeldOn_->keyUp();
      keyHeld_ = false;
      keyHeldOn_ = nullptr;
    }
    // Spotlight: Cmd+Space, wait for it to open, type the name, wait for it to
    // resolve the match, then Return. Discrete/infrequent, so blocking delays
    // are fine (and give Spotlight time to appear over USB or BLE).
    t->keyDown(kModCmd, ' ');
    delay(30);
    t->keyUp();
    delay(300);
    for (const char *p = appName; *p; ++p) {
      t->keyDown(0, (uint8_t)*p);
      t->keyUp();
      delay(12);
    }
    delay(500);  // let Spotlight resolve the top match before Enter opens it
    t->keyDown(0, kKeyReturn);
    delay(30);
    t->keyUp();
  }
  record(String("open app \"") + appName + "\"");
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
    case kMacroApp:
      launchApp(slot.text);
      break;
    case kMacroNone:
    default:
      break;
  }
}
