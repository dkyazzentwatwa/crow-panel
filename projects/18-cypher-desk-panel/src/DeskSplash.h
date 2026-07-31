#ifndef CYPHER_DESK_SPLASH_H
#define CYPHER_DESK_SPLASH_H

#include "../config/ProjectConfig.h"
#include "DeskTypes.h"
#include <Arduino.h>

// Boot animation.
//
// Replaces DeskApp::drawIntroSplash, which blocked for five seconds in a
// delay(80) loop AND never actually played in the OS build - the Writer is
// started with initializeDisplay=false, so the branch that called it was dead.
//
// Modelled on project 21's KeysSplash: the wordmark fades up through an RGB565
// blend, then the app tiles land left to right, each flushing only its own
// rectangle. About 1.2 s, and it runs once from CypherDeskOs::begin().
namespace DeskSplash {

// No-op in headless builds.
void run(const DeskThemePalette &theme, const char *subtitle);

}  // namespace DeskSplash

#endif
