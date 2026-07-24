#ifndef CYPHER_TUNE_TUNE_THEMES_H
#define CYPHER_TUNE_TUNE_THEMES_H

#include <Arduino.h>

// RGB565 helper available in every build (Widgets::rgb is display-gated, but the
// theme table and the `theme` serial command exist headless too).
constexpr uint16_t tuneRgb(uint8_t r, uint8_t g, uint8_t b) {
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

// One palette for the whole groovebox. Same idea as project 21's DeckTheme, but
// the roles are split for an instrument rather than a keyboard: the pads and the
// step lane get their own colors so a hit can flash hard against the chrome
// without washing the rest of the screen out.
struct TuneTheme {
  const char *name;
  uint16_t bg;          // page background
  uint16_t surface;     // chrome cards, step "off" cell, meter troughs
  uint16_t surfaceHi;   // highlighted card
  uint16_t line;        // borders and separators
  uint16_t ink;         // primary text
  uint16_t muted;       // secondary text
  uint16_t accent;      // selection borders, step accent fill, meters
  uint16_t accentDim;   // step "on" (non-accent) fill
  uint16_t padFill;     // pad idle fill (even rows)
  uint16_t padFillAlt;  // pad idle fill (odd rows) - subtle row banding
  uint16_t padSel;      // selected pad fill
  uint16_t padFlash;    // pad hit flash - the punchy one
  uint16_t playhead;    // step cell under the playhead
  uint16_t good;        // PLAY engaged
  uint16_t warn;        // metronome engaged
  uint16_t danger;      // REC armed
  uint16_t onAccent;    // text on accent/good/warn/danger/flash fills
};

uint8_t tuneThemeCount();
const TuneTheme &tuneTheme(uint8_t index);
int tuneThemeIndexFromName(const String &name);  // prefix match; -1 if none

#endif
