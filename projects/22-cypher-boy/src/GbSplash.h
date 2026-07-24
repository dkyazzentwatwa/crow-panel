#ifndef CYPHER_BOY_SPLASH_H
#define CYPHER_BOY_SPLASH_H

#include "../config/ProjectConfig.h"
#include <Arduino.h>

class GbAudio;
class GbInput;

// Animated boot sequence: a cartridge glyph drops in from the top, settles with
// a chime, and the wordmark fades up beneath it.
//
// Borrows the *shape* of a handheld power-on (something descends, it lands, it
// sounds) but uses our own mark and our own two-note tone - no other company's
// logo or jingle is reproduced.
//
// Deliberately drawn with primitives rather than an offscreen buffer: a
// full-screen 1024x600 RGB565 canvas is 1.2 MB, far past internal SRAM, so each
// frame erases and redraws only the band the glyph moved through.
//
// Blocking, ~1.6 s, and skippable - a touch ends it immediately.
namespace GbSplash {
void run(GbAudio *audio, GbInput *input);

// Wipe the screen in the active theme's background. Used as a transition
// between screens as well as by the splash itself.
void wipe(bool leftToRight);
}  // namespace GbSplash

#endif
