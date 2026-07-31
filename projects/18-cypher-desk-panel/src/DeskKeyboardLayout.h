#ifndef CYPHER_DESK_KEYBOARD_LAYOUT_H
#define CYPHER_DESK_KEYBOARD_LAYOUT_H

#include "../config/ProjectConfig.h"
#include <Arduino.h>

// Keyboard geometry and hit-tests, in one header so drawing and touch can never
// disagree. Pure integer math, no display headers, so it compiles in every flag
// combination and under the host test harness.
//
// These numbers came from this project originally; project 21 forked them and
// then evolved the *behaviour* well past what is here. This is the geometry
// coming home with 21's templated form, which is what lets each project keep
// its own key table - all the math needs is a `.weight` member.
//
// 1024x600, keyboard occupying y 304..600:
//   y 304        band top (cleared and flushed as one unit)
//   y 308        grab-handle bar
//   y 316        row 0 (QWERTYUIOP), rows pitch 68 (60 tall + 8 gap)

namespace DeskKeyboardLayout {

constexpr int16_t kScreenW = 1024;
constexpr int16_t kScreenH = 600;

constexpr int16_t kKeyboardY = 316;
constexpr int16_t kKeyboardMarginX = 22;
constexpr int16_t kKeyboardWidth = 980;
constexpr int16_t kRowHeight = 60;
constexpr int16_t kRowGap = 8;
constexpr int16_t kKeyGap = 6;
constexpr uint8_t kRows = 4;

// The whole keyboard is cleared and flushed as one band starting 12px above
// row 0.
constexpr int16_t kBandY = kKeyboardY - 12;  // 304
constexpr int16_t kBandH = 296;              // 304..600

// Grab-handle bar drawn just above the first row.
constexpr int16_t kHandleX = 430;
constexpr int16_t kHandleY = kKeyboardY - 8;  // 308
constexpr int16_t kHandleW = 164;
constexpr int16_t kHandleH = 5;

inline bool inRect(int16_t x, int16_t y, int16_t rx, int16_t ry, int16_t rw, int16_t rh) {
  return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

// Row 1 (ASDF...) is inset so it sits centred under the row above it.
inline int16_t rowInset(uint8_t row) { return row == 1 ? 44 : 0; }
inline int16_t rowWidth(uint8_t row) { return kKeyboardWidth - rowInset(row) * 2; }

template <typename KeyT>
inline uint16_t totalWeight(const KeyT *keys, uint8_t count) {
  uint16_t total = 0;
  for (uint8_t i = 0; i < count; ++i) total += keys[i].weight;
  return total;
}

// Rect of one key in a weighted row. Widths are truncated per key and the x
// cursor advances by the truncated width - changing that would shift every key
// after the first by a pixel or two, and these positions are muscle memory.
template <typename KeyT>
inline bool keyBounds(const KeyT *keys, uint8_t count, uint8_t rowIndex, uint8_t keyIndex,
                      int16_t &x, int16_t &y, int16_t &w, int16_t &h) {
  if (keyIndex >= count) return false;
  const int16_t available = rowWidth(rowIndex) - (count - 1) * kKeyGap;
  const uint16_t weight = totalWeight(keys, count);
  x = kKeyboardMarginX + rowInset(rowIndex);
  for (uint8_t i = 0; i < keyIndex; ++i) {
    x += static_cast<int32_t>(available) * keys[i].weight / weight + kKeyGap;
  }
  w = static_cast<int32_t>(available) * keys[keyIndex].weight / weight;
  y = kKeyboardY + rowIndex * (kRowHeight + kRowGap);
  h = kRowHeight;
  return true;
}

// Touch target for a key cell: extended into half the gutter on every side, so
// there are no dead strips between keys while the visible breathing room stays.
inline bool hitKeyCell(int16_t px, int16_t py, int16_t x, int16_t y, int16_t w, int16_t h) {
  return inRect(px, py, x - kKeyGap / 2, y - kRowGap / 2, w + kKeyGap, h + kRowGap);
}

// Key ids pack row and index so a press can be matched to its release across
// polls. They are LAYER-RELATIVE: toggling 123/ABC invalidates every
// outstanding id, which the keyboard handles explicitly.
inline uint8_t makeKeyId(uint8_t row, uint8_t index) {
  return static_cast<uint8_t>(row * 16 + index);
}
inline uint8_t keyIdRow(uint8_t keyId) { return static_cast<uint8_t>(keyId / 16); }
inline uint8_t keyIdIndex(uint8_t keyId) { return static_cast<uint8_t>(keyId % 16); }
constexpr uint8_t kNoKey = 0xFF;

}  // namespace DeskKeyboardLayout

#endif
