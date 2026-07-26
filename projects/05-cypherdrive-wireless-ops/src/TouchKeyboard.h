#ifndef CYPHERDRIVE_TOUCH_KEYBOARD_H
#define CYPHERDRIVE_TOUCH_KEYBOARD_H

#include "../config/ProjectConfig.h"
#include <Arduino.h>

// A compact on-screen QWERTY for entering a Wi-Fi key on the panel. Modeled on
// the Cypher Desk keyboard but self-contained: it draws through the shared
// Widgets palette (no external theme struct) and holds no text - the caller owns
// the buffer. hitTest() maps a tap to a semantic event and toggles shift/symbol
// modes internally.

enum KbAction : uint8_t {
  KB_NONE = 0,
  KB_CHAR,       // ev.ch is the character to insert
  KB_BACKSPACE,
  KB_ENTER,
  KB_CANCEL,
  KB_SHIFT,      // mode toggled internally; caller just repaints
  KB_SYMBOLS,
};

struct KbEvent {
  KbAction action = KB_NONE;
  char ch = 0;
};

class TouchKeyboard {
 public:
  void reset() { shift_ = false; symbols_ = false; }
  bool shifted() const { return shift_; }
  bool symbols() const { return symbols_; }

  // Map a tap to an event. Mode keys (SHIFT / 123 / ABC) toggle state here and
  // return KB_SHIFT / KB_SYMBOLS so the caller knows to repaint. A char key
  // clears a one-shot shift.
  KbEvent hitTest(int16_t x, int16_t y);

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  void draw(class Arduino_GFX *g) const;
#endif

 private:
  bool shift_ = false;
  bool symbols_ = false;
};

#endif
