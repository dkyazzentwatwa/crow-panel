#include "DeskTheme.h"

namespace {

uint16_t rgb565(uint32_t color) {
  const uint8_t r = (color >> 16) & 0xff;
  const uint8_t g = (color >> 8) & 0xff;
  const uint8_t b = color & 0xff;
  return static_cast<uint16_t>(((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3));
}

DeskThemePalette makePalette(const char *name, uint32_t bg, uint32_t shell, uint32_t panel,
                             uint32_t panelHi, uint32_t ink, uint32_t muted, uint32_t accent,
                             uint32_t accent2, uint32_t accent3, uint32_t success,
                             uint32_t warning, uint32_t line, uint32_t onAccent, uint32_t key0,
                             uint32_t key1, uint32_t key2, uint32_t key3, bool light) {
  DeskThemePalette p = {};
  p.name = name;
  p.background = rgb565(bg);
  p.shell = rgb565(shell);
  p.panel = rgb565(panel);
  p.panelHighlight = rgb565(panelHi);
  p.ink = rgb565(ink);
  p.muted = rgb565(muted);
  p.accent = rgb565(accent);
  p.accent2 = rgb565(accent2);
  p.accent3 = rgb565(accent3);
  p.success = rgb565(success);
  p.warning = rgb565(warning);
  p.line = rgb565(line);
  p.onAccent = rgb565(onAccent);
  p.keyboardRows[0] = rgb565(key0);
  p.keyboardRows[1] = rgb565(key1);
  p.keyboardRows[2] = rgb565(key2);
  p.keyboardRows[3] = rgb565(key3);
  p.light = light;
  return p;
}

// Order must match DeskThemeId. The first five are this project's original
// writing palettes, unchanged apart from gaining an onAccent; the last six are
// ported from project 21's DeckThemes so the two decks look like one product.
const DeskThemePalette kThemes[kDeskThemeCount] = {
    makePalette("Midnight Plum", 0x120f1b, 0x1a1525, 0x241d31, 0x312641, 0xfff7fc, 0xb8abc4,
                0xc4b5fd, 0xf9a8d4, 0xa5d8ff, 0xa7f3d0, 0xfde68a, 0x493a5d, 0x120f1b, 0x2b233b,
                0x222c3a, 0x2d2435, 0x223032, false),
    makePalette("Matcha Terminal", 0x0d1713, 0x13221b, 0x1b2d24, 0x243c30, 0xf0f7ee, 0x9bb3a5,
                0xb7e4c7, 0xd8f3dc, 0xcdeac0, 0x95d5b2, 0xffd6a5, 0x365546, 0x0d1713, 0x203b2e,
                0x1c352b, 0x294234, 0x24382e, false),
    makePalette("Dusty Rose", 0x1b1218, 0x271a22, 0x35232d, 0x432b38, 0xfff7f8, 0xc6aab4,
                0xf4acb7, 0xffcad4, 0xd8b4e2, 0xb8e0d2, 0xffd6a5, 0x60404e, 0x1b1218, 0x402833,
                0x362b3b, 0x432936, 0x30383a, false),
    makePalette("Rainy Blue", 0x0d151d, 0x14212d, 0x1b2b39, 0x243848, 0xf3f8ff, 0x9eb3c5,
                0x90caf9, 0xb4d7ff, 0xc4b5fd, 0xa8dadc, 0xffd166, 0x36536a, 0x0d151d, 0x1f3547,
                0x213242, 0x2b354c, 0x203d43, false),
    makePalette("Paperback", 0x241f1a, 0x302921, 0x3b3228, 0x4a3d30, 0xfff4dc, 0xc8b99c,
                0xe9c46a, 0xf4a261, 0xd4a373, 0xb7c4a5, 0xe76f51, 0x65543f, 0x241f1a, 0x463a2e,
                0x40352a, 0x4b382d, 0x394039, false),

    // --- Ported from project 21 ---------------------------------------------
    makePalette("Ops Teal", 0x0b111c, 0x16202f, 0x1e2b3d, 0x2a3a4f, 0xeaf0f7, 0x8296ac, 0x16c2c9,
                0x16c2c9, 0x35d07f, 0x35d07f, 0xf7b733, 0x2a3a4f, 0x0b111c, 0x1b2738, 0x182434,
                0x1b2738, 0x182434, false),
    makePalette("Amber CRT", 0x0a0700, 0x1a1200, 0x2a1e00, 0x4a3608, 0xffc24d, 0xa9791f, 0xff9f1a,
                0xffc24d, 0xff6a00, 0xffd24d, 0xff6a00, 0x4a3608, 0x0a0700, 0x1a1200, 0x140e00,
                0x1a1200, 0x140e00, false),
    makePalette("Synthwave", 0x160d22, 0x241634, 0x331e4a, 0x452c63, 0xf5e6ff, 0xa98ac9, 0xff3cac,
                0x7a5cff, 0x3ce0c0, 0x3ce0c0, 0xff8a3c, 0x452c63, 0x160d22, 0x241634, 0x1d1230,
                0x241634, 0x1d1230, false),
    makePalette("Matrix", 0x020a03, 0x06180a, 0x0a240e, 0x124a18, 0x7cff88, 0x35a047, 0x35d07f,
                0x7cff88, 0xc7ff33, 0x35d07f, 0xc7ff33, 0x124a18, 0x020a03, 0x06180a, 0x041207,
                0x06180a, 0x041207, false),
    // Light: dark ink on soft light surfaces. onAccent is what keeps a
    // highlighted key or pill readable here.
    makePalette("Pastel", 0xf3eff7, 0xffffff, 0xede6f6, 0xd8cee8, 0x4a3f5c, 0x8b7fa3, 0x7ec8b0,
                0xa78bd0, 0xf2b5a0, 0x8fc99a, 0xf2b5a0, 0xd8cee8, 0x2c2438, 0xffffff, 0xf1ebfa,
                0xffffff, 0xf1ebfa, true),
    makePalette("Cotton Candy", 0xfbeff6, 0xfff5fb, 0xfce6f2, 0xf3cbe0, 0x5a4657, 0xb08aa0,
                0xff8fbe, 0x8fc6ff, 0x8fe0be, 0x8fe0be, 0xffbe8f, 0xf3cbe0, 0x4a2e3e, 0xfff5fb,
                0xfbeaf3, 0xfff5fb, 0xfbeaf3, true)};

}  // namespace

const DeskThemePalette &deskTheme(DeskThemeId id) {
  if (id < 0 || id >= kDeskThemeCount) id = kDeskThemeMidnightPlum;
  return kThemes[id];
}

DeskThemeId nextDeskTheme(DeskThemeId id) {
  return static_cast<DeskThemeId>((static_cast<uint8_t>(id) + 1) % kDeskThemeCount);
}

const char *deskThemeName(DeskThemeId id) { return deskTheme(id).name; }

DeskThemeId deskThemeFromName(const String &name) {
  String value = name;
  value.trim();
  value.toLowerCase();
  if (!value.length()) return kDeskThemeMidnightPlum;
  for (uint8_t i = 0; i < kDeskThemeCount; ++i) {
    String candidate = kThemes[i].name;
    candidate.toLowerCase();
    if (candidate == value || candidate.startsWith(value)) {
      return static_cast<DeskThemeId>(i);
    }
  }
  return kDeskThemeMidnightPlum;
}
