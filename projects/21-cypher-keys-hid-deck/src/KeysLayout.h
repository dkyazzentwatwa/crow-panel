#ifndef CYPHER_KEYS_LAYOUT_H
#define CYPHER_KEYS_LAYOUT_H

#include "../config/ProjectConfig.h"
#include <Arduino.h>

// Every pixel coordinate of the Cypher Keys deck plus the matching hit-tests,
// in one header so drawing and touch can never disagree. Pure integer math, no
// display headers - compiles in every flag combination (headless included).
//
// 1024x600 landscape. DECK view:
//   y   0..40   status bar: title | MOCK/USB/BLE pill | info | last action |
//               OUT / DICTATE / SET / MODE buttons on the right
//   y  40..304  macro band: preset tab bar (y 42) + 4x3 macro grid (y 88)
//   y 304..600  weighted 4-row touch keyboard (first row at y 316)
// TRACKPAD view replaces everything below the status bar:
//   move surface + right-edge scroll strip + LEFT/RIGHT click buttons.
// SETTINGS view replaces the whole screen: a BACK header plus five rows
//   (sound / volume / brightness / theme / idle dim).
namespace KeysLayout {

constexpr int16_t kScreenW = 1024;
constexpr int16_t kScreenH = 600;

inline bool inRect(int16_t x, int16_t y, int16_t rx, int16_t ry, int16_t rw,
                   int16_t rh) {
  return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

// --- Status bar --------------------------------------------------------------
constexpr int16_t kStatusY = 0;
constexpr int16_t kStatusH = 40;  // the divider hline sits on y = kStatusH
constexpr int16_t kTitleX = 16, kTitleY = 12;
constexpr int16_t kPillX = 168, kPillY = 8;  // MOCK / USB / BLE pill
constexpr int16_t kInfoX = 236, kInfoY = 14;  // "preset - theme"
constexpr int16_t kLastActionX = 400, kLastActionY = 14;

// Right-hand button cluster. The drawn panels ARE the hit bands - one set of
// numbers, so a label can never drift out from under its touch target.
//
// The bar was full at four slots, and every new knob (sound, volume,
// brightness, idle dim) would have wanted a fifth. So the third slot stopped
// being a one-shot THEME cycle and became SET, which opens the settings view;
// theme is a row in there now. The x bands are untouched, so OUT / DICTATE /
// MODE stay exactly where the muscle memory expects them.
constexpr int16_t kBtnY = 4, kBtnH = 32;  // hit/draw band, y 4..36
constexpr int16_t kOutBtnX = 548, kOutBtnW = 106;     // 548..654
constexpr int16_t kDictBtnX = 660, kDictBtnW = 106;   // 660..766
constexpr int16_t kSetBtnX = 772, kSetBtnW = 106;     // 772..878
constexpr int16_t kModeBtnX = 884, kModeBtnW = 118;   // 884..1002
constexpr int16_t kBtnLabelY = 12;
constexpr int16_t kOutLabelX = kOutBtnX + kOutBtnW / 2;      // 601
constexpr int16_t kDictLabelX = kDictBtnX + kDictBtnW / 2;   // 713
constexpr int16_t kSetBtnLabelX = kSetBtnX + kSetBtnW / 2;   // 825
constexpr int16_t kModeLabelX = kModeBtnX + kModeBtnW / 2;   // 943
// BLE link dot, tucked inside the OUT button's right edge.
constexpr int16_t kOutDotX = 645, kOutDotY = 12, kOutDotR = 4;

inline bool hitOutputButton(int16_t x, int16_t y) {
  return inRect(x, y, kOutBtnX, kBtnY, kOutBtnW, kBtnH);
}
inline bool hitDictButton(int16_t x, int16_t y) {
  return inRect(x, y, kDictBtnX, kBtnY, kDictBtnW, kBtnH);
}
inline bool hitSetButton(int16_t x, int16_t y) {
  return inRect(x, y, kSetBtnX, kBtnY, kSetBtnW, kBtnH);
}
inline bool hitModeButton(int16_t x, int16_t y) {
  return inRect(x, y, kModeBtnX, kBtnY, kModeBtnW, kBtnH);
}

// --- Macro band: preset tabs + macro grid ------------------------------------
// The band is cleared and flushed as one region (status bar bottom -> keyboard
// top), so its y/height are shared by MacroPresets' draw and HidDeck's flush.
constexpr int16_t kMacroBandY = kStatusH;  // 40
constexpr int16_t kMacroBandH = 264;       // 40..304

constexpr int16_t kMarginX = 22;
constexpr int16_t kTabTop = 42;
constexpr int16_t kTabH = 36;
constexpr int16_t kTabGap = 8;
constexpr int16_t kBandW = 980;  // 22..1002

constexpr int16_t kGridTop = 88;
constexpr int16_t kGridH = 212;
constexpr int16_t kSlotGap = 10;
constexpr int16_t kCols = CYPHER_KEYS_MACRO_COLS;
constexpr int16_t kRows = CYPHER_KEYS_MACRO_SLOTS / CYPHER_KEYS_MACRO_COLS;
constexpr uint8_t kSlots = CYPHER_KEYS_MACRO_SLOTS;

// Tabs share the band width evenly; the count is the live preset count.
inline void tabBounds(uint8_t index, uint8_t count, int16_t &x, int16_t &y,
                      int16_t &w, int16_t &h) {
  if (count == 0) count = 1;
  int16_t available = kBandW - (count - 1) * kTabGap;
  w = available / count;
  x = kMarginX + index * (w + kTabGap);
  y = kTabTop;
  h = kTabH;
}

inline void slotBounds(uint8_t index, int16_t &x, int16_t &y, int16_t &w,
                       int16_t &h) {
  int16_t col = index % kCols;
  int16_t row = index / kCols;
  w = (kBandW - (kCols - 1) * kSlotGap) / kCols;
  h = (kGridH - (kRows - 1) * kSlotGap) / kRows;
  x = kMarginX + col * (w + kSlotGap);
  y = kGridTop + row * (h + kSlotGap);
}

// -1 or tab index 0..count-1.
inline int8_t hitTab(int16_t x, int16_t y, uint8_t count) {
  for (uint8_t i = 0; i < count; ++i) {
    int16_t bx, by, bw, bh;
    tabBounds(i, count, bx, by, bw, bh);
    if (inRect(x, y, bx, by, bw, bh)) return (int8_t)i;
  }
  return -1;
}

// -1 or slot index 0..kSlots-1.
inline int8_t hitSlot(int16_t x, int16_t y) {
  for (uint8_t i = 0; i < kSlots; ++i) {
    int16_t bx, by, bw, bh;
    slotBounds(i, bx, by, bw, bh);
    if (inRect(x, y, bx, by, bw, bh)) return (int8_t)i;
  }
  return -1;
}

// --- Touch keyboard ----------------------------------------------------------
// Geometry copied verbatim from Cypher Desk's DeskTouchKeyboard so the panel's
// bottom half feels identical. The key TABLES (labels/roles/weights) live in
// HidKeyboard.cpp; only the rectangles are computed here.
constexpr int16_t kKeyboardY = 316;
constexpr int16_t kKeyboardMarginX = 22;
constexpr int16_t kKeyboardWidth = 980;
constexpr int16_t kRowHeight = 60;
constexpr int16_t kRowGap = 8;
constexpr int16_t kKeyGap = 6;

// The whole keyboard is cleared/flushed as one band starting 12px above row 0.
constexpr int16_t kKeyboardBandY = kKeyboardY - 12;  // 304
constexpr int16_t kKeyboardBandH = 296;              // 304..600
// Grab-handle bar drawn just above the first row.
constexpr int16_t kKeyboardHandleX = 430;
constexpr int16_t kKeyboardHandleY = kKeyboardY - 8;  // 308
constexpr int16_t kKeyboardHandleW = 164;
constexpr int16_t kKeyboardHandleH = 5;

// Row 1 (ASDF...) is inset so it sits centered under the row above it.
inline int16_t rowInset(uint8_t row) { return row == 1 ? 44 : 0; }
inline int16_t rowWidth(uint8_t row) { return kKeyboardWidth - rowInset(row) * 2; }

template <typename KeyT>
inline uint16_t totalWeight(const KeyT *keys, uint8_t count) {
  uint16_t total = 0;
  for (uint8_t i = 0; i < count; ++i) total += keys[i].weight;
  return total;
}

// Rect of one key in a weighted row. Templated on the row's element type so the
// key table can keep living next to its labels/roles - all this needs is a
// `.weight` member. Widths are truncated per key and the x cursor advances by
// the truncated width, exactly as before: changing that would shift every key
// after the first by a pixel or two.
template <typename KeyT>
inline bool keyBounds(const KeyT *keys, uint8_t count, uint8_t rowIndex,
                      uint8_t keyIndex, int16_t &x, int16_t &y, int16_t &w,
                      int16_t &h) {
  if (keyIndex >= count) return false;
  int16_t available = rowWidth(rowIndex) - (count - 1) * kKeyGap;
  uint16_t weight = totalWeight(keys, count);
  x = kKeyboardMarginX + rowInset(rowIndex);
  for (uint8_t i = 0; i < keyIndex; ++i) {
    x += (int32_t)available * keys[i].weight / weight + kKeyGap;
  }
  w = (int32_t)available * keys[keyIndex].weight / weight;
  y = kKeyboardY + rowIndex * (kRowHeight + kRowGap);
  h = kRowHeight;
  return true;
}

// Touch target for a key cell: extended into half the gutter on every side
// (matches Desk) so there are no dead strips between keys.
inline bool hitKeyCell(int16_t px, int16_t py, int16_t x, int16_t y, int16_t w,
                       int16_t h) {
  return inRect(px, py, x - kKeyGap / 2, y - kRowGap / 2, w + kKeyGap,
                h + kRowGap);
}

// --- Trackpad view -----------------------------------------------------------
// Everything below the status bar is repainted when this view is up.
constexpr int16_t kTrackpadBgY = kStatusH;              // 40
constexpr int16_t kTrackpadBgH = kScreenH - kStatusH;   // 560

constexpr int16_t kPadX = 22, kPadY = 92, kPadW = 736, kPadH = 378;
constexpr int16_t kScrollX = 780, kScrollY = 92, kScrollW = 222, kScrollH = 378;
constexpr int16_t kClickBtnY = 486, kClickBtnH = 78;
constexpr int16_t kLBtnX = 22, kLBtnW = 478;
constexpr int16_t kRBtnX = 524, kRBtnW = 478;

// A tap (vs a drag): small travel, released quickly.
constexpr int16_t kTapSlop = 12;
constexpr uint32_t kTapMs = 300;
// Pixels of vertical travel in the scroll strip per one wheel detent.
constexpr int16_t kScrollStep = 18;

inline bool hitPadSurface(int16_t x, int16_t y) {
  return inRect(x, y, kPadX, kPadY, kPadW, kPadH);
}
inline bool hitScrollStrip(int16_t x, int16_t y) {
  return inRect(x, y, kScrollX, kScrollY, kScrollW, kScrollH);
}
inline bool hitLeftButton(int16_t x, int16_t y) {
  return inRect(x, y, kLBtnX, kClickBtnY, kLBtnW, kClickBtnH);
}
inline bool hitRightButton(int16_t x, int16_t y) {
  return inRect(x, y, kRBtnX, kClickBtnY, kRBtnW, kClickBtnH);
}

// "TRACKPAD: USB ONLY" overlay (drawn by HidDeck while output is BLE, because
// mouse-over-BLE is disabled).
constexpr int16_t kUsbOnlyX = 262, kUsbOnlyY = 250;
constexpr int16_t kUsbOnlyW = 500, kUsbOnlyH = 96;
constexpr int16_t kUsbOnlyLabelX = kUsbOnlyX + kUsbOnlyW / 2;  // 512
constexpr int16_t kUsbOnlyTitleY = 276;
constexpr int16_t kUsbOnlyHintY = 306;

// --- Settings view (full-screen) ---------------------------------------------
// A BACK header, then one row per setting: label on the left, [-]/[<] and
// [+]/[>] steppers, a bar or a name panel between them, and the value
// right-aligned at the screen edge. Rows are a fixed pitch so a single setting
// can be repainted and flushed as one band (see kSetRowH / setRowY).
constexpr int16_t kSetHeaderH = 60;
constexpr int16_t kSetBackX = 24, kSetBackW = 110;
constexpr int16_t kSetBackY = 14, kSetBackH = 36;
constexpr int16_t kSetTitleX = kScreenW / 2, kSetTitleY = 20;
constexpr int16_t kSetRow0Y = 92;
constexpr int16_t kSetRowPitch = 76;
constexpr int16_t kSetRowH = 52;
constexpr int16_t kSetLabelX = 40;
constexpr int16_t kSetMinusX = 250, kSetStepW = 76;
constexpr int16_t kSetBarX = 344, kSetBarW = 430;  // 344..774
constexpr int16_t kSetPlusX = 792;
constexpr int16_t kSetValueX = 1000;  // right-aligned
// Row indices double as the bit positions of HidDeck's dirty-row mask.
constexpr uint8_t kSetRowSound = 0;
constexpr uint8_t kSetRowVolume = 1;
constexpr uint8_t kSetRowBrightness = 2;
constexpr uint8_t kSetRowTheme = 3;
constexpr uint8_t kSetRowIdleDim = 4;
constexpr uint8_t kSetRowCount = 5;
// Read-only footer block under the rows (sound engine / heap / output).
constexpr int16_t kSetInfoY = 480;

inline int16_t setRowY(uint8_t row) { return kSetRow0Y + row * kSetRowPitch; }

// --- Control ids (settings view) ---------------------------------------------
// The deck and trackpad views keep their dedicated hit* predicates; only the
// settings screen has enough controls to be worth an id enum.
enum Control : int16_t {
  kControlNone = -1,
  kControlBack = 0,
  kControlSoundPrev,
  kControlSoundNext,
  kControlVolumeMinus,
  kControlVolumePlus,
  kControlBrightMinus,
  kControlBrightPlus,
  kControlThemePrev,
  kControlThemeNext,
  kControlIdleDim,
};

// Settings view. Separate from the deck/trackpad hit-tests because the screens
// share no controls - dispatching by view keeps a stale key or macro rect from
// ever firing while settings is up.
inline int16_t hitTestSettings(int16_t x, int16_t y) {
  if (inRect(x, y, kSetBackX, kSetBackY, kSetBackW, kSetBackH)) {
    return kControlBack;
  }
  for (uint8_t row = 0; row < kSetRowCount; ++row) {
    int16_t ry = setRowY(row);
    if (y < ry || y >= ry + kSetRowH) continue;
    bool minus = inRect(x, y, kSetMinusX, ry, kSetStepW, kSetRowH);
    bool plus = inRect(x, y, kSetPlusX, ry, kSetStepW, kSetRowH);
    switch (row) {
      case kSetRowSound:
        if (minus) return kControlSoundPrev;
        if (plus) return kControlSoundNext;
        break;
      case kSetRowVolume:
        if (minus) return kControlVolumeMinus;
        if (plus) return kControlVolumePlus;
        break;
      case kSetRowBrightness:
        if (minus) return kControlBrightMinus;
        if (plus) return kControlBrightPlus;
        break;
      case kSetRowTheme:
        if (minus) return kControlThemePrev;
        if (plus) return kControlThemeNext;
        break;
      case kSetRowIdleDim:
        // The whole row toggles: a two-state control needs no stepper.
        if (minus || plus || inRect(x, y, kSetBarX, ry, kSetBarW, kSetRowH)) {
          return kControlIdleDim;
        }
        break;
      default:
        break;
    }
    return kControlNone;  // inside a row, but not on one of its controls
  }
  return kControlNone;
}

}  // namespace KeysLayout

#endif
