#ifndef POKEDEX_TOUCH_KEYBOARD_H
#define POKEDEX_TOUCH_KEYBOARD_H

#include "../config/ProjectConfig.h"
#include <Arduino.h>

enum PokedexKeyAction {
  kPokedexKeyNone,
  kPokedexKeyText,
  kPokedexKeyBackspace,
  kPokedexKeyEnter,
  kPokedexKeyShift,
  kPokedexKeySymbols,
  kPokedexKeyLeft,
  kPokedexKeyRight,
  kPokedexKeyCancel
};

struct PokedexKeyEvent {
  PokedexKeyAction action = kPokedexKeyNone;
  String text;
};

class PokedexTouchKeyboard {
 public:
  void reset();
  bool shifted() const;
  bool symbols() const;
  void applyModeAction(PokedexKeyAction action);
  PokedexKeyEvent hitTest(int16_t x, int16_t y) const;

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  void draw(class Arduino_GFX *g) const;
#endif

 private:
  bool shifted_ = false;
  bool symbols_ = false;
};

#endif
