#ifndef CYPHER_TUNE_UI_LAYOUT_H
#define CYPHER_TUNE_UI_LAYOUT_H

#include "../config/ProjectConfig.h"
#include <Arduino.h>

// Every pixel coordinate of the MPC screen plus the matching hit-tests, in
// one header so drawing and touch can never disagree. Pure integer math -
// compiles in every flag combination.
//
// 1024x600 landscape:
//   y 0-64    transport bar (PLAY STOP REC | BPM | SWING | MET | PTN A-D)
//   x 0-544   4x4 pad grid (126px cells, pad 1 bottom-left, MPC order)
//   x 552+    step grid 2x8, pad edit panel, voice meters, status strip
namespace UiLayout {

constexpr int16_t kScreenW = 1024;
constexpr int16_t kScreenH = 600;

// --- Transport bar ---
constexpr int16_t kTransportH = 64;
constexpr int16_t kBtnY = 8;
constexpr int16_t kBtnH = 48;
constexpr int16_t kPlayX = 8,    kPlayW = 100;
constexpr int16_t kStopX = 116,  kStopW = 100;
constexpr int16_t kRecX = 224,   kRecW = 100;
constexpr int16_t kBpmMinusX = 340, kBpmMinusW = 52;
constexpr int16_t kBpmValX = 396,   kBpmValW = 92;
constexpr int16_t kBpmPlusX = 492,  kBpmPlusW = 52;
constexpr int16_t kSwingMinusX = 560, kSwingMinusW = 44;
constexpr int16_t kSwingValX = 608,   kSwingValW = 76;
constexpr int16_t kSwingPlusX = 688,  kSwingPlusW = 44;
constexpr int16_t kMetroX = 748, kMetroW = 64;
constexpr int16_t kPatternX = 828, kPatternW = 44, kPatternPitch = 48;  // A-D chips

// --- Pad grid (left) ---
constexpr int16_t kPadX0 = 8;
constexpr int16_t kPadY0 = 72;
constexpr int16_t kPadCell = 126;
constexpr int16_t kPadGap = 8;
constexpr int16_t kPadPitch = kPadCell + kPadGap;  // 134
constexpr int16_t kPadGridRight = kPadX0 + 4 * kPadPitch - kPadGap;  // 536

// --- Right column ---
constexpr int16_t kRightX = 552;
constexpr int16_t kRightW = 464;  // 552..1016

constexpr int16_t kSeqHeaderY = 64;
constexpr int16_t kSeqHeaderH = 36;

constexpr int16_t kStepX0 = 552;
constexpr int16_t kStepY0 = 100;
constexpr int16_t kStepCell = 56;
constexpr int16_t kStepGap = 4;
constexpr int16_t kStepPitch = kStepCell + kStepGap;  // 60

constexpr int16_t kEditY = 222;
constexpr int16_t kEditH = 230;  // ..452
constexpr int16_t kEditLabelX = 560;
constexpr int16_t kSliderX = 620;
constexpr int16_t kSliderW = 370;  // ..990
constexpr int16_t kSliderH = 28;
constexpr int16_t kVolSliderY = 264;
constexpr int16_t kPitchSliderY = 308;
constexpr int16_t kChokeY = 352;
constexpr int16_t kChokeH = 40;
constexpr int16_t kChokeX0 = 620, kChokeW = 56, kChokePitch = 64;  // OFF 1 2 3 4
constexpr int16_t kKitY = 404;
constexpr int16_t kKitH = 40;
constexpr int16_t kKitPrevX = 620, kKitArrowW = 48;
constexpr int16_t kKitNextX = 944;

constexpr int16_t kVoicesY = 452;
constexpr int16_t kVoicesH = 108;  // ..560

constexpr int16_t kStatusY = 560;
constexpr int16_t kStatusH = 40;

// --- Control ids (touch owner binding) ---
enum Control : int16_t {
  kControlNone = -1,
  kControlPlay = 0,
  kControlStop,
  kControlRec,
  kControlBpmMinus,
  kControlBpmPlus,
  kControlSwingMinus,
  kControlSwingPlus,
  kControlMetro,
  kControlPattern0,  // +0..3 = A..D
  kControlPattern1,
  kControlPattern2,
  kControlPattern3,
  kControlVolSlider,
  kControlPitchSlider,
  kControlChoke0,  // +0..4 = OFF,1..4
  kControlChoke1,
  kControlChoke2,
  kControlChoke3,
  kControlChoke4,
  kControlKitPrev,
  kControlKitNext,
  kControlPadBase = 100,   // +0..15
  kControlStepBase = 200,  // +0..15
};

inline bool inRect(int16_t x, int16_t y, int16_t rx, int16_t ry, int16_t rw, int16_t rh) {
  return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

inline int16_t padX(uint8_t col) { return kPadX0 + col * kPadPitch; }
inline int16_t padY(uint8_t row) { return kPadY0 + row * kPadPitch; }
// MPC order: pad 1 is bottom-left, pad 13 top-left.
inline uint8_t padIndexAt(uint8_t row, uint8_t col) { return (3 - row) * 4 + col; }
inline uint8_t padRow(uint8_t padIdx) { return 3 - padIdx / 4; }
inline uint8_t padCol(uint8_t padIdx) { return padIdx % 4; }

inline int16_t stepX(uint8_t step) { return kStepX0 + (step % 8) * kStepPitch; }
inline int16_t stepY(uint8_t step) { return kStepY0 + (step / 8) * kStepPitch; }

// -1 or pad index 0..15.
inline int8_t hitPad(int16_t x, int16_t y) {
  if (x < kPadX0 || y < kPadY0) {
    return -1;
  }
  int16_t col = (x - kPadX0) / kPadPitch;
  int16_t row = (y - kPadY0) / kPadPitch;
  if (col > 3 || row > 3) {
    return -1;
  }
  // Miss the gaps between cells.
  if ((x - kPadX0) % kPadPitch >= kPadCell || (y - kPadY0) % kPadPitch >= kPadCell) {
    return -1;
  }
  return (int8_t)padIndexAt((uint8_t)row, (uint8_t)col);
}

// -1 or step index 0..15.
inline int8_t hitStep(int16_t x, int16_t y) {
  if (x < kStepX0 || y < kStepY0 || y >= kEditY) {
    return -1;
  }
  int16_t col = (x - kStepX0) / kStepPitch;
  int16_t row = (y - kStepY0) / kStepPitch;
  if (col > 7 || row > 1) {
    return -1;
  }
  if ((x - kStepX0) % kStepPitch >= kStepCell || (y - kStepY0) % kStepPitch >= kStepCell) {
    return -1;
  }
  return (int8_t)(row * 8 + col);
}

// Any control anywhere on screen; pads/steps come back offset from
// kControlPadBase / kControlStepBase.
inline int16_t hitTest(int16_t x, int16_t y) {
  int8_t pad = hitPad(x, y);
  if (pad >= 0) {
    return kControlPadBase + pad;
  }
  int8_t step = hitStep(x, y);
  if (step >= 0) {
    return kControlStepBase + step;
  }
  if (y >= kBtnY && y < kBtnY + kBtnH) {
    if (inRect(x, y, kPlayX, kBtnY, kPlayW, kBtnH)) return kControlPlay;
    if (inRect(x, y, kStopX, kBtnY, kStopW, kBtnH)) return kControlStop;
    if (inRect(x, y, kRecX, kBtnY, kRecW, kBtnH)) return kControlRec;
    if (inRect(x, y, kBpmMinusX, kBtnY, kBpmMinusW, kBtnH)) return kControlBpmMinus;
    if (inRect(x, y, kBpmPlusX, kBtnY, kBpmPlusW, kBtnH)) return kControlBpmPlus;
    if (inRect(x, y, kSwingMinusX, kBtnY, kSwingMinusW, kBtnH)) return kControlSwingMinus;
    if (inRect(x, y, kSwingPlusX, kBtnY, kSwingPlusW, kBtnH)) return kControlSwingPlus;
    if (inRect(x, y, kMetroX, kBtnY, kMetroW, kBtnH)) return kControlMetro;
    for (uint8_t i = 0; i < 4; i++) {
      if (inRect(x, y, kPatternX + i * kPatternPitch, kBtnY, kPatternW, kBtnH)) {
        return kControlPattern0 + i;
      }
    }
    return kControlNone;
  }
  // Sliders get a taller hit zone than their track for finger comfort.
  if (inRect(x, y, kSliderX - 8, kVolSliderY - 8, kSliderW + 16, kSliderH + 16)) {
    return kControlVolSlider;
  }
  if (inRect(x, y, kSliderX - 8, kPitchSliderY - 8, kSliderW + 16, kSliderH + 16)) {
    return kControlPitchSlider;
  }
  if (y >= kChokeY && y < kChokeY + kChokeH) {
    for (uint8_t i = 0; i < 5; i++) {
      if (inRect(x, y, kChokeX0 + i * kChokePitch, kChokeY, kChokeW, kChokeH)) {
        return kControlChoke0 + i;
      }
    }
  }
  if (y >= kKitY && y < kKitY + kKitH) {
    if (inRect(x, y, kKitPrevX, kKitY, kKitArrowW, kKitH)) return kControlKitPrev;
    if (inRect(x, y, kKitNextX, kKitY, kKitArrowW, kKitH)) return kControlKitNext;
  }
  return kControlNone;
}

// Slider position -> value 0..range (clamped), for VOL/PITCH drags.
inline int32_t sliderValue(int16_t x, int32_t range) {
  int32_t v = (int32_t)(x - kSliderX) * range / (kSliderW - 1);
  if (v < 0) v = 0;
  if (v > range) v = range;
  return v;
}

}  // namespace UiLayout

#endif
