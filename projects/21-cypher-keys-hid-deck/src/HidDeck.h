#ifndef CYPHER_KEYS_HID_DECK_H
#define CYPHER_KEYS_HID_DECK_H

#include "../config/ProjectConfig.h"
#include "DeckThemes.h"
#include "HidBackend.h"
#include "HidKeyboard.h"
#include "MacroPresets.h"
#include "TouchInput.h"
#include "Trackpad.h"
#include <Arduino.h>

class EventLog;

// The Cypher Keys controller: owns the HID backend, the touch keyboard, the
// macro-preset pad, and the trackpad, and switches between the DECK and
// TRACKPAD views. Touch and Serial commands run the same HidBackend paths.
class HidDeck {
 public:
  enum Mode : uint8_t { kModeDeck, kModeTrackpad };

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

  void printStatus(Print &out);
  void printHid(Print &out);
  void printTouchDiagnostics(Print &out) const;

 private:
  bool parseCombo(const String &spec, uint8_t &mods, uint8_t &key) const;
  void handleDeckRelease(int16_t x, int16_t y);
  void toggleMode();
  void setMode(Mode mode);
  const DeckTheme &theme() const { return deckTheme(themeIndex_); }
  void loadTheme();
  void cycleTheme();
  void persistTheme() const;

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  void render(uint32_t nowMs);
  void drawStatusBar(class Arduino_GFX *g);
  bool hitModeButton(int16_t x, int16_t y) const;
  bool hitDictButton(int16_t x, int16_t y) const;
  bool hitThemeButton(int16_t x, int16_t y) const;
#endif

  HidBackend backend_;
  HidKeyboard keyboard_;
  MacroPresets presets_;
  Trackpad trackpad_;
  TouchInput touch_;
  EventLog *events_ = nullptr;
  Mode mode_ = kModeDeck;
  uint8_t themeIndex_ = 0;
  bool displayReady_ = false;

  bool dirtyAll_ = true;
  bool dirtyMacro_ = false;
  bool dirtyKeyboard_ = false;
  bool dirtyStatus_ = false;

  // Instant press-down feedback: the key rect currently drawn "pressed".
  bool keyPressed_ = false;
  int16_t pressKx_ = 0, pressKy_ = 0, pressKw_ = 0, pressKh_ = 0;
  // Status bar is repainted at most every kStatusMinMs to avoid thrash.
  uint32_t lastStatusDrawMs_ = 0;
  static const uint32_t kStatusMinMs = 200;
};

#endif
