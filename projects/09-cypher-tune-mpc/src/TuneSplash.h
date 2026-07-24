#ifndef CYPHER_TUNE_TUNE_SPLASH_H
#define CYPHER_TUNE_TUNE_SPLASH_H

#include "../config/ProjectConfig.h"
#include <Arduino.h>
#include "TuneThemes.h"

// Animated boot splash: the wordmark fades up, a damped-sine waveform sweeps
// across behind it with a bright leading edge, then the pad grid wipes in row
// by row before the main UI takes over.
//
// Runs once from setup() after the display is up and before the first UI
// paint. Boot is the one moment nothing competes for the framebuffer, so it
// draws straight into the cached FB and syncs one region per frame - the same
// manual-flush path the main UI uses, no offscreen canvas needed.
//
// Compiles to a no-op without a display, so setup() needs no #ifdef.
namespace TuneSplash {

// `subtitle` is shown under the wordmark once the sweep completes (the engine
// status line, so the very first thing on screen is the truth about audio).
void run(const TuneTheme &theme, const char *subtitle);

}  // namespace TuneSplash

#endif
