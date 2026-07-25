#ifndef CYPHER_KEYS_KEYS_SPLASH_H
#define CYPHER_KEYS_KEYS_SPLASH_H

#include "../config/ProjectConfig.h"
#include "DeckThemes.h"
#include <Arduino.h>

// Animated boot splash: the wordmark fades up out of the background, then ten
// keycaps spelling CYPHERKEYS land left to right - each lit as it arrives and
// settling as the next one comes down - drawing an accent rule in behind them.
//
// Runs once from HidDeck::begin() after the display is up and before the first
// UI paint. Boot is the one moment nothing competes for the framebuffer, so it
// draws straight into the cached FB and syncs one region per frame - the same
// manual-flush path the deck itself uses, no offscreen canvas needed.
//
// Deliberately ~1.2 s end to end. This is a keyboard: anything longer is a
// wait, not an intro.
//
// Compiles to a no-op without a display, so begin() needs no #ifdef.
namespace KeysSplash {

// `subtitle` fades in with the wordmark (the HID backend's mode, so the very
// first thing on screen is the truth about where keystrokes will go).
void run(const DeckTheme &theme, const char *subtitle);

}  // namespace KeysSplash

#endif
