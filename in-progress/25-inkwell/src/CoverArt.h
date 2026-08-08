#ifndef INKWELL_COVER_ART_H
#define INKWELL_COVER_ART_H

#include "../config/ProjectConfig.h"
// Arduino.h before the target guard (sdkconfig macro -- see GfxMeasure.h).
#include <Arduino.h>

#include "InkBook.h"

// Global-scope forward declaration: naming it first inside the namespace
// below would introduce CoverArt::Arduino_GFX instead.
class Arduino_GFX;

// EPUB cover thumbnails: decoded once (JPEGDEC / PNGdec), letterboxed into
// a fixed 220x300 RGB565 canvas, cached as /books/.inkwell/<id>.thumb so
// the library grid never decodes at draw time.
//
// Deliberate deviation from the plan's grid-time thumbFor(): thumbs are
// generated when a book is OPENED, while its bytes are already resident in
// the single-slot PSRAM buffer -- generating at grid time would evict the
// open book's buffer (LibraryStore's documented single-slot policy). The
// visible consequence, documented in README/TECHNICAL: a cover appears in
// the grid only after the book has been opened once.
//
// Needs display + SD + the P4 target; otherwise inline no-op stubs keep
// every call site unconditional. NOT HARDWARE-VERIFIED.
namespace CoverArt {

constexpr int16_t kThumbW = 220, kThumbH = 300;

#if USE_DISPLAY && USE_INKWELL_SD && defined(CONFIG_IDF_TARGET_ESP32P4)

// Blits the cached thumb at (x, y). False when no cache exists (caller
// draws the text placeholder card instead). Never decodes.
bool drawCachedThumb(Arduino_GFX *g, const String &bookId, int16_t x,
                     int16_t y);

// Extracts + decodes + caches the cover of an open EPUB. No-op when the
// cache already exists, the book has no cover, or decode fails (the grid
// keeps its placeholder -- a bad cover never blocks anything).
void generateThumb(const String &bookId, Ink::InkBook &book);

#else

inline bool drawCachedThumb(Arduino_GFX *, const String &, int16_t,
                            int16_t) {
  return false;
}
inline void generateThumb(const String &, Ink::InkBook &) {}

#endif

}  // namespace CoverArt

#endif
