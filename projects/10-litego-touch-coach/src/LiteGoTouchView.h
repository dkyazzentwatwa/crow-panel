#ifndef LITEGO_TOUCH_VIEW_H
#define LITEGO_TOUCH_VIEW_H

#include "../config/ProjectConfig.h"
#include <Arduino.h>
#include "LiteGoGame.h"

// Full-screen touch surface for the 1024x600 panel.
//
// Two things make this feel like an app rather than a status screen:
//
//  * Placement is preview-then-confirm. The first tap parks a ghost stone on
//    the nearest intersection, the second on the same point commits it. A
//    fingertip is wider than a grid cell, so committing on the first tap
//    misplaces stones no matter how good the calibration is.
//
//  * Drawing is dirty-region only. The panel is opened with manualFlush so
//    Arduino_GFX stops cache-syncing per primitive, and each region flushes
//    just its own rows. Nothing calls fillScreen after boot, which is also why
//    the single-framebuffer panel never flashes mid-update.
class LiteGoTouchView {
 public:
  enum ActionType {
    kActionNone,
    kActionPlay,
    kActionPass,
    kActionUndo,
    kActionResign,
    kActionScore,
    kActionHint,
    kActionNewGame,
    kActionLevel,
    kActionColor,
  };

  struct Action {
    ActionType type;
    int8_t x;
    int8_t y;
  };

  void begin(LiteGoGame *game);
  bool ready() const;

  // Repaint requests, coarsest first.
  void requestFullRepaint();
  void requestBoardRepaint();
  void requestStatusRepaint();
  void requestScoreRepaint();

  // Marks the points touched by a move so only those cells are redrawn.
  void noteMoveResult(const LiteGoGame::MoveResult &result, const char *source);
  void clearGhost();
  void setStatus(const String &message, bool warning = false);
  void setThinking(bool thinking);

  // Call once per loop(). Returns true when the user committed an action.
  bool tick(Action &action);

  // Raw vs mapped touch point, for on-device calibration checks. Returned as
  // text as well as printed, because native USB-CDC serial on this board drops
  // once the app is running - the panel has to be able to show it.
  void reportCalibration(Print &out) const;
  String calibrationSummary() const;

 private:
  LiteGoGame *game_ = nullptr;
  String status_ = "Tap an intersection to preview, tap it again to place.";
  bool statusWarning_ = false;

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  static const uint8_t kMaxDirtyPoints = LiteGoGame::kPointCount;

  bool ready_ = false;
  bool thinking_ = false;
  uint8_t thinkingShown_ = 255;

  // Dirty tracking
  bool dirtyChrome_ = true;
  bool dirtyBoard_ = true;
  bool dirtyStatus_ = true;
  bool dirtyScore_ = true;
  bool dirtyButtons_ = true;
  uint8_t dirtyPoints_[kMaxDirtyPoints];
  uint8_t dirtyPointCount_ = 0;

  // Ghost stone (preview) state
  int8_t ghostX_ = -1;
  int8_t ghostY_ = -1;
  // Cell currently carrying the last-move marker, so it can be cleaned up when
  // the marker moves on.
  int8_t markerX_ = -1;
  int8_t markerY_ = -1;
  bool gameOverShown_ = false;

  // Touch state
  bool down_ = false;
  bool wasDown_ = false;
  int16_t touchX_ = 0;
  int16_t touchY_ = 0;
  int16_t pressX_ = 0;
  int16_t pressY_ = 0;
  int16_t rawX_ = 0;
  int16_t rawY_ = 0;
  int8_t pressedButton_ = -1;
  uint32_t lastActionMs_ = 0;

  int16_t mapX(int16_t rawX, int16_t rawY) const;
  int16_t mapY(int16_t rawX, int16_t rawY) const;
  void sampleTouch();

  int8_t buttonAt(int16_t x, int16_t y) const;
  bool pointAt(int16_t x, int16_t y, int8_t &px, int8_t &py) const;
  void markPointDirty(uint8_t x, uint8_t y);

  void drawChrome();
  void drawBoardAll();
  void drawCell(uint8_t x, uint8_t y);
  void drawScorePanel();
  void drawStatusPanel();
  void drawButtons();
  void drawThinking();
  void drawGameOver();
  void render();
#endif
};

#endif
