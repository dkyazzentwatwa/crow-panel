#ifndef CYPHER_BOY_UI_H
#define CYPHER_BOY_UI_H

#include "../config/ProjectConfig.h"
#include "GbRomStore.h"
#include <Arduino.h>

// Draws the two screens: the ROM picker and the in-game chrome (the gamepad
// overlay drawn around the live viewport). Rendering only - it never mutates
// app state; the .ino owns the screen enum and the transitions.
//
// The picker row maths is deliberately kept out of the display guard so the
// selftest can verify hit-testing headlessly.
class GbUi {
 public:
  static const int16_t kRowH = 64;
  static const int16_t kRowTop = 110;   // first row's top edge
  static const int16_t kRowX = 40;
  static const int16_t kRowW = 620;

  bool begin();
  void drawPicker(const GbRomStore &roms, int8_t selected);
  void drawPlayChrome(const String &title);
  void drawButtonState(uint32_t heldBits);  // light up pressed controls

  // Which ROM row a point lands on, or -1. Pure maths, headless-safe.
  static int8_t pickerHit(int16_t px, int16_t py, uint8_t romCount);

  bool ready() const { return ready_; }

 private:
  bool ready_ = false;
  uint32_t lastHeld_ = 0;
};

#endif
