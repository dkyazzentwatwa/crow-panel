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
// Cells are wider than they are tall: the 4 rows have to fit between the
// transport bar and the full-width status strip (72..560), so the vertical
// pitch is derived from that budget rather than being square. Getting this
// wrong clips the bottom row under the status bar.
//   4*kPadCellH + 3*kPadGap = 552 - 72 = 480, leaving an 8px gap above kStatusY.
constexpr int16_t kPadX0 = 8;
constexpr int16_t kPadY0 = 72;
constexpr int16_t kPadCellW = 126;
constexpr int16_t kPadCellH = 114;
constexpr int16_t kPadGap = 8;
constexpr int16_t kPadPitchX = kPadCellW + kPadGap;  // 134
constexpr int16_t kPadPitchY = kPadCellH + kPadGap;  // 122
constexpr int16_t kPadGridRight = kPadX0 + 4 * kPadPitchX - kPadGap;   // 536
constexpr int16_t kPadGridBottom = kPadY0 + 4 * kPadPitchY - kPadGap;  // 552

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
// The kit selector moved to the settings screen; this row now carries the
// backing loop, which is a performance control and has to stay on the main
// screen so you can drop a bed in and out mid-beat.
constexpr int16_t kKitY = 404;
constexpr int16_t kKitH = 40;
constexpr int16_t kKitPrevX = 620, kKitArrowW = 48;
constexpr int16_t kKitNextX = 944;
constexpr int16_t kLoopY = kKitY;
constexpr int16_t kLoopH = kKitH;
constexpr int16_t kLoopPrevX = kKitPrevX;
constexpr int16_t kLoopNextX = kKitNextX;
constexpr int16_t kLoopArrowW = kKitArrowW;

constexpr int16_t kVoicesY = 452;
constexpr int16_t kVoicesH = 108;  // ..560
// THEME button lives in the voices panel's free right edge (the 4 meters end
// at x=944), so it costs no other control any room.
constexpr int16_t kThemeX = 948, kThemeW = 64;
constexpr int16_t kThemeY = 462, kThemeH = 44;

// Live output scope + VU. Theme and kit moved to the settings screen, so the
// trace gets the panel's full width.
constexpr int16_t kScopeX = 560;
constexpr int16_t kScopeW = 452;  // 560..1012
constexpr int16_t kScopeTraceY = 474;
constexpr int16_t kScopeTraceH = 56;
constexpr int16_t kVuY = 536;
constexpr int16_t kVuH = 14;

// SET button: top-right of the step-lane header, away from every play control.
constexpr int16_t kSetBtnX = 944, kSetBtnW = 70;
constexpr int16_t kSetBtnY = 66, kSetBtnH = 30;

// --- Settings screen (full-screen view) ---
constexpr int16_t kSetHeaderH = 60;
constexpr int16_t kSetBackX = 24, kSetBackW = 110;
constexpr int16_t kSetBackY = 14, kSetBackH = 36;
// Rows: label on the left, [-]/[<] then a bar or name, then [+]/[>], value.
constexpr int16_t kSetRow0Y = 92;
constexpr int16_t kSetRowPitch = 76;
constexpr int16_t kSetRowH = 52;
constexpr int16_t kSetLabelX = 40;
constexpr int16_t kSetMinusX = 250, kSetStepW = 76;
constexpr int16_t kSetBarX = 344, kSetBarW = 430;  // 344..774
constexpr int16_t kSetPlusX = 792;
constexpr int16_t kSetValueX = 1000;  // right-aligned
constexpr uint8_t kSetRowBrightness = 0;
constexpr uint8_t kSetRowVolume = 1;
constexpr uint8_t kSetRowTheme = 2;
constexpr uint8_t kSetRowKit = 3;
constexpr uint8_t kSetRowIdleDim = 4;
constexpr uint8_t kSetRowCount = 5;
constexpr int16_t kSetInfoY = 480;

inline int16_t setRowY(uint8_t row) { return kSetRow0Y + row * kSetRowPitch; }

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
  kControlTheme,
  kControlOpenSettings,
  kControlBack,
  kControlBrightMinus,
  kControlBrightPlus,
  kControlVolumeMinus,
  kControlVolumePlus,
  kControlThemePrev,
  kControlThemeNext,
  kControlIdleDim,
  kControlLoopPrev,
  kControlLoopNext,
  kControlPadBase = 100,   // +0..15
  kControlStepBase = 200,  // +0..15
};

inline bool inRect(int16_t x, int16_t y, int16_t rx, int16_t ry, int16_t rw, int16_t rh) {
  return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

inline int16_t padX(uint8_t col) { return kPadX0 + col * kPadPitchX; }
inline int16_t padY(uint8_t row) { return kPadY0 + row * kPadPitchY; }
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
  int16_t col = (x - kPadX0) / kPadPitchX;
  int16_t row = (y - kPadY0) / kPadPitchY;
  if (col > 3 || row > 3) {
    return -1;
  }
  // Miss the gaps between cells.
  if ((x - kPadX0) % kPadPitchX >= kPadCellW || (y - kPadY0) % kPadPitchY >= kPadCellH) {
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
  if (y >= kLoopY && y < kLoopY + kLoopH) {
    if (inRect(x, y, kLoopPrevX, kLoopY, kLoopArrowW, kLoopH)) return kControlLoopPrev;
    if (inRect(x, y, kLoopNextX, kLoopY, kLoopArrowW, kLoopH)) return kControlLoopNext;
  }
  if (inRect(x, y, kSetBtnX, kSetBtnY, kSetBtnW, kSetBtnH)) {
    return kControlOpenSettings;
  }
  return kControlNone;
}

// Settings view. Separate from hitTest() because the two screens share no
// controls - dispatching by view keeps a stale pad rect from ever firing while
// settings is up.
inline int16_t hitTestSettings(int16_t x, int16_t y) {
  if (inRect(x, y, kSetBackX, kSetBackY, kSetBackW, kSetBackH)) {
    return kControlBack;
  }
  for (uint8_t row = 0; row < kSetRowCount; row++) {
    int16_t ry = setRowY(row);
    if (y < ry || y >= ry + kSetRowH) {
      continue;
    }
    bool minus = inRect(x, y, kSetMinusX, ry, kSetStepW, kSetRowH);
    bool plus = inRect(x, y, kSetPlusX, ry, kSetStepW, kSetRowH);
    switch (row) {
      case kSetRowBrightness:
        if (minus) return kControlBrightMinus;
        if (plus) return kControlBrightPlus;
        break;
      case kSetRowVolume:
        if (minus) return kControlVolumeMinus;
        if (plus) return kControlVolumePlus;
        break;
      case kSetRowTheme:
        if (minus) return kControlThemePrev;
        if (plus) return kControlThemeNext;
        break;
      case kSetRowKit:
        if (minus) return kControlKitPrev;
        if (plus) return kControlKitNext;
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
    return kControlNone;
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
