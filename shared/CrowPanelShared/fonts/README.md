# Vendored fonts

Adafruit GFX FreeFonts (`FreeSans*`, `FreeSansBold*`), copied from the
Adafruit_GFX library's `Fonts/` directory (BSD license). The only change is
removing the `#include <Adafruit_GFX.h>` line from each header: these files
only define glyph arrays and a `GFXfont`, and this repo draws with
Arduino_GFX, whose `gfxfont.h` provides a byte-identical `GFXfont`/`GFXglyph`
layout. Pulling in Adafruit_GFX would double-define those structs.

Included via relative path from `DashboardWidgets.cpp` (single translation
unit, so the `const GFXfont ... PROGMEM` definitions raise no ODR issue).
Used only when a sketch is built with `USE_DISPLAY=1`.
