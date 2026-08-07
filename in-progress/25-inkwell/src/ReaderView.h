#ifndef INKWELL_READER_VIEW_H
#define INKWELL_READER_VIEW_H

#include "../config/ProjectConfig.h"
// Arduino.h must precede the target guard: CONFIG_IDF_TARGET_ESP32P4
// comes from sdkconfig.h via Arduino.h, so testing it before this
// include silently compiles the whole file out (same reason
// DisplayBringup.h includes <Arduino.h> first).
#include <Arduino.h>

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)

#include <Arduino_GFX_Library.h>

#include "InkDoc.h"
#include "Paginator.h"

// Draws one laid-out page onto the panel in the e-ink paper style. The
// page cadence is seconds, so every draw is a full-screen redraw -- the
// single-framebuffer tearing constraint that hurts animated projects is
// irrelevant here (see CLAUDE.md display invariants).
//
// NOT HARDWARE-VERIFIED: compiled for the target, never observed on glass.
namespace ReaderView {

struct FooterInfo {
  const char *title;      // truncated by the renderer as needed
  size_t chapter, chapterCount;
  size_t page, pageCount;
  uint16_t permille;
};

// Full page draw: paper background, styled text lines, quote bars,
// bullets/ordered numbers, rules, footer. blocks is the chapter's block
// list (used to recover list metadata for a line, same recovery the
// serial renderer uses). invertFlash: fill the screen with ink and pause
// ~90ms before the draw -- the signature e-ink refresh look, optional.
void drawPage(Arduino_GFX *gfx, const Ink::Paginator &p, size_t pageIdx,
              const std::vector<Ink::Block> &blocks,
              const Ink::LayoutSettings &settings, uint8_t fontStep,
              const FooterInfo &footer, bool invertFlash);

}  // namespace ReaderView

#endif  // USE_DISPLAY && CONFIG_IDF_TARGET_ESP32P4

#endif
