#ifndef CYPHER_KEYS_HID_DECK_H
#define CYPHER_KEYS_HID_DECK_H

#include "../config/ProjectConfig.h"
#include "DeckThemes.h"
#include <CrowHidBackend.h>
#include "HidKeyboard.h"
#include "KeyAudio.h"
#include "KeysTouch.h"
#include "MacroPresets.h"
#include "Trackpad.h"
#include <Arduino.h>

class EventLog;

// The Cypher Keys controller: owns the HID backend, the touch keyboard, the
// macro-preset pad, and the trackpad, and switches between the DECK, TRACKPAD
// and SETTINGS views. Touch and Serial commands run the same HidBackend paths.
class HidDeck {
 public:
  enum Mode : uint8_t { kModeDeck, kModeTrackpad, kModeSettings };

  void begin(EventLog *events);
  void tick();

  // Serial command surface (mirrors the on-screen actions).
  void commandKey(const String &text);       // type a literal string
  void commandCombo(const String &spec);      // e.g. "cmd+shift+4"
  void commandTap(const String &slot);        // fire macro slot N in active preset
  void commandPreset(const String &arg);      // "next" | name
  void commandMode(const String &arg);        // "deck" | "trackpad"
  void commandMouse(const String &args);      // "<dx> <dy>"
  void commandClick(const String &arg);       // "l" | "r"
  void commandScroll(const String &arg);      // signed wheel steps
  void commandMedia(const String &arg);       // volup|voldn|mute|play|brightup|brightdn
  void commandDictate();                       // tap F5 (macOS dictation/mic key)
  void commandTheme(const String &arg);        // "next" | name
  void commandOutput(const String &arg);       // "usb" | "ble" | "toggle"
  void commandBle(const String &arg);          // "status" | "clear"
  // ""|off|blue|brown|red|next|vol N|packs|pack <name>|<pack name>
  void commandSound(const String &arg);
  void commandSettings(const String &arg);     // ""|open|close
  void commandBright(const String &arg);       // ""|<0-255>|+|-
  void commandIdleDim(const String &arg);      // ""|on|off|toggle

  void printStatus(Print &out);
  void printHid(Print &out);
  void printTouchDiagnostics(Print &out) const;

  // Panel backlight, persisted alongside the theme. Floored at kMinBrightness
  // rather than 0: at 0 the panel still renders but shows nothing, which looks
  // exactly like a crash and leaves no visible way back to the + button.
  static const uint8_t kMinBrightness = 40;
  static const uint8_t kBrightnessStep = 24;

 private:
  bool parseCombo(const String &spec, uint8_t &mods, uint8_t &key) const;
  void handleDeckRelease(int16_t x, int16_t y);
  void toggleMode();
  void setMode(Mode mode);
  const DeckTheme &theme() const { return deckTheme(themeIndex_); }
  void loadSettings();
  void persistSettings() const;
  void cycleTheme();
  void stepTheme(int8_t dir);

  // Key sound selection. The Sound row and `sound next` cycle one flat list:
  // Off / Blue / Brown / Red first, then every pack folder found on the card.
  // All three do SD I/O, so all three are loop-context only, and all three
  // compile down to the synthesized-only path without USE_CYPHER_KEYS_SD.
  void stepSound(int8_t dir);
  // Loads one pack and reports the outcome on Serial + in KeyAudio::status().
  bool loadSoundPack_(const String &name);
  String soundPackList_() const;  // for `sound packs`

  // Backlight + idle dimming. Compiled in every build (the level is tracked
  // headless too, so `bright` and the NVS value stay meaningful); only the
  // actual LEDC write is display-gated, in applyBacklight_().
  void setBrightness(uint8_t level);   // clamped to kMinBrightness..255
  void bumpBrightness(int16_t delta);
  void setIdleDim(bool on);
  void noteActivity();                 // any touch or command wakes the panel
  void applyBrightness_();             // push brightness_/dim state out
  void applyBacklight_(uint8_t level);
  uint8_t dimTarget_() const;          // idle level, never above brightness_
  String settingsLine() const;         // serial `settings` / `bright` report

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  void render(uint32_t nowMs);
  void drawStatusBar(class Arduino_GFX *g);
  void drawSettings(class Arduino_GFX *g);
  void drawSettingsRow(class Arduino_GFX *g, const DeckTheme &t, uint8_t row);
  void handleSettingsRelease(int16_t x, int16_t y);
  // Multi-contact keyboard servicing: press / hold-repeat / release per finger.
  void serviceKeyboardTouch(class Arduino_GFX *g, uint32_t nowMs);
  // A layer toggle invalidates every other contact's key id (ids are
  // layer-relative), so drop their bindings and their pressed art.
  void invalidateKeyOwners(uint8_t exceptContact);
  // Idle dimming. Returns true when this tick's touch state belongs to a
  // wake-from-dim gesture and must not reach the UI at all.
  bool serviceIdleDim(uint32_t nowMs);
  void tickIdleDim(uint32_t nowMs);
  bool hitModeButton(int16_t x, int16_t y) const;
  bool hitDictButton(int16_t x, int16_t y) const;
  bool hitSetButton(int16_t x, int16_t y) const;
  bool hitOutputButton(int16_t x, int16_t y) const;
#endif

  HidBackend backend_;
  HidKeyboard keyboard_;
  MacroPresets presets_;
  Trackpad trackpad_;
  KeysTouch touch_;
  KeyAudio audio_;
  EventLog *events_ = nullptr;
  Mode mode_ = kModeDeck;
  Mode returnMode_ = kModeDeck;  // view BACK goes to (whichever opened SET)
  uint8_t themeIndex_ = 0;
  bool displayReady_ = false;

  // Backlight state. appliedLevel_ tracks what was last written to the LEDC
  // channel, which is what lets the idle dim RAMP instead of snapping.
  uint8_t brightness_ = CYPHER_KEYS_BRIGHTNESS;
  uint8_t appliedLevel_ = CYPHER_KEYS_BRIGHTNESS;
  bool idleDimEnabled_ = true;
  bool dimmed_ = false;
  bool wakeSuppress_ = false;  // consuming the gesture that woke the panel
  uint32_t lastActivityMs_ = 0;
  uint32_t lastDimStepMs_ = 0;
  static const uint8_t kDimStep = 6;         // backlight counts per ramp step
  static const uint32_t kDimStepMs = 16;     // ~0.6 s from 255 to 24

  bool dirtyAll_ = true;
  bool dirtyMacro_ = false;
  bool dirtyKeyboard_ = false;
  bool dirtyStatus_ = false;
  uint16_t dirtySettingsRows_ = 0;  // bit i = settings row i (KeysLayout ids)

  // Instant press-down feedback, one entry per contact: the key rect currently
  // drawn "pressed" for that finger, restored when it lifts.
  struct PressedArt {
    bool active = false;
    int16_t x = 0, y = 0, w = 0, h = 0;
  };
  PressedArt pressedArt_[KeysTouch::kMaxContacts];
  // Status bar is repainted at most every kStatusMinMs to avoid thrash.
  uint32_t lastStatusDrawMs_ = 0;
  static const uint32_t kStatusMinMs = 200;

  // Last chip-reset cause (panic / watchdog / brownout / ...), shown in the
  // status bar until the first action so a crash-reboot is diagnosable on-panel.
  String bootReason_;
};

#endif
