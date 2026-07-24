#ifndef CROW_PANEL_TOUCH_INPUT_H
#define CROW_PANEL_TOUCH_INPUT_H

#include <Arduino.h>
#include "AppConfig.h"

// Shared touch helper for panel UIs: polls the GT911 point exposed by
// CrowDisplay, applies board calibration, and turns the noisy contact signal
// into a live finger position plus clean press/release edges.
//
// Hit-test navigation should key off releasedEdge() + releaseX/releaseY() so a
// drag that starts on one control and ends on another does not fire either.
// Drag-style surfaces (game paddles, trackpads) use down() with x()/y().
//
// On USE_DISPLAY=0 builds the shared touch namespace has no panel behind it,
// so this degrades to a never-pressed stub and headless builds stay green.
class CrowTouch {
 public:
  void tick();  // call once per loop()

  bool down() const { return down_; }
  int16_t x() const { return x_; }
  int16_t y() const { return y_; }

  bool pressedEdge() const { return pressedEdge_; }    // true only on press tick
  bool releasedEdge() const { return releasedEdge_; }  // true only on release tick
  int16_t releaseX() const { return releaseX_; }
  int16_t releaseY() const { return releaseY_; }

  uint32_t count() const { return count_; }  // taps seen since boot

  // Last raw (uncalibrated) point, for the `touch` diagnostic command.
  int16_t rawX() const { return rawX_; }
  int16_t rawY() const { return rawY_; }

 private:
  int16_t mapX(int16_t rawX, int16_t rawY) const;
  int16_t mapY(int16_t rawX, int16_t rawY) const;

  bool down_ = false;  // debounced contact state
  bool pressedEdge_ = false;
  bool releasedEdge_ = false;
  bool releasePending_ = false;  // saw no contact but not yet past debounce
  uint32_t releasePendingSinceMs_ = 0;
  int16_t x_ = 0;
  int16_t y_ = 0;
  int16_t rawX_ = 0;
  int16_t rawY_ = 0;
  int16_t releaseX_ = 0;
  int16_t releaseY_ = 0;
  uint32_t count_ = 0;
};

#endif
