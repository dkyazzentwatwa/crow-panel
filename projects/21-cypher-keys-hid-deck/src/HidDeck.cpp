#include "HidDeck.h"

#include "KeySoundPacks.h"  // SD sound packs (stubs without USE_CYPHER_KEYS_SD)
#include "KeysLayout.h"     // status-bar/band coordinates and their hit-tests
#include "KeysSplash.h"     // boot animation (no-op without a display)

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
  loadSettings();
  // The card is mounted BEFORE the MIPI-DSI framebuffer comes up: on this panel,
  // mounting SD_MMC once DSI is live can leave it backlit but blank (the
  // device-proven order from projects 02 and 20). No-op without the SD flag, and
  // the pack itself is read further down, after the display and the amp.
  KeySoundPacks::beginSd();
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  // manualFlush: draw a whole frame, then flush once (see DisplayBringup). This
  // turns Arduino_GFX's per-pixel cache sync into one sync per frame.
  displayReady_ = CrowDisplay::begin(activeHardwareProfile(), "Cypher Keys", true);
#endif
  // The backlight PWM only exists once the panel is up, so the stored level is
  // pushed here rather than in loadSettings().
  applyBrightness_();
  // Boot animation, straight into the cached framebuffer. Runs before the first
  // UI paint (dirtyAll_ below draws over it) and no-ops without a display. The
  // subtitle is the HID backend's mode, so the first thing on screen is the
  // truth about where keystrokes will go.
  KeysSplash::run(theme(), backend_.modeLabel());
  // After the display, so the first frame is not held up by the amp bring-up
  // (two blocking I2S writes of silence) or by synthesizing the click set.
  // Compiles to a no-op announcement without USE_CYPHER_KEYS_AUDIO.
  audio_.begin(activeHardwareProfile(), Serial);
#if USE_CYPHER_KEYS_SD
  // Re-load the sound pack chosen last boot (NVS "sndpack", by NAME so a
  // re-imaged card cannot silently select a different pack). This is SD I/O, so
  // it runs here in setup - loop context - never on the render task. A missing
  // pack falls back to the stored synthesized profile and says so in `status`.
  if (audio_.storedPackName()[0] != '\0') loadSoundPack_(audio_.storedPackName());
#endif
  dirtyAll_ = true;
}

// Theme, backlight level and the idle-dim switch share one NVS record; the
// preset (key "preset") and the click engine (keys "snd"/"sndvol") keep their
// own in the same namespace.
void HidDeck::loadSettings() {
  Preferences prefs;
  if (prefs.begin(CYPHER_KEYS_NVS_NAMESPACE, true)) {
    uint32_t stored = prefs.getUInt("theme", 0);
    uint32_t bright = prefs.getUInt("bright", CYPHER_KEYS_BRIGHTNESS);
    idleDimEnabled_ = prefs.getBool("idledim", true);
    prefs.end();
    if (stored < deckThemeCount()) themeIndex_ = (uint8_t)stored;
    brightness_ = bright > 255 ? 255 : (uint8_t)bright;
  }
  // Floored outside the read, so a too-low CYPHER_KEYS_BRIGHTNESS override is
  // clamped even on the first boot (when the namespace does not exist yet).
  if (brightness_ < kMinBrightness) brightness_ = kMinBrightness;
  appliedLevel_ = brightness_;
  lastActivityMs_ = millis();
}

void HidDeck::persistSettings() const {
  Preferences prefs;
  if (prefs.begin(CYPHER_KEYS_NVS_NAMESPACE, false)) {
    prefs.putUInt("theme", themeIndex_);
    prefs.putUInt("bright", brightness_);
    prefs.putBool("idledim", idleDimEnabled_);
    prefs.end();
  }
}

// --- Key sound selection -----------------------------------------------------
// One flat list: the synthesized profiles (Off / Blue / Brown / Red) followed by
// every pack folder on the card. The synthesized half never needs the card, so
// without USE_CYPHER_KEYS_SD these collapse to the plain profile cycle and the
// pack code leaves the binary entirely.

void HidDeck::stepSound(int8_t dir) {
#if USE_CYPHER_KEYS_SD
  String names[KeySoundPacks::kMaxPacks];
  const uint8_t packs = KeySoundPacks::listPacks(names, KeySoundPacks::kMaxPacks);
  const uint8_t total = (uint8_t)(KeyAudio::kProfileCount + packs);
  uint8_t current = (uint8_t)audio_.profile();
  if (audio_.packActive()) {
    current = KeyAudio::kProfileCount;  // a pack no longer on the card: land on
    for (uint8_t i = 0; i < packs; ++i) {  // the first one rather than wrap oddly
      if (names[i] == audio_.packName()) {
        current = (uint8_t)(KeyAudio::kProfileCount + i);
        break;
      }
    }
  }
  const uint8_t next = (uint8_t)((current + total + dir) % total);
  if (next < KeyAudio::kProfileCount) {
    audio_.setProfile((KeyAudio::Profile)next);
    return;
  }
  loadSoundPack_(names[next - KeyAudio::kProfileCount]);
#else
  audio_.setProfile((KeyAudio::Profile)((audio_.profile() +
                                         KeyAudio::kProfileCount + dir) %
                                        KeyAudio::kProfileCount));
#endif
}

// Reads one pack off the card into KeyAudio's staging clip set and asks the
// engine to swap to it. Loop context: the read is ~tens of ms, which is why it
// never happens on the keypress path or on the render task. The outcome (clip
// count, size, read time, any rejected file, and what it fell back to) is printed
// and kept in KeyAudio::status(), so there is one place that formats it.
bool HidDeck::loadSoundPack_(const String &name) {
#if USE_CYPHER_KEYS_SD
  KeySoundPacks::ClipSet *staging = audio_.beginPackStaging();
  if (staging == nullptr) {
    // Only reachable if the render task stopped acknowledging swaps, i.e. the
    // I2S path is wedged. Refusing is the safe answer; audio keeps playing
    // whatever it has.
    Serial.println(F("[keyaudio] sound engine busy, pack not loaded"));
    return false;
  }
  String status;
  bool ok = KeySoundPacks::loadPack(name.c_str(), *staging, status);
  if (ok) {
    ok = audio_.commitPackStaging(name.c_str(), status);
  } else {
    audio_.discardPackStaging();
  }
  if (!ok) status += String("; staying on ") + audio_.soundName();
  audio_.setPackStatus(status);
  dirtySettingsRows_ |= (uint16_t)1 << KeysLayout::kSetRowSound;
  Serial.println("[keyaudio] " + status);
  return ok;
#else
  (void)name;
  Serial.println(F("[keyaudio] SD sound packs are disabled "
                   "(build with -DUSE_CYPHER_KEYS_SD=1)"));
  return false;
#endif
}

String HidDeck::soundPackList_() const {
#if USE_CYPHER_KEYS_SD
  String names[KeySoundPacks::kMaxPacks];
  const uint8_t packs = KeySoundPacks::listPacks(names, KeySoundPacks::kMaxPacks);
  if (packs == 0) {
    return String("none under ") + CYPHER_KEYS_SOUNDS_DIR +
           (KeySoundPacks::sdReady() ? " (card mounted)" : " (no card mounted)");
  }
  String out;
  for (uint8_t i = 0; i < packs; ++i) {
    if (i != 0) out += " ";
    out += names[i];
  }
  return out + "   (" + CYPHER_KEYS_SOUNDS_DIR + ")";
#else
  return String("disabled - build with -DUSE_CYPHER_KEYS_SD=1");
#endif
}

void HidDeck::cycleTheme() { stepTheme(1); }

void HidDeck::stepTheme(int8_t dir) {
  uint8_t count = deckThemeCount();
  themeIndex_ = (uint8_t)((themeIndex_ + count + dir) % count);
  persistSettings();
  dirtyAll_ = true;  // every color changed; nothing partial would be coherent
}

// --- Backlight + idle dimming -----------------------------------------------
// Mechanism mirrors project 09's TuneUi: the level lives here, the only display
// dependency is CrowDisplay::setBacklight (the shared LEDC helper), and the
// state is tracked in headless builds too so `bright` stays meaningful.

uint8_t HidDeck::dimTarget_() const {
  uint8_t level = CYPHER_KEYS_IDLE_DIM_LEVEL;
  return level > brightness_ ? brightness_ : level;
}

void HidDeck::applyBacklight_(uint8_t level) {
  appliedLevel_ = level;
  // CrowDisplay only exists in USE_DISPLAY builds by design, so the call is
  // guarded here rather than relying on a stub.
#if USE_DISPLAY
  CrowDisplay::setBacklight(level);
#endif
}

void HidDeck::applyBrightness_() {
  applyBacklight_(dimmed_ ? dimTarget_() : brightness_);
}

void HidDeck::setBrightness(uint8_t level) {
  brightness_ = level < kMinBrightness ? kMinBrightness : level;
  dimmed_ = false;
  applyBrightness_();
  persistSettings();
}

void HidDeck::bumpBrightness(int16_t delta) {
  int16_t v = (int16_t)brightness_ + delta;
  if (v > 255) v = 255;
  if (v < (int16_t)kMinBrightness) v = kMinBrightness;
  setBrightness((uint8_t)v);
}

void HidDeck::setIdleDim(bool on) {
  idleDimEnabled_ = on;
  if (!on && dimmed_) {
    dimmed_ = false;
    applyBrightness_();
  }
  persistSettings();
}

void HidDeck::noteActivity() {
  lastActivityMs_ = millis();
  if (dimmed_) {
    dimmed_ = false;
    applyBrightness_();  // full level immediately, never a fade back up
  }
}

String HidDeck::settingsLine() const {
  return String("brightness ") + String(brightness_) + "/255 (min " +
         String(kMinBrightness) + ")  idle-dim " +
         (idleDimEnabled_ ? "on" : "off") + " after " +
         String(CYPHER_KEYS_IDLE_DIM_MS / 1000) + "s -> " +
         String(dimTarget_()) + (dimmed_ ? " (dimmed now)" : "") + "  theme " +
         theme().name + "  view " +
         (mode_ == kModeSettings
              ? "settings"
              : (mode_ == kModeTrackpad ? "trackpad" : "deck"));
}

void HidDeck::tick() {
  touch_.tick();
  uint32_t now = millis();
  backend_.service(now);  // perform any due (non-blocking) HID releases
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  if (!displayReady_) return;
  uint32_t before = backend_.reportsSent();
  Arduino_GFX *g = CrowDisplay::canvas();

  // Idle dimming runs first: a tap that wakes a dimmed panel is consumed whole
  // (down through up), so it can never also type a key or move the pointer.
  if (serviceIdleDim(now)) {
    render(now);
    return;
  }

  int16_t rx = touch_.releaseX();
  int16_t ry = touch_.releaseY();

  // Settings is dispatched by view, before anything else: the screens share no
  // controls, so routing here keeps a stale key, macro tile or chrome-button
  // rect from ever firing while settings is up.
  if (mode_ == kModeSettings) {
    if (touch_.releasedEdge()) handleSettingsRelease(rx, ry);
    render(now);
    return;
  }

  // The keyboard is multi-touch: every contact is serviced on its own, which is
  // what makes chording (hold CMD, tap C) and hold-repeat work. This runs
  // regardless of what the primary contact is doing on the chrome buttons, so a
  // key release can never be swallowed and leave a modifier stuck down.
  if (mode_ == kModeDeck) serviceKeyboardTouch(g, now);

  // The status-bar buttons, the macro band and the trackpad stay single-touch,
  // driven by the primary contact exactly as before.
  if (touch_.releasedEdge() && hitOutputButton(rx, ry)) {
    commandOutput("toggle");
  } else if (touch_.releasedEdge() && hitDictButton(rx, ry)) {
    commandDictate();
    dirtyStatus_ = true;
  } else if (touch_.releasedEdge() && hitSetButton(rx, ry)) {
    setMode(kModeSettings);
  } else if (touch_.releasedEdge() && hitModeButton(rx, ry)) {
    toggleMode();
  } else if (mode_ == kModeDeck) {
    if (touch_.releasedEdge()) handleDeckRelease(rx, ry);
  } else {
    trackpad_.update(touch_, backend_);
  }

  if (backend_.reportsSent() != before) dirtyStatus_ = true;  // discrete action
  render(now);
#endif
}

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
// Backlight side of the loop. Returns true while a wake-from-dim gesture is
// being swallowed, in which case the caller must not dispatch this tick's touch
// state anywhere.
bool HidDeck::serviceIdleDim(uint32_t nowMs) {
  bool pressed = false;
  for (uint8_t i = 0; i < KeysTouch::kMaxContacts; ++i) {
    if (touch_.contact(i).pressedEdge) {
      pressed = true;
      break;
    }
  }
  // A resting finger counts as activity, which is also why the panel can only
  // ever dim with nothing down - so a wake tap is always a fresh press.
  if (pressed || touch_.activeCount() > 0) {
    bool wasDimmed = dimmed_;
    noteActivity();
    if (wasDimmed && pressed) wakeSuppress_ = true;
  }

  if (wakeSuppress_) {
    // Drop every binding: a release still pending from the wake tap must not
    // replay as a key release (which would leave a modifier stuck down).
    for (uint8_t i = 0; i < KeysTouch::kMaxContacts; ++i) {
      touch_.contact(i).owner = HidKeyboard::kNoKey;
      touch_.contact(i).nextRepeatMs = 0;
    }
    // The gesture ends when the last finger lifts. Clearing on that same tick
    // (before returning) is what consumes its release edge too.
    if (touch_.activeCount() == 0) wakeSuppress_ = false;
    return true;
  }

  tickIdleDim(nowMs);
  return false;
}

void HidDeck::tickIdleDim(uint32_t nowMs) {
  if (!idleDimEnabled_) return;
  if (!dimmed_) {
    if ((uint32_t)(nowMs - lastActivityMs_) < CYPHER_KEYS_IDLE_DIM_MS) return;
    dimmed_ = true;
    lastDimStepMs_ = nowMs;
  }
  // Ramp down a few counts per step rather than snapping: a hard cut reads as a
  // glitch, a ~0.6 s fade reads as the panel going to sleep. Non-blocking, so
  // HID and Serial keep running through it.
  uint8_t target = dimTarget_();
  if (appliedLevel_ <= target) return;
  if ((uint32_t)(nowMs - lastDimStepMs_) < kDimStepMs) return;
  lastDimStepMs_ = nowMs;
  uint8_t next = appliedLevel_ > kDimStep ? (uint8_t)(appliedLevel_ - kDimStep) : 0;
  applyBacklight_(next < target ? target : next);
}
#endif

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
void HidDeck::invalidateKeyOwners(uint8_t exceptContact) {
  for (uint8_t i = 0; i < KeysTouch::kMaxContacts; ++i) {
    if (i == exceptContact) continue;
    KeysTouch::Contact &c = touch_.contact(i);
    c.owner = HidKeyboard::kNoKey;
    c.nextRepeatMs = 0;
    pressedArt_[i].active = false;  // the full repaint below covers the art
  }
}

// One pass over the live contacts: bind new presses, fire due repeats, and
// settle releases. Each key is decided at touch-DOWN and hit-tested against the
// DOWN position, so sliding a finger off a key can never fire a different one.
void HidDeck::serviceKeyboardTouch(Arduino_GFX *g, uint32_t nowMs) {
  for (uint8_t i = 0; i < KeysTouch::kMaxContacts; ++i) {
    KeysTouch::Contact &c = touch_.contact(i);

    if (c.pressedEdge) {
      // Snapshot what is visible now: the rect must come from the layer that is
      // still up, and a consumed one-shot means every mod key's art changed.
      int16_t kx = 0, ky = 0, kw = 0, kh = 0;
      bool haveRect = keyboard_.keyRectAt(c.downX, c.downY, kx, ky, kw, kh);
      bool preShift = keyboard_.shifted();
      uint8_t preMods = keyboard_.stickyMods();
      bool preSymbols = keyboard_.symbols();

      int16_t keyId = HidKeyboard::kNoKey;
      HidKeyEvent ev = keyboard_.pressAt(c.downX, c.downY, keyId);
      c.owner = keyId;
      c.nextRepeatMs = 0;

      // The switch click belongs with the instant press art, not with the HID
      // report: a real mechanical switch actuates on the way DOWN. haveRect is
      // true only over a keyboard key, which is what keeps macro tiles, the
      // chrome buttons and the trackpad silent.
      //
      // The class + row pick the right clip out of an SD sound pack (dedicated
      // Backspace/Enter/Space samples, GENERIC_R<row> otherwise); the synthesized
      // profiles ignore both. Resolved after pressAt because that is when the id
      // exists - and the only key that flips the layer under us (123/ABC) is
      // generic in both layers, so the classification cannot change.
      if (haveRect) {
        audio_.press((KeyAudio::KeyClass)keyboard_.keySoundClass(keyId),
                     keyboard_.keySoundRow(keyId));
      }

      // Instant press-down feedback: light the touched key and flush only its
      // region (sub-ms), so the panel answers the finger immediately.
      if (g && haveRect) {
        keyboard_.drawSingleKey(g, theme(), kx, ky, kw, kh, /*pressed=*/true);
        CrowDisplay::flush(kx - 3, ky - 3, kw + 10, kh + 10);
        pressedArt_[i].active = true;
        pressedArt_[i].x = kx;
        pressedArt_[i].y = ky;
        pressedArt_[i].w = kw;
        pressedArt_[i].h = kh;
      }

      if (ev.send) {
        backend_.tapKey(ev.mods, ev.key);
        // A one-shot shift/modifier was consumed -> those keys' highlights
        // changed, so redraw the whole keyboard. Plain typing leaves it to the
        // single-key restore on release.
        if (preShift || preMods != 0) dirtyKeyboard_ = true;
        if (keyboard_.repeats(keyId)) {
          c.nextRepeatMs = nowMs + CYPHER_KEYS_KEY_REPEAT_DELAY_MS;
        }
      } else if (ev.redraw) {
        // Mod/shift/layer toggle: visible state changed, repaint the keyboard.
        dirtyKeyboard_ = true;
        dirtyStatus_ = true;
      }
      if (keyboard_.symbols() != preSymbols) invalidateKeyOwners(i);
      continue;  // a press and its repeat can never be due on the same tick
    }

    // Hold-to-repeat for Backspace and the arrows. Skipped while the contact is
    // inside its release debounce window: the panel currently reads no finger,
    // so a repeat now would be one extra character past the lift. The due time
    // is not advanced, so if it was only sensor flicker the repeat fires as soon
    // as the contact is back and the cadence is unchanged.
    if (c.active && !c.releasePending && c.owner != HidKeyboard::kNoKey &&
        c.nextRepeatMs != 0 && (int32_t)(nowMs - c.nextRepeatMs) >= 0 &&
        keyboard_.repeats(c.owner)) {
      HidKeyEvent ev = keyboard_.repeatKey(c.owner);
      if (ev.send) backend_.tapKey(ev.mods, ev.key);
      c.nextRepeatMs = nowMs + CYPHER_KEYS_KEY_REPEAT_MS;
    }

    if (c.releasedEdge) {
      if (c.owner != HidKeyboard::kNoKey) {
        // The "clack". owner != kNoKey is exactly the condition that produced
        // the press click, so every click gets its matching release. Classified
        // before releaseKey() for the same reason the press is classified after
        // pressAt(): the id must be read against the layer it belongs to.
        audio_.release((KeyAudio::KeyClass)keyboard_.keySoundClass(c.owner),
                       keyboard_.keySoundRow(c.owner));
        HidKeyEvent ev = keyboard_.releaseKey(c.owner);
        if (ev.send) backend_.tapKey(ev.mods, ev.key);
        if (ev.redraw) {
          dirtyKeyboard_ = true;
          dirtyStatus_ = true;
        }
      }
      // Clear this finger's press highlight. If a full keyboard redraw is
      // already pending it repaints the key; otherwise restore just that one
      // key - the fast path for ordinary typing.
      if (pressedArt_[i].active) {
        if (g && !dirtyKeyboard_ && !dirtyAll_) {
          const PressedArt &a = pressedArt_[i];
          keyboard_.drawSingleKey(g, theme(), a.x, a.y, a.w, a.h,
                                  /*pressed=*/false);
          CrowDisplay::flush(a.x - 3, a.y - 3, a.w + 10, a.h + 10);
        }
        pressedArt_[i].active = false;
      }
      c.owner = HidKeyboard::kNoKey;
      c.nextRepeatMs = 0;
    }
  }
}
#endif

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
    persistSettings();
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

// `sound` on its own reports; `sound off|blue|brown|red` picks a synthesized
// switch profile; `sound packs` lists the SD sound packs and `sound pack <name>`
// (or just `sound <name>`) loads one; `sound next` cycles the whole list,
// profiles then packs; `sound vol <0-100>` sets the level. All persist in NVS.
void HidDeck::commandSound(const String &arg) {
  String raw = arg;
  raw.trim();
  String a = raw;  // lowercased for matching; `raw` keeps a pack's real case
  a.toLowerCase();
  if (a.startsWith("vol")) {
    int sp = a.indexOf(' ');
    if (sp >= 0) {  // bare "sound vol" just reports, like bare "sound"
      long pct = atol(a.c_str() + sp + 1);
      audio_.setVolume((uint8_t)(pct < 0 ? 0 : (pct > 100 ? 100 : pct)));
    }
  } else if (a == "next") {
    stepSound(1);
  } else if (a == "prev") {
    stepSound(-1);
  } else if (a == "packs") {
    Serial.print(F("[keyaudio] packs: "));
    Serial.println(soundPackList_());
  } else if (a.startsWith("pack")) {
    String name = raw.substring(4);
    name.trim();
    if (name.length() == 0) {
      Serial.println("usage: sound pack <name>   (see `sound packs`)");
    } else {
      loadSoundPack_(name);
    }
  } else if (a.length() != 0) {
    // A bare word is a profile name if it matches one, otherwise a pack name -
    // so `sound alpaca` works exactly like `sound pack alpaca`.
    if (!audio_.selectProfileByName(a)) loadSoundPack_(raw);
  }
  // The settings screen mirrors both values, so repaint its rows if it is up.
  dirtySettingsRows_ |= ((uint16_t)1 << KeysLayout::kSetRowSound) |
                        ((uint16_t)1 << KeysLayout::kSetRowVolume);
  Serial.println(audio_.status());  // always report the resulting state
}

// `settings` on its own reports; `settings open|close` switches the view.
void HidDeck::commandSettings(const String &arg) {
  String a = arg;
  a.trim();
  a.toLowerCase();
  noteActivity();
  if (a == "open" || a == "on") {
    setMode(kModeSettings);
  } else if (a == "close" || a == "off" || a == "back") {
    if (mode_ == kModeSettings) setMode(returnMode_);
  } else if (a.length() != 0) {
    Serial.println("usage: settings [open|close]");
    return;
  }
  Serial.println("settings: " + settingsLine());
}

// `bright` reports; `bright <0-255>` sets (floored at kMinBrightness);
// `bright +|-` steps by kBrightnessStep.
void HidDeck::commandBright(const String &arg) {
  String a = arg;
  a.trim();
  a.toLowerCase();
  noteActivity();
  if (a == "+" || a == "up") {
    bumpBrightness(kBrightnessStep);
  } else if (a == "-" || a == "down") {
    bumpBrightness(-(int16_t)kBrightnessStep);
  } else if (a.length() != 0) {
    long v = a.toInt();
    setBrightness((uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v)));
  }
  dirtySettingsRows_ |= (uint16_t)1 << KeysLayout::kSetRowBrightness;
  Serial.println("bright: " + String(brightness_) + "/255 (min " +
                 String(kMinBrightness) + ")");
}

void HidDeck::commandIdleDim(const String &arg) {
  String a = arg;
  a.trim();
  a.toLowerCase();
  noteActivity();
  if (a == "on" || a == "1") {
    setIdleDim(true);
  } else if (a == "off" || a == "0") {
    setIdleDim(false);
  } else if (a == "toggle") {
    setIdleDim(!idleDimEnabled_);
  } else if (a.length() != 0) {
    Serial.println("usage: idledim on|off|toggle");
    return;
  }
  dirtySettingsRows_ |= (uint16_t)1 << KeysLayout::kSetRowIdleDim;
  Serial.println(String("idledim: ") + (idleDimEnabled_ ? "on" : "off") +
                 " after " + String(CYPHER_KEYS_IDLE_DIM_MS / 1000) + "s -> " +
                 String(dimTarget_()));
}

void HidDeck::printStatus(Print &out) {
  out.println("== Cypher Keys HID Deck ==");
  out.print("mode: ");
  out.println(mode_ == kModeSettings
                  ? "settings"
                  : (mode_ == kModeTrackpad ? "trackpad" : "deck"));
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
  // Switch profile + volume (and the I2S state) in one line, same text `sound`
  // prints, so there is only one place that formats it.
  out.println(audio_.status());
  out.print("panel: ");
  out.println(settingsLine());
  out.print("ble: advertising=");
  out.print(backend_.bleAdvertising() ? 1 : 0);
  out.print(" connected=");
  out.println(backend_.bleReady() ? 1 : 0);
  out.print("mods: held=");
  out.print(keyboard_.heldMods() ? hidModPrefix(keyboard_.heldMods()) : "(none) ");
  out.print(" sticky=");
  out.print(keyboard_.stickyMods() ? hidModPrefix(keyboard_.stickyMods())
                                  : "(none) ");
  out.print(" shift=");
  out.println(keyboard_.heldShift() ? "held"
                                    : (keyboard_.shifted() ? "sticky" : "off"));
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
  out.print("[touch] ");
  out.println(touch_.diagnostics());
  out.print("[touch] primary down=");
  out.print(touch_.down() ? 1 : 0);
  out.print(" x=");
  out.print(touch_.x());
  out.print(" y=");
  out.print(touch_.y());
  out.print(" release_count=");
  out.println(touch_.count());
  out.print("[keys] held=");
  out.print(keyboard_.heldMods() ? hidModPrefix(keyboard_.heldMods()) : "(none) ");
  out.print(" sticky=");
  out.print(keyboard_.stickyMods() ? hidModPrefix(keyboard_.stickyMods())
                                  : "(none) ");
  out.print(" shift=");
  out.print(keyboard_.heldShift() ? "held" : (keyboard_.shifted() ? "sticky" : "off"));
  out.print(" layer=");
  out.println(keyboard_.symbols() ? "123" : "ABC");
}

// The keyboard itself is serviced per contact in serviceKeyboardTouch(); this is
// only the single-touch macro band (preset tabs and macro tiles), which still
// fires on the primary contact's release.
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
  }
}

void HidDeck::toggleMode() {
  setMode(mode_ == kModeDeck ? kModeTrackpad : kModeDeck);
}

void HidDeck::setMode(Mode mode) {
  if (mode == mode_) return;
  // Remember where SET was opened from so BACK returns there, not always to the
  // deck (the button is in the status bar of both working views).
  if (mode == kModeSettings) returnMode_ = mode_;
  mode_ = mode;
  dirtySettingsRows_ = 0;
  trackpad_.reset();
  keyboard_.reset();
  // Any finger still resting on a key loses its binding: the other view owns the
  // screen now, so its release must not be replayed as a keyboard release.
  for (uint8_t i = 0; i < KeysTouch::kMaxContacts; ++i) {
    touch_.contact(i).owner = HidKeyboard::kNoKey;
    touch_.contact(i).nextRepeatMs = 0;
    pressedArt_[i].active = false;
  }
  dirtyAll_ = true;
}

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
// The status-bar buttons' rects live in KeysLayout so the panels drawn below and
// these hit-tests can never drift apart.
bool HidDeck::hitOutputButton(int16_t x, int16_t y) const {
  return KeysLayout::hitOutputButton(x, y);
}
bool HidDeck::hitDictButton(int16_t x, int16_t y) const {
  return KeysLayout::hitDictButton(x, y);
}
bool HidDeck::hitSetButton(int16_t x, int16_t y) const {
  return KeysLayout::hitSetButton(x, y);
}
bool HidDeck::hitModeButton(int16_t x, int16_t y) const {
  return KeysLayout::hitModeButton(x, y);
}

void HidDeck::drawStatusBar(Arduino_GFX *g) {
  using namespace KeysLayout;  // every coordinate below comes from KeysLayout.h
  const DeckTheme &t = theme();
  g->fillRect(0, kStatusY, kScreenW, kStatusH, t.surface);
  g->drawFastHLine(0, kStatusH, kScreenW, t.line);
  Widgets::text(g, kTitleX, kTitleY, "CYPHER KEYS", Widgets::fontL(), t.ink,
                Widgets::kLeft);

  bool live = backend_.live();
  Widgets::pill(g, kPillX, kPillY, backend_.modeLabel(), Widgets::fontS(),
                t.onAccent, live ? t.good : t.warn);

  String info = String(presets_.activeName()) + "  -  " + theme().name;
  if (info.length() > 22) info = info.substring(0, 22);
  Widgets::text(g, kInfoX, kInfoY, info.c_str(), Widgets::fontS(), t.muted,
                Widgets::kLeft);

  // Last action sits between the info text and the right-hand button cluster
  // (which starts at kOutBtnX); keep it short so it never collides with OUT.
  // Until the first action, show the last reset cause (diagnostic for
  // crash-reboots).
  String last = (backend_.reportsSent() == 0) ? bootReason_ : backend_.lastAction();
  if (last.length() > 15) last = last.substring(0, 15) + "...";
  Widgets::text(g, kLastActionX, kLastActionY, last.c_str(), Widgets::fontS(),
                t.accent, Widgets::kLeft);

  // Right-side buttons: Output toggle, Dictate, Settings, Mode toggle.
  bool ble = (backend_.output() == kOutputBle);
  Widgets::panel(g, kOutBtnX, kBtnY, kOutBtnW, kBtnH, 8, t.surfaceHi, 1,
                 ble ? t.good : t.accent);
  Widgets::text(g, kOutLabelX, kBtnLabelY, ble ? "BLE" : "USB", Widgets::fontS(),
                t.ink, Widgets::kCenter);
  if (ble) {  // connection dot: green when a host is connected, else warn
    uint16_t dot = backend_.bleReady() ? t.good : t.warn;
    g->fillCircle(kOutDotX, kOutDotY, kOutDotR, dot);
  }
  Widgets::panel(g, kDictBtnX, kBtnY, kDictBtnW, kBtnH, 8, t.surfaceHi, 1, t.warn);
  Widgets::text(g, kDictLabelX, kBtnLabelY, "DICTATE", Widgets::fontS(), t.ink,
                Widgets::kCenter);
  Widgets::panel(g, kSetBtnX, kBtnY, kSetBtnW, kBtnH, 8, t.surfaceHi, 1,
                 t.accent2);
  Widgets::text(g, kSetBtnLabelX, kBtnLabelY, "SET", Widgets::fontS(), t.ink,
                Widgets::kCenter);
  Widgets::panel(g, kModeBtnX, kBtnY, kModeBtnW, kBtnH, 8, t.surfaceHi, 1,
                 t.accent);
  Widgets::text(g, kModeLabelX, kBtnLabelY,
                mode_ == kModeDeck ? "TRACKPAD >" : "< DECK", Widgets::fontS(),
                t.ink, Widgets::kCenter);
}

// One settings row: label on the left, steppers, a bar or a name panel, and the
// value right-aligned. Each row clears its own full-width band first, so it can
// be repainted and flushed on its own (see render()).
void HidDeck::drawSettingsRow(Arduino_GFX *g, const DeckTheme &t, uint8_t row) {
  using namespace KeysLayout;
  int16_t y = setRowY(row);
  g->fillRect(0, y, kScreenW, kSetRowH, t.bg);

  const char *label = "";
  switch (row) {
    case kSetRowSound:      label = "KEY SOUND"; break;
    case kSetRowVolume:     label = "SOUND VOL"; break;
    case kSetRowBrightness: label = "BRIGHTNESS"; break;
    case kSetRowTheme:      label = "THEME"; break;
    case kSetRowIdleDim:    label = "IDLE DIM"; break;
    default: break;
  }
  Widgets::text(g, kSetLabelX, y + 18, label, Widgets::fontM(), t.ink);

  // Idle dim is a two-state toggle, so it gets no stepper; the name-picking rows
  // get arrows instead of plus/minus.
  bool stepper = row != kSetRowIdleDim;
  bool arrows = row == kSetRowSound || row == kSetRowTheme;
  if (stepper) {
    Widgets::panel(g, kSetMinusX, y, kSetStepW, kSetRowH, 8, t.surface, 1, t.line);
    Widgets::text(g, kSetMinusX + kSetStepW / 2, y + 12, arrows ? "<" : "-",
                  Widgets::fontL(), t.ink, Widgets::kCenter);
    Widgets::panel(g, kSetPlusX, y, kSetStepW, kSetRowH, 8, t.surface, 1, t.line);
    Widgets::text(g, kSetPlusX + kSetStepW / 2, y + 12, arrows ? ">" : "+",
                  Widgets::fontL(), t.ink, Widgets::kCenter);
  }

  switch (row) {
    case kSetRowSound: {
      // The name is a synthesized profile OR the folder name of the SD sound
      // pack in use, because the row cycles through both.
      bool pack = audio_.packActive();
      bool off = !pack && audio_.profile() == KeyAudio::kOff;
      Widgets::panel(g, kSetBarX, y, kSetBarW, kSetRowH, 8, t.surface, 1, t.line);
      Widgets::text(g, kSetBarX + kSetBarW / 2, y + 16, audio_.soundName(),
                    Widgets::fontM(), off ? t.muted : t.accent, Widgets::kCenter);
      // Right-hand value is the honest engine state, not a repeat of the name:
      // "sd" means real recorded samples off the card, "i2s" the synthesized
      // set, and without USE_CYPHER_KEYS_AUDIO there is no I2S at all.
      const char *engine = pack ? "sd" : (audio_.ready() ? "i2s" : "n/a");
      Widgets::text(g, kSetValueX, y + 18, engine, Widgets::fontS(),
                    audio_.ready() ? t.good : t.muted, Widgets::kRight);
      break;
    }
    case kSetRowVolume: {
      uint8_t vol = audio_.volume();
      Widgets::hBar(g, kSetBarX, y + 12, kSetBarW, 28, vol / 100.0f, t.accent,
                    t.surface);
      Widgets::text(g, kSetValueX, y + 18, (String(vol) + "%").c_str(),
                    Widgets::fontM(), t.ink, Widgets::kRight);
      break;
    }
    case kSetRowBrightness: {
      // The bar spans the usable range (kMinBrightness..255), not 0..255, so a
      // full-left bar still matches the dimmest the panel actually goes.
      float frac = (float)(brightness_ - kMinBrightness) / (255 - kMinBrightness);
      Widgets::hBar(g, kSetBarX, y + 12, kSetBarW, 28, frac, t.accent, t.surface);
      Widgets::text(g, kSetValueX, y + 18, String(brightness_).c_str(),
                    Widgets::fontM(), t.ink, Widgets::kRight);
      break;
    }
    case kSetRowTheme:
      Widgets::panel(g, kSetBarX, y, kSetBarW, kSetRowH, 8, t.surface, 1, t.line);
      Widgets::text(g, kSetBarX + kSetBarW / 2, y + 16, t.name, Widgets::fontM(),
                    t.accent, Widgets::kCenter);
      Widgets::text(g, kSetValueX, y + 18,
                    (String(themeIndex_ + 1) + "/" + String(deckThemeCount())).c_str(),
                    Widgets::fontS(), t.muted, Widgets::kRight);
      break;
    case kSetRowIdleDim: {
      Widgets::panel(g, kSetBarX, y, kSetBarW, kSetRowH, 8,
                     idleDimEnabled_ ? t.good : t.surface, 1, t.line);
      String state = idleDimEnabled_
                         ? String("ON - dims after ") +
                               String(CYPHER_KEYS_IDLE_DIM_MS / 1000) + "s"
                         : String("OFF");
      Widgets::text(g, kSetBarX + kSetBarW / 2, y + 16, state.c_str(),
                    Widgets::fontM(),
                    idleDimEnabled_ ? t.onAccent : t.muted, Widgets::kCenter);
      Widgets::text(g, kSetValueX, y + 18,
                    idleDimEnabled_ ? String(dimTarget_()).c_str() : "-",
                    Widgets::fontS(), t.muted, Widgets::kRight);
      break;
    }
    default:
      break;
  }
}

void HidDeck::drawSettings(Arduino_GFX *g) {
  using namespace KeysLayout;
  const DeckTheme &t = theme();
  g->fillScreen(t.bg);

  Widgets::panel(g, kSetBackX, kSetBackY, kSetBackW, kSetBackH, 8, t.surface, 1,
                 t.line);
  Widgets::text(g, kSetBackX + kSetBackW / 2, kSetBackY + 11, "< BACK",
                Widgets::fontS(), t.ink, Widgets::kCenter);
  Widgets::text(g, kSetTitleX, kSetTitleY, "SETTINGS", Widgets::fontL(), t.ink,
                Widgets::kCenter);
  g->drawFastHLine(0, kSetHeaderH, kScreenW, t.line);

  for (uint8_t row = 0; row < kSetRowCount; ++row) {
    drawSettingsRow(g, t, row);
  }

  // Read-only footer: the numbers worth seeing when something sounds or feels
  // wrong, in one place instead of squeezed into the deck's status bar.
  g->drawFastHLine(0, kSetInfoY - 16, kScreenW, t.line);
  Widgets::text(g, kSetLabelX, kSetInfoY, "SOUND ENGINE", Widgets::fontS(),
                t.muted);
  Widgets::text(g, kSetValueX, kSetInfoY,
                (String("out ") + backend_.modeLabel() + "   heap " +
                 String(ESP.getFreeHeap() / 1024) + "K")
                    .c_str(),
                Widgets::fontS(), t.muted, Widgets::kRight);
  Widgets::text(g, kSetLabelX, kSetInfoY + 26, audio_.status().c_str(),
                Widgets::fontS(), t.ink);
  Widgets::text(g, kSetLabelX, kSetInfoY + 52,
                "keyboard, macro presets and the trackpad are on the deck screen",
                Widgets::fontS(), t.muted);
}

// Settings taps come in on the primary contact's release, exactly like the
// deck's chrome buttons, so press/release can never straddle a view change.
void HidDeck::handleSettingsRelease(int16_t x, int16_t y) {
  using namespace KeysLayout;
  int16_t control = KeysLayout::hitTestSettings(x, y);
  switch (control) {
    case kControlBack:
      setMode(returnMode_);
      break;
    case kControlSoundNext:
    case kControlSoundPrev:
      // Cycles Off / Blue / Brown / Red and then every SD pack on the card.
      // Landing on a pack reads it off the card here, in loop context - tens of
      // ms on a settings tap, never on the typing path.
      stepSound(control == kControlSoundNext ? 1 : -1);
      // Audition the switch you just picked. Generic class, middle row: the
      // sound a letter key would make.
      audio_.press(KeyAudio::kKeyGeneric, 1);
      dirtySettingsRows_ |= (uint16_t)1 << kSetRowSound;
      break;
    case kControlVolumeMinus:
    case kControlVolumePlus: {
      int16_t v = (int16_t)audio_.volume() +
                  (control == kControlVolumePlus ? 5 : -5);
      if (v < 0) v = 0;
      if (v > 100) v = 100;
      audio_.setVolume((uint8_t)v);
      audio_.press(KeyAudio::kKeyGeneric, 1);  // hear the level, not just the bar
      dirtySettingsRows_ |= (uint16_t)1 << kSetRowVolume;
      break;
    }
    case kControlBrightMinus:
      bumpBrightness(-(int16_t)kBrightnessStep);
      dirtySettingsRows_ |= (uint16_t)1 << kSetRowBrightness;
      break;
    case kControlBrightPlus:
      bumpBrightness(kBrightnessStep);
      dirtySettingsRows_ |= (uint16_t)1 << kSetRowBrightness;
      break;
    case kControlThemePrev:
      stepTheme(-1);  // sets dirtyAll_: every color on screen changed
      break;
    case kControlThemeNext:
      stepTheme(1);
      break;
    case kControlIdleDim:
      setIdleDim(!idleDimEnabled_);
      dirtySettingsRows_ |= (uint16_t)1 << kSetRowIdleDim;
      break;
    default:
      break;
  }
}

void HidDeck::render(uint32_t nowMs) {
  using namespace KeysLayout;  // every coordinate below comes from KeysLayout.h
  if (!displayReady_) return;
  Arduino_GFX *g = CrowDisplay::canvas();
  if (g == nullptr) return;
  const DeckTheme &t = theme();

  // Settings owns the whole screen, so it renders on its own path and returns
  // before any deck/trackpad band (or the status bar) can paint over it.
  if (mode_ == kModeSettings) {
    if (dirtyAll_) {
      drawSettings(g);
      dirtyAll_ = dirtyMacro_ = dirtyKeyboard_ = dirtyStatus_ = false;
      dirtySettingsRows_ = 0;
      CrowDisplay::flush();
      return;
    }
    if (dirtySettingsRows_ == 0) return;
    for (uint8_t row = 0; row < kSetRowCount; ++row) {
      if ((dirtySettingsRows_ & ((uint16_t)1 << row)) == 0) continue;
      drawSettingsRow(g, t, row);
      CrowDisplay::flush(0, setRowY(row), kScreenW, kSetRowH);
    }
    dirtySettingsRows_ = 0;
    return;
  }

  if (dirtyAll_) {
    if (mode_ == kModeDeck) {
      g->fillRect(0, kMacroBandY, kScreenW, kMacroBandH, t.bg);
      presets_.draw(g, t);
      keyboard_.draw(g, t);
    } else {
      trackpad_.draw(g, t);
      // Mouse over Bluetooth is disabled (it panics the BLE stack); the trackpad
      // is USB-only. Make that obvious in BLE output.
      if (backend_.output() == kOutputBle) {
        Widgets::panel(g, kUsbOnlyX, kUsbOnlyY, kUsbOnlyW, kUsbOnlyH, 16,
                       t.surfaceHi, 2, t.warn);
        Widgets::text(g, kUsbOnlyLabelX, kUsbOnlyTitleY, "TRACKPAD: USB ONLY",
                      Widgets::fontL(), t.warn, Widgets::kCenter);
        Widgets::text(g, kUsbOnlyLabelX, kUsbOnlyHintY,
                      "switch OUT to USB, or plug in the cable", Widgets::fontS(),
                      t.muted, Widgets::kCenter);
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
      g->fillRect(0, kMacroBandY, kScreenW, kMacroBandH, t.bg);
      presets_.draw(g, t);
      dirtyMacro_ = false;
      CrowDisplay::flush(0, kMacroBandY, kScreenW, kMacroBandH);
    }
    if (dirtyKeyboard_) {
      keyboard_.draw(g, t);
      dirtyKeyboard_ = false;
      CrowDisplay::flush(0, kKeyboardBandY, kScreenW, kKeyboardBandH);
    }
  }
  // Status bar is low-value at high frequency: repaint at most ~5 Hz.
  if (dirtyStatus_ && (nowMs - lastStatusDrawMs_) >= kStatusMinMs) {
    drawStatusBar(g);
    dirtyStatus_ = false;
    lastStatusDrawMs_ = nowMs;
    CrowDisplay::flush(0, kStatusY, kScreenW, kStatusH);
  }
}
#endif
