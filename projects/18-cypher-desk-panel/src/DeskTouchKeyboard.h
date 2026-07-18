#ifndef CYPHER_DESK_PANEL_TOUCH_KEYBOARD_H
#define CYPHER_DESK_PANEL_TOUCH_KEYBOARD_H

#include "../config/ProjectConfig.h"
#include "DeskTypes.h"
#include <Arduino.h>

enum DeskKeyAction {
  kDeskKeyNone,
  kDeskKeyText,
  kDeskKeyBackspace,
  kDeskKeyEnter,
  kDeskKeyShift,
  kDeskKeySymbols,
  kDeskKeyLeft,
  kDeskKeyRight
};

struct DeskKeyEvent {
  DeskKeyAction action = kDeskKeyNone;
  String text;
};

class DeskTouchKeyboard {
 public:
  void reset();
  bool shifted() const;
  bool symbols() const;
  void applyModeAction(DeskKeyAction action);
  DeskKeyEvent hitTest(int16_t x, int16_t y) const;

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  void draw(class Arduino_GFX *g, const DeskThemePalette &theme) const;
#endif

 private:
  bool shifted_ = false;
  bool symbols_ = false;
};

#endif
