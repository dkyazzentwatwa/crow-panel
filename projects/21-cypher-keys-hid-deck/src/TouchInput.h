#ifndef CYPHER_KEYS_TOUCH_INPUT_H
#define CYPHER_KEYS_TOUCH_INPUT_H

#include "../config/ProjectConfig.h"
#include <Arduino.h>

// Forked from Cypher Desk's DeskTouchService: polls the shared GT911 touch
// point, applies board calibration, and exposes both the live finger position
// (for the trackpad) and clean press/release edges (for taps). On non-display
// builds the shared touch namespace does not exist, so this degrades to a
// never-pressed stub and everything still compiles headless.
class TouchInput {
 public:
  void tick();  // call once per loop()

  bool down() const { return down_; }
  int16_t x() const { return x_; }
  int16_t y() const { return y_; }

  bool pressedEdge() const { return pressedEdge_; }   // true only on press tick
  bool releasedEdge() const { return releasedEdge_; }  // true only on release tick
  int16_t releaseX() const { return releaseX_; }
  int16_t releaseY() const { return releaseY_; }

  uint32_t count() const { return count_; }

 private:
  int16_t mapX(int16_t rawX, int16_t rawY) const;
  int16_t mapY(int16_t rawX, int16_t rawY) const;

  bool down_ = false;            // debounced contact state
  bool pressedEdge_ = false;
  bool releasedEdge_ = false;
  bool releasePending_ = false;  // saw no-contact but not yet past debounce
  uint32_t releasePendingSinceMs_ = 0;
  int16_t x_ = 0;
  int16_t y_ = 0;
  int16_t releaseX_ = 0;
  int16_t releaseY_ = 0;
  uint32_t count_ = 0;
  bool polledOnce_ = false;      // force a read on the very first tick
  uint32_t lastPollMs_ = 0;      // last time the GT911 was actually read
};

#endif
