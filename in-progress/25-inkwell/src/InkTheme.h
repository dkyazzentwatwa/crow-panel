#ifndef INKWELL_INK_THEME_H
#define INKWELL_INK_THEME_H

#include <cstdint>

// E-ink paper palette. 24-bit 0xRRGGBB, converted to RGB565 at the call
// site (same convention as DisplayBringup's toColor565). The panel is an
// IPS LCD -- the "e-ink" here is an aesthetic: warm paper, near-black
// serif text, minimal chrome, full-page redraws only.
namespace InkTheme {

constexpr uint32_t kPaper = 0xEFE8D8;   // warm paper background
constexpr uint32_t kText = 0x1A1A14;    // near-black body text
constexpr uint32_t kFaint = 0x8A8474;   // footer, progress, quote bars
constexpr uint32_t kCard = 0xE4DCC8;    // library cards / HUD sheets

inline uint16_t to565(uint32_t rgb) {
  return (uint16_t)(((rgb >> 8) & 0xF800) | ((rgb >> 5) & 0x07E0) |
                    ((rgb >> 3) & 0x001F));
}

}  // namespace InkTheme

#endif
