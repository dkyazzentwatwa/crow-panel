#include "DeskTheme.h"

namespace {

DeskThemePalette makePalette(const char *name, uint32_t bg, uint32_t shell,
                             uint32_t panel, uint32_t panelHi, uint32_t ink,
                             uint32_t muted, uint32_t accent, uint32_t accent2,
                             uint32_t accent3, uint32_t success, uint32_t warning,
                             uint32_t line, uint32_t key0, uint32_t key1,
                             uint32_t key2, uint32_t key3) {
  DeskThemePalette p = {};
  p.name = name;
  auto cv = [](uint32_t c) {
    uint8_t r = (c >> 16) & 0xff;
    uint8_t g = (c >> 8) & 0xff;
    uint8_t b = c & 0xff;
    return static_cast<uint16_t>(((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3));
  };
  p.background = cv(bg);
  p.shell = cv(shell);
  p.panel = cv(panel);
  p.panelHighlight = cv(panelHi);
  p.ink = cv(ink);
  p.muted = cv(muted);
  p.accent = cv(accent);
  p.accent2 = cv(accent2);
  p.accent3 = cv(accent3);
  p.success = cv(success);
  p.warning = cv(warning);
  p.line = cv(line);
  p.keyboardRows[0] = cv(key0);
  p.keyboardRows[1] = cv(key1);
  p.keyboardRows[2] = cv(key2);
  p.keyboardRows[3] = cv(key3);
  return p;
}

const DeskThemePalette kThemes[kDeskThemeCount] = {
    makePalette("Midnight Plum", 0x120f1b, 0x1a1525, 0x241d31, 0x312641,
                0xfff7fc, 0xb8abc4, 0xc4b5fd, 0xf9a8d4, 0xa5d8ff,
                0xa7f3d0, 0xfde68a, 0x493a5d, 0x2b233b, 0x222c3a,
                0x2d2435, 0x223032),
    makePalette("Matcha Terminal", 0x0d1713, 0x13221b, 0x1b2d24, 0x243c30,
                0xf0f7ee, 0x9bb3a5, 0xb7e4c7, 0xd8f3dc, 0xcdeac0,
                0x95d5b2, 0xffd6a5, 0x365546, 0x203b2e, 0x1c352b,
                0x294234, 0x24382e),
    makePalette("Dusty Rose", 0x1b1218, 0x271a22, 0x35232d, 0x432b38,
                0xfff7f8, 0xc6aab4, 0xf4acb7, 0xffcad4, 0xd8b4e2,
                0xb8e0d2, 0xffd6a5, 0x60404e, 0x402833, 0x362b3b,
                0x432936, 0x30383a),
    makePalette("Rainy Blue", 0x0d151d, 0x14212d, 0x1b2b39, 0x243848,
                0xf3f8ff, 0x9eb3c5, 0x90caf9, 0xb4d7ff, 0xc4b5fd,
                0xa8dadc, 0xffd166, 0x36536a, 0x1f3547, 0x213242,
                0x2b354c, 0x203d43),
    makePalette("Paperback", 0x241f1a, 0x302921, 0x3b3228, 0x4a3d30,
                0xfff4dc, 0xc8b99c, 0xe9c46a, 0xf4a261, 0xd4a373,
                0xb7c4a5, 0xe76f51, 0x65543f, 0x463a2e, 0x40352a,
                0x4b382d, 0x394039)};

}  // namespace

const DeskThemePalette &deskTheme(DeskThemeId id) {
  if (id < 0 || id >= kDeskThemeCount) id = kDeskThemeMidnightPlum;
  return kThemes[id];
}

DeskThemeId nextDeskTheme(DeskThemeId id) {
  return static_cast<DeskThemeId>((static_cast<uint8_t>(id) + 1) % kDeskThemeCount);
}

DeskThemeId deskThemeFromName(const String &name) {
  String value = name;
  value.toLowerCase();
  for (uint8_t i = 0; i < kDeskThemeCount; ++i) {
    String candidate = kThemes[i].name;
    candidate.toLowerCase();
    if (candidate == value || candidate.startsWith(value)) {
      return static_cast<DeskThemeId>(i);
    }
  }
  return kDeskThemeMidnightPlum;
}
