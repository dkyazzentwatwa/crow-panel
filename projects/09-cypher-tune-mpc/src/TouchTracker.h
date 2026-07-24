#ifndef CYPHER_TUNE_TOUCH_TRACKER_H
#define CYPHER_TUNE_TOUCH_TRACKER_H

#include "../config/ProjectConfig.h"
#include <Arduino.h>

// Multi-contact touch state machine over CrowDisplay::touchPoints(): tracks
// up to 5 fingers by GT911 track id (with a proximity fallback for panels
// whose ids churn), and exposes clean press/release edges per contact so two
// pads can be drummed simultaneously. Forked in spirit from project 21's
// single-point TouchInput, keeping its fixed poll cadence and release
// debounce (a dropped GT911 frame mid-press must not read as a lift).
// Headless builds compile to a never-touched stub.
class TouchTracker {
 public:
  static const uint8_t kMaxContacts = 5;

  struct Contact {
    bool active = false;         // finger currently down
    bool pressedEdge = false;    // true only on the tick the finger landed
    bool releasedEdge = false;   // true only on the tick the lift committed
    uint8_t id = 0xFF;           // GT911 track id
    int16_t x = 0, y = 0;        // current mapped position
    int16_t downX = 0, downY = 0;  // position at press (hit-test against THIS)
    uint32_t downMs = 0;
    int16_t owner = -1;          // UiLayout control the press landed on
    uint32_t nextRepeatMs = 0;   // hold-repeat bookkeeping (UI-managed)
    bool releasePending = false;
    uint32_t releasePendingSinceMs = 0;
    bool seenThisPoll = false;
  };

  void tick();  // call once per loop()

  uint8_t activeCount() const;
  Contact &contact(uint8_t i) { return contacts_[i]; }
  const Contact &contact(uint8_t i) const { return contacts_[i]; }
  uint32_t totalPresses() const { return totalPresses_; }
  String diagnostics() const;

 private:
  int16_t mapX(int16_t rawX, int16_t rawY) const;
  int16_t mapY(int16_t rawX, int16_t rawY) const;

  Contact contacts_[kMaxContacts];
  uint32_t totalPresses_ = 0;
  bool polledOnce_ = false;
  uint32_t lastPollMs_ = 0;
};

#endif
