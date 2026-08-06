#ifndef INKWELL_PROJECT_CONFIG_H
#define INKWELL_PROJECT_CONFIG_H

// Inkwell is mock-first. Real SD and display builds pass these flags through
// compiler.cpp.extra_flags so shared translation units see the same values
// (see CLAUDE.md's three-layer flag rule).
#ifndef USE_INKWELL_SD
#define USE_INKWELL_SD 0
#endif

// Portrait rotation for Arduino_DSI_Display: 1 = 90° CW (USB at bottom),
// 3 = 90° CCW. Hardware bring-up decides which; default 1 until proven.
#ifndef INKWELL_ROTATION
#define INKWELL_ROTATION 1
#endif

// Logical page geometry (portrait).
#ifndef INKWELL_PAGE_W
#define INKWELL_PAGE_W 600
#endif
#ifndef INKWELL_PAGE_H
#define INKWELL_PAGE_H 1024
#endif

#include <AppConfig.h>

#endif  // INKWELL_PROJECT_CONFIG_H
