#ifndef CYPHER_KEYS_HID_KEYBOARD_H
#define CYPHER_KEYS_HID_KEYBOARD_H

#include "../config/ProjectConfig.h"
#include "DeckThemes.h"
#include "HidTypes.h"
#include <Arduino.h>

// On-screen QWERTY forked from Cypher Desk's DeskTouchKeyboard: same weighted
// 4-row geometry and hit-testing, but each key now carries a USB HID target
// instead of editor text, and a Mac modifier row (Ctrl/Opt/Cmd) with sticky,
// one-shot behavior turns it into a real keyboard (tap Cmd then C -> Cmd+C).

// A resolved keypress the caller sends through HidBackend::tapKey.
struct HidKeyEvent {
  bool send = false;    // true when a key should be transmitted
  bool redraw = false;  // true when only visual state changed (mod/shift/layer)
  uint8_t key = 0;      // ASCII or kKey* constant
  uint8_t mods = 0;     // sticky modifiers to apply with this key (one-shot)
};

class HidKeyboard {
 public:
  void reset();
  bool shifted() const { return shifted_; }
  bool symbols() const { return symbols_; }
  uint8_t stickyMods() const { return stickyMods_; }

  // Resolve a touch point to a keypress or a state toggle. Updates internal
  // shift/symbols/sticky-modifier state and consumes one-shot modifiers.
  HidKeyEvent hitTest(int16_t x, int16_t y);

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  void draw(class Arduino_GFX *g, const DeckTheme &theme) const;

  // Find the on-screen rectangle of the key under (x,y) without mutating state.
  // Used for instant press feedback and single-key redraws. Returns false if
  // the point is not over a key.
  bool keyRectAt(int16_t x, int16_t y, int16_t &kx, int16_t &ky, int16_t &kw,
                 int16_t &kh) const;

  // Redraw just the key at the given rectangle. `pressed` draws the momentary
  // touch-down highlight; otherwise the key renders in its normal state.
  void drawSingleKey(class Arduino_GFX *g, const DeckTheme &theme, int16_t kx,
                     int16_t ky, int16_t kw, int16_t kh, bool pressed) const;
#endif

 private:
  bool shifted_ = false;
  bool symbols_ = false;
  uint8_t stickyMods_ = 0;
};

#endif
