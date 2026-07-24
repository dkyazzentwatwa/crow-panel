#include "GbTheme.h"

namespace {

// 0xRRGGBB -> RGB565, same helper shape Cypher Desk's DeskTheme uses.
constexpr uint16_t cv(uint32_t c) {
  return (uint16_t)((((c >> 16) & 0xf8) << 8) | (((c >> 8) & 0xfc) << 3) | ((c & 0xff) >> 3));
}

const GbPalette kThemes[kGbThemeCount] = {
    // Ops Teal - the suite's house style, and the default.
    {"Ops Teal", cv(0x0b111c), cv(0x16202f), cv(0x1e2b3d), cv(0x2a3a4f),
     cv(0xeaf0f7), cv(0x8296ac), cv(0x16c2c9), cv(0x35d07f), cv(0xf7b733)},

    // DMG Green - 1989 Game Boy olive/green chrome.
    {"DMG Green", cv(0x0f140c), cv(0x1b2415), cv(0x27331d), cv(0x3a4a2b),
     cv(0xd7e894), cv(0x8fa06a), cv(0x9bbc0f), cv(0x8bac0f), cv(0xd7b31f)},

    // Pocket Grey - Game Boy Pocket. Neutral greys.
    {"Pocket Grey", cv(0x101010), cv(0x1c1c1c), cv(0x2a2a2a), cv(0x3d3d3d),
     cv(0xe8e8e8), cv(0x9a9a9a), cv(0xc0c0c0), cv(0x8fbf8f), cv(0xd0b070)},

    // Berry - purple/pink, echoing Cypher Desk's Midnight Plum.
    {"Berry", cv(0x120f1b), cv(0x1f1830), cv(0x2c2242), cv(0x3f3157),
     cv(0xfff7fc), cv(0xb8abc4), cv(0xc4b5fd), cv(0x86efac), cv(0xf9a8d4)},

    // Amber CRT - warm phosphor terminal.
    {"Amber CRT", cv(0x140d05), cv(0x21160a), cv(0x2f2010), cv(0x46301a),
     cv(0xffd9a0), cv(0xb08a57), cv(0xffb020), cv(0xa8c050), cv(0xff8c1a)},
};

}  // namespace

const GbPalette &gbTheme(GbThemeId id) {
  return kThemes[id < kGbThemeCount ? id : kGbThemeOpsTeal];
}

GbThemeId nextGbTheme(GbThemeId id) {
  return (GbThemeId)(((uint8_t)id + 1) % kGbThemeCount);
}

GbThemeId prevGbTheme(GbThemeId id) {
  return (GbThemeId)(((uint8_t)id + kGbThemeCount - 1) % kGbThemeCount);
}

GbThemeId gbThemeFromName(const String &name) {
  String value = name;
  value.trim();
  value.toLowerCase();
  if (value.length() == 0) return kGbThemeOpsTeal;
  for (uint8_t i = 0; i < kGbThemeCount; i++) {
    String candidate = kThemes[i].name;
    candidate.toLowerCase();
    if (candidate == value || candidate.startsWith(value)) {
      return (GbThemeId)i;
    }
  }
  return kGbThemeOpsTeal;
}
