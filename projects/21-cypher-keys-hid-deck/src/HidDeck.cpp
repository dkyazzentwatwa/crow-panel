#include "HidDeck.h"

#include <Preferences.h>
#include <esp_system.h>  // esp_reset_reason()

#include <CrowPanelShared.h>  // EventLog, CrowDisplay, Widgets, HardwareProfile

namespace {
// Short label for the last chip-reset cause (diagnostic).
const char *resetReasonLabel() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return "poweron";
    case ESP_RST_SW: return "sw";
    case ESP_RST_PANIC: return "PANIC";
    case ESP_RST_INT_WDT: return "INT-WDT";
    case ESP_RST_TASK_WDT: return "TASK-WDT";
    case ESP_RST_WDT: return "WDT";
    case ESP_RST_BROWNOUT: return "BROWNOUT";
    case ESP_RST_DEEPSLEEP: return "deepsleep";
    default: return "other";
  }
}
}  // namespace

namespace {
uint8_t modBitFromName(const String &t) {
  if (t == "cmd" || t == "command" || t == "gui" || t == "meta" || t == "win")
    return kModCmd;
  if (t == "shift") return kModShift;
  if (t == "opt" || t == "option" || t == "alt") return kModOpt;
  if (t == "ctrl" || t == "control") return kModCtrl;
  return kModNone;
}

// Named non-printable keys accepted by `combo`.
bool keyFromName(const String &t, uint8_t &key) {
  if (t == "enter" || t == "return") { key = kKeyReturn; return true; }
  if (t == "tab") { key = kKeyTab; return true; }
  if (t == "esc" || t == "escape") { key = kKeyEsc; return true; }
  if (t == "space" || t == "spc") { key = ' '; return true; }
  if (t == "backspace" || t == "bksp") { key = kKeyBackspace; return true; }
  if (t == "up") { key = kKeyUpArrow; return true; }
  if (t == "down") { key = kKeyDownArrow; return true; }
  if (t == "left") { key = kKeyLeftArrow; return true; }
  if (t == "right") { key = kKeyRightArrow; return true; }
  if (t.length() == 1) { key = (uint8_t)t[0]; return true; }
  return false;
}
}  // namespace

void HidDeck::begin(EventLog *events) {
  events_ = events;
  bootReason_ = String("rst:") + resetReasonLabel();
  Serial.print("[boot] reset reason: ");
  Serial.println(resetReasonLabel());
  backend_.begin(&Serial, events);
  presets_.begin();
  loadTheme();
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  // manualFlush: draw a whole frame, then flush once (see DisplayBringup). This
  // turns Arduino_GFX's per-pixel cache sync into one sync per frame.
  displayReady_ = CrowDisplay::begin(activeHardwareProfile(), "Cypher Keys", true);
#endif
  dirtyAll_ = true;
}

void HidDeck::loadTheme() {
  Preferences prefs;
  if (prefs.begin(CYPHER_KEYS_NVS_NAMESPACE, true)) {
    uint32_t stored = prefs.getUInt("theme", 0);
    prefs.end();
    if (stored < deckThemeCount()) themeIndex_ = (uint8_t)stored;
  }
}

void HidDeck::persistTheme() const {
  Preferences prefs;
  if (prefs.begin(CYPHER_KEYS_NVS_NAMESPACE, false)) {
    prefs.putUInt("theme", themeIndex_);
    prefs.end();
  }
}

void HidDeck::cycleTheme() {
  themeIndex_ = (themeIndex_ + 1) % deckThemeCount();
  persistTheme();
  dirtyAll_ = true;
}

void HidDeck::tick() {
  touch_.tick();
  uint32_t now = millis();
  backend_.service(now);  // perform any due (non-blocking) HID releases
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  if (!displayReady_) return;
  uint32_t before = backend_.reportsSent();
  Arduino_GFX *g = CrowDisplay::canvas();

  // Instant press-down feedback: light the touched key immediately and flush
  // only its region (sub-ms), well before the character commits on release.
  if (g && mode_ == kModeDeck && touch_.pressedEdge()) {
    int16_t kx, ky, kw, kh;
    if (keyboard_.keyRectAt(touch_.x(), touch_.y(), kx, ky, kw, kh)) {
      keyboard_.drawSingleKey(g, theme(), kx, ky, kw, kh, /*pressed=*/true);
      CrowDisplay::flush(kx - 3, ky - 3, kw + 10, kh + 10);
      keyPressed_ = true;
      pressKx_ = kx;
      pressKy_ = ky;
      pressKw_ = kw;
      pressKh_ = kh;
    }
  }

  int16_t rx = touch_.releaseX();
  int16_t ry = touch_.releaseY();
  if (touch_.releasedEdge() && hitOutputButton(rx, ry)) {
    commandOutput("toggle");
  } else if (touch_.releasedEdge() && hitDictButton(rx, ry)) {
    commandDictate();
    dirtyStatus_ = true;
  } else if (touch_.releasedEdge() && hitThemeButton(rx, ry)) {
    cycleTheme();
  } else if (touch_.releasedEdge() && hitModeButton(rx, ry)) {
    toggleMode();
  } else if (mode_ == kModeDeck) {
    if (touch_.releasedEdge()) handleDeckRelease(rx, ry);
  } else {
    trackpad_.update(touch_, backend_);
  }

  // Clear the press highlight on release. If a full keyboard redraw is already
  // pending (mods/shift/layer changed), it repaints the key; otherwise restore
  // just that one key - the fast path for ordinary typing.
  if (keyPressed_ && touch_.releasedEdge()) {
    if (g && !dirtyKeyboard_ && !dirtyAll_ && mode_ == kModeDeck) {
      keyboard_.drawSingleKey(g, theme(), pressKx_, pressKy_, pressKw_, pressKh_,
                              /*pressed=*/false);
      CrowDisplay::flush(pressKx_ - 3, pressKy_ - 3, pressKw_ + 10, pressKh_ + 10);
    }
    keyPressed_ = false;
  }

  if (backend_.reportsSent() != before) dirtyStatus_ = true;  // discrete action
  render(now);
#endif
}

bool HidDeck::parseCombo(const String &spec, uint8_t &mods, uint8_t &key) const {
  mods = 0;
  key = 0;
  bool haveKey = false;
  String work = spec;
  work.trim();
  work.toLowerCase();
  int start = 0;
  while (start <= work.length()) {
    int plus = work.indexOf('+', start);
    String token = (plus < 0) ? work.substring(start) : work.substring(start, plus);
    token.trim();
    if (token.length() > 0) {
      uint8_t bit = modBitFromName(token);
      if (bit != kModNone) {
        mods |= bit;
      } else {
        uint8_t k;
        if (keyFromName(token, k)) {
          key = k;
          haveKey = true;
        }
      }
    }
    if (plus < 0) break;
    start = plus + 1;
  }
  return haveKey;
}

// ---- Serial command surface -------------------------------------------------

void HidDeck::commandKey(const String &text) { backend_.typeText(text); }

void HidDeck::commandCombo(const String &spec) {
  uint8_t mods, key;
  if (parseCombo(spec, mods, key)) {
    backend_.tapKey(mods, key);
  } else {
    Serial.println("usage: combo <mods+key>, e.g. cmd+c, cmd+shift+4, ctrl+up");
  }
}

void HidDeck::commandTap(const String &slot) {
  int index = slot.toInt();
  if (index < 0 || index >= presets_.slotCount()) {
    Serial.println("usage: tap <0.." + String(presets_.slotCount() - 1) + ">");
    return;
  }
  const MacroSlot &s = presets_.slot((uint8_t)index);
  if (s.kind == kMacroNone) {
    Serial.println("slot " + String(index) + " is empty");
    return;
  }
  backend_.fireMacro(s);
}

void HidDeck::commandPreset(const String &arg) {
  String a = arg;
  a.trim();
  if (a.length() == 0 || a == "next") {
    presets_.next();
  } else if (!presets_.selectByName(a)) {
    Serial.println("no preset matching \"" + a + "\"");
    return;
  }
  dirtyAll_ = true;
  Serial.println("preset: " + String(presets_.activeName()));
}

void HidDeck::commandMode(const String &arg) {
  String a = arg;
  a.trim();
  a.toLowerCase();
  if (a.startsWith("track")) {
    setMode(kModeTrackpad);
  } else if (a.startsWith("deck") || a.startsWith("key")) {
    setMode(kModeDeck);
  } else {
    Serial.println("usage: mode deck|trackpad");
    return;
  }
  Serial.println(String("mode: ") + (mode_ == kModeTrackpad ? "trackpad" : "deck"));
}

void HidDeck::commandMouse(const String &args) {
  String a = args;
  a.trim();
  int sp = a.indexOf(' ');
  if (sp < 0) {
    Serial.println("usage: mouse <dx> <dy>");
    return;
  }
  int16_t dx = a.substring(0, sp).toInt();
  int16_t dy = a.substring(sp + 1).toInt();
  backend_.mouseMove(dx, dy);
}

void HidDeck::commandClick(const String &arg) {
  String a = arg;
  a.trim();
  a.toLowerCase();
  backend_.mouseClick(a.startsWith("r") ? 2 : 1);
}

void HidDeck::commandScroll(const String &arg) {
  backend_.mouseScroll((int8_t)arg.toInt());
}

void HidDeck::commandMedia(const String &arg) {
  String a = arg;
  a.trim();
  a.toLowerCase();
  if (a == "volup" || a == "vol+") backend_.consumer(kCcVolumeUp);
  else if (a == "voldn" || a == "vol-") backend_.consumer(kCcVolumeDown);
  else if (a == "mute") backend_.consumer(kCcMute);
  else if (a == "play") backend_.consumer(kCcPlayPause);
  else if (a == "brightup") backend_.consumer(kCcBrightnessUp);
  else if (a == "brightdn") backend_.consumer(kCcBrightnessDown);
  else Serial.println("usage: media volup|voldn|mute|play|brightup|brightdn");
}

void HidDeck::commandDictate() {
  // F5 is the Dictation/mic key on the Mac's function row.
  backend_.tapKey(kModNone, kKeyF5);
}

void HidDeck::commandTheme(const String &arg) {
  String a = arg;
  a.trim();
  if (a.length() == 0 || a == "next") {
    cycleTheme();
  } else {
    int idx = deckThemeIndexFromName(a);
    if (idx < 0) {
      Serial.println("no theme matching \"" + a + "\"");
      return;
    }
    themeIndex_ = (uint8_t)idx;
    persistTheme();
    dirtyAll_ = true;
  }
  Serial.println("theme: " + String(theme().name));
}

void HidDeck::commandOutput(const String &arg) {
  String a = arg;
  a.trim();
  a.toLowerCase();
  if (a == "ble") backend_.setOutput(kOutputBle);
  else if (a == "usb") backend_.setOutput(kOutputUsb);
  else backend_.setOutput(backend_.output() == kOutputUsb ? kOutputBle : kOutputUsb);
  dirtyAll_ = true;
  Serial.println(String("output: ") + backend_.modeLabel());
}

void HidDeck::commandBle(const String &arg) {
  String a = arg;
  a.trim();
  a.toLowerCase();
  if (a == "clear") {
    backend_.bleClearBonds();
    Serial.println("ble bonds cleared");
    return;
  }
  Serial.print("ble advertising=");
  Serial.print(backend_.bleAdvertising() ? 1 : 0);
  Serial.print(" connected=");
  Serial.println(backend_.bleReady() ? 1 : 0);
}

void HidDeck::printStatus(Print &out) {
  out.println("== Cypher Keys HID Deck ==");
  out.print("mode: ");
  out.println(mode_ == kModeTrackpad ? "trackpad" : "deck");
  out.print("hid backend: ");
  out.print(backend_.modeLabel());
  out.print("  (USE_USB_HID=");
  out.print((int)USE_USB_HID);
  out.print(", USE_BLE_HID=");
  out.print((int)USE_BLE_HID);
  out.println(backend_.live() ? ", live)" : ")");
  out.print("preset: ");
  out.print(presets_.activeName());
  out.print("  [");
  out.print(presets_.activeIndex() + 1);
  out.print('/');
  out.print(presets_.presetCount());
  out.println("]");
  out.print("theme: ");
  out.print(theme().name);
  out.print("  [");
  out.print(themeIndex_ + 1);
  out.print('/');
  out.print(deckThemeCount());
  out.println("]");
  out.print("output: ");
  out.println(backend_.modeLabel());
  out.print("ble: advertising=");
  out.print(backend_.bleAdvertising() ? 1 : 0);
  out.print(" connected=");
  out.println(backend_.bleReady() ? 1 : 0);
  out.print("sticky mods: ");
  out.print(keyboard_.stickyMods() ? hidModPrefix(keyboard_.stickyMods()) : "(none) ");
  out.print(" shift=");
  out.println(keyboard_.shifted() ? "on" : "off");
  out.print("reports sent: ");
  out.println(backend_.reportsSent());
  out.print("last action: ");
  out.println(backend_.lastAction());
}

void HidDeck::printHid(Print &out) {
  out.print("backend: ");
  out.println(backend_.modeLabel());
  out.println("interfaces: keyboard + consumer-control + mouse");
  out.print("output: ");
  out.print(backend_.output() == kOutputBle ? "BLE" : "USB");
  out.print("  usb_live=");
  out.print(backend_.usbLive() ? 1 : 0);
  out.print(" ble_connected=");
  out.println(backend_.bleReady() ? 1 : 0);
  if (backend_.live()) {
    out.print(backend_.modeLabel());
    out.println(" active; reports are sent to the host.");
  } else {
    out.println("MOCK: reports are logged, not sent.");
    out.println("Real device: USBMode=default with -DUSE_USB_HID=1 (USB) and/or");
    out.println("-DUSE_BLE_HID=1 (Bluetooth); toggle with the OUT button or 'out'.");
  }
}

void HidDeck::printTouchDiagnostics(Print &out) const {
  out.print("[touch] release_count=");
  out.print(touch_.count());
  out.print(" down=");
  out.print(touch_.down() ? 1 : 0);
  out.print(" x=");
  out.print(touch_.x());
  out.print(" y=");
  out.println(touch_.y());
}

void HidDeck::handleDeckRelease(int16_t x, int16_t y) {
  int8_t tab = presets_.hitTab(x, y);
  if (tab >= 0) {
    presets_.setActive((uint8_t)tab);
    dirtyMacro_ = true;
    dirtyStatus_ = true;
    return;
  }
  int8_t slot = presets_.hitSlot(x, y);
  if (slot >= 0) {
    const MacroSlot &s = presets_.slot((uint8_t)slot);
    if (s.kind != kMacroNone) backend_.fireMacro(s);
    return;
  }
  // Snapshot visible keyboard state so we only force a full redraw when key
  // highlights actually change.
  bool preShift = keyboard_.shifted();
  uint8_t preMods = keyboard_.stickyMods();
  HidKeyEvent ev = keyboard_.hitTest(x, y);
  if (ev.send) {
    backend_.tapKey(ev.mods, ev.key);
    // A one-shot shift/modifier was consumed -> those keys' highlights changed,
    // so redraw the whole keyboard. Plain typing leaves it to the single-key
    // restore in tick().
    if (preShift || preMods != 0) dirtyKeyboard_ = true;
  } else if (ev.redraw) {
    // Mod/shift/layer toggle: visible state changed, repaint the keyboard.
    dirtyKeyboard_ = true;
    dirtyStatus_ = true;
  }
}

void HidDeck::toggleMode() {
  setMode(mode_ == kModeDeck ? kModeTrackpad : kModeDeck);
}

void HidDeck::setMode(Mode mode) {
  if (mode == mode_) return;
  mode_ = mode;
  trackpad_.reset();
  keyboard_.reset();
  dirtyAll_ = true;
}

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
bool HidDeck::hitOutputButton(int16_t x, int16_t y) const {
  return x >= 548 && x < 654 && y >= 4 && y < 36;
}
bool HidDeck::hitDictButton(int16_t x, int16_t y) const {
  return x >= 660 && x < 766 && y >= 4 && y < 36;
}
bool HidDeck::hitThemeButton(int16_t x, int16_t y) const {
  return x >= 772 && x < 878 && y >= 4 && y < 36;
}
bool HidDeck::hitModeButton(int16_t x, int16_t y) const {
  return x >= 884 && x < 1002 && y >= 4 && y < 36;
}

void HidDeck::drawStatusBar(Arduino_GFX *g) {
  const DeckTheme &t = theme();
  g->fillRect(0, 0, 1024, 40, t.surface);
  g->drawFastHLine(0, 40, 1024, t.line);
  Widgets::text(g, 16, 12, "CYPHER KEYS", Widgets::fontL(), t.ink, Widgets::kLeft);

  bool live = backend_.live();
  Widgets::pill(g, 168, 8, backend_.modeLabel(), Widgets::fontS(), t.onAccent,
                live ? t.good : t.warn);

  String info = String(presets_.activeName()) + "  -  " + theme().name;
  if (info.length() > 22) info = info.substring(0, 22);
  Widgets::text(g, 236, 14, info.c_str(), Widgets::fontS(), t.muted,
                Widgets::kLeft);

  // Last action sits between the info text and the right-hand button cluster
  // (which starts at x=548); keep it short so it never collides with OUT. Until
  // the first action, show the last reset cause (diagnostic for crash-reboots).
  String last = (backend_.reportsSent() == 0) ? bootReason_ : backend_.lastAction();
  if (last.length() > 15) last = last.substring(0, 15) + "...";
  Widgets::text(g, 400, 14, last.c_str(), Widgets::fontS(), t.accent,
                Widgets::kLeft);

  // Right-side buttons: Output toggle, Dictate, Theme, Mode toggle.
  bool ble = (backend_.output() == kOutputBle);
  Widgets::panel(g, 548, 4, 106, 32, 8, t.surfaceHi, 1, ble ? t.good : t.accent);
  Widgets::text(g, 601, 12, ble ? "BLE" : "USB", Widgets::fontS(), t.ink,
                Widgets::kCenter);
  if (ble) {  // connection dot: green when a host is connected, else warn
    uint16_t dot = backend_.bleReady() ? t.good : t.warn;
    g->fillCircle(645, 12, 4, dot);
  }
  Widgets::panel(g, 660, 4, 106, 32, 8, t.surfaceHi, 1, t.warn);
  Widgets::text(g, 713, 12, "DICTATE", Widgets::fontS(), t.ink, Widgets::kCenter);
  Widgets::panel(g, 772, 4, 106, 32, 8, t.surfaceHi, 1, t.accent2);
  Widgets::text(g, 825, 12, "THEME", Widgets::fontS(), t.ink, Widgets::kCenter);
  Widgets::panel(g, 884, 4, 118, 32, 8, t.surfaceHi, 1, t.accent);
  Widgets::text(g, 943, 12, mode_ == kModeDeck ? "TRACKPAD >" : "< DECK",
                Widgets::fontS(), t.ink, Widgets::kCenter);
}

void HidDeck::render(uint32_t nowMs) {
  if (!displayReady_) return;
  Arduino_GFX *g = CrowDisplay::canvas();
  if (g == nullptr) return;
  const DeckTheme &t = theme();

  if (dirtyAll_) {
    if (mode_ == kModeDeck) {
      g->fillRect(0, 40, 1024, 264, t.bg);
      presets_.draw(g, t);
      keyboard_.draw(g, t);
    } else {
      trackpad_.draw(g, t);
      // Mouse over Bluetooth is disabled (it panics the BLE stack); the trackpad
      // is USB-only. Make that obvious in BLE output.
      if (backend_.output() == kOutputBle) {
        Widgets::panel(g, 262, 250, 500, 96, 16, t.surfaceHi, 2, t.warn);
        Widgets::text(g, 512, 276, "TRACKPAD: USB ONLY", Widgets::fontL(), t.warn,
                      Widgets::kCenter);
        Widgets::text(g, 512, 306, "switch OUT to USB, or plug in the cable",
                      Widgets::fontS(), t.muted, Widgets::kCenter);
      }
    }
    drawStatusBar(g);
    lastStatusDrawMs_ = nowMs;
    dirtyAll_ = dirtyMacro_ = dirtyKeyboard_ = dirtyStatus_ = false;
    CrowDisplay::flush();  // one whole-frame sync
    return;
  }

  // Partial redraws each flush only their own band.
  if (mode_ == kModeDeck) {
    if (dirtyMacro_) {
      g->fillRect(0, 40, 1024, 264, t.bg);
      presets_.draw(g, t);
      dirtyMacro_ = false;
      CrowDisplay::flush(0, 40, 1024, 264);
    }
    if (dirtyKeyboard_) {
      keyboard_.draw(g, t);
      dirtyKeyboard_ = false;
      CrowDisplay::flush(0, 304, 1024, 296);
    }
  }
  // Status bar is low-value at high frequency: repaint at most ~5 Hz.
  if (dirtyStatus_ && (nowMs - lastStatusDrawMs_) >= kStatusMinMs) {
    drawStatusBar(g);
    dirtyStatus_ = false;
    lastStatusDrawMs_ = nowMs;
    CrowDisplay::flush(0, 0, 1024, 40);
  }
}
#endif
