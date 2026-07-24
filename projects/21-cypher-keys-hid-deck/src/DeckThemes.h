#ifndef CYPHER_KEYS_DECK_THEMES_H
#define CYPHER_KEYS_DECK_THEMES_H

#include <Arduino.h>

// RGB565 helper available in every build (Widgets::rgb is display-gated, but the
// theme table and the `theme` serial command exist headless too).
constexpr uint16_t deckRgb(uint8_t r, uint8_t g, uint8_t b) {
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

// One palette. Every drawable (keyboard, macro pad, trackpad, status bar) reads
// its colors from the active DeckTheme so a single switch restyles the whole UI.
struct DeckTheme {
  const char *name;
  uint16_t bg;         // page background
  uint16_t surface;    // card / tile fill
  uint16_t surfaceHi;  // highlighted card fill
  uint16_t line;       // borders and separators
  uint16_t ink;        // primary text
  uint16_t muted;      // secondary text
  uint16_t accent;     // main accent (active keys, tabs, borders)
  uint16_t accent2;    // combo-macro accent
  uint16_t warn;       // consumer-macro accent / MOCK pill
  uint16_t good;       // text-macro accent / LIVE pill
  uint16_t onAccent;   // text drawn on top of accent/good/warn fills
  uint16_t keyFill;    // keyboard key fill (even rows)
  uint16_t keyFillAlt; // keyboard key fill (odd rows)
};

uint8_t deckThemeCount();
const DeckTheme &deckTheme(uint8_t index);
int deckThemeIndexFromName(const String &name);  // prefix match; -1 if none

#endif
