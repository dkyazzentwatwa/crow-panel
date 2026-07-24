#ifndef CYPHER_BOY_THEME_H
#define CYPHER_BOY_THEME_H

#include "../config/ProjectConfig.h"
#include <Arduino.h>

// Themes, following the same shape as Cypher Desk's DeskTheme: an id enum, a
// palette struct in RGB565, and next/fromName helpers driving a `theme` serial
// command.
//
// Themes restyle the CHROME ONLY - picker, settings, gamepad, pause overlay.
// The emulated screen is deliberately left alone: it should look like the game
// looks, not like the theme. (gnuboy CAN retint the DMG screen via
// gnuboy_set_palette, but that is intentionally not wired up here.)

enum GbThemeId : uint8_t {
  kGbThemeOpsTeal = 0,
  kGbThemeDmgGreen,
  kGbThemePocketGrey,
  kGbThemeBerry,
  kGbThemeAmberCrt,
  kGbThemeCount
};

struct GbPalette {
  const char *name;
  uint16_t bg;          // page background
  uint16_t surface;     // card fill
  uint16_t surfaceHi;   // button / highlighted card fill
  uint16_t line;        // borders
  uint16_t ink;         // primary text
  uint16_t muted;       // secondary text
  uint16_t accent;      // primary accent
  uint16_t success;     // SD ready / sound on
  uint16_t warning;     // no SD / MENU
};

const GbPalette &gbTheme(GbThemeId id);
GbThemeId nextGbTheme(GbThemeId id);
GbThemeId prevGbTheme(GbThemeId id);
GbThemeId gbThemeFromName(const String &name);

#endif
