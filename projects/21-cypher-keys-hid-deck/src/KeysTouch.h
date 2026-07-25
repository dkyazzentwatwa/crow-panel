#ifndef CYPHER_KEYS_KEYS_TOUCH_H
#define CYPHER_KEYS_KEYS_TOUCH_H

#include "../config/ProjectConfig.h"
#include <Arduino.h>

// Multi-contact touch state machine over CrowDisplay::touchPoints(): tracks up
// to 5 fingers by GT911 track id (with a proximity fallback for panels whose
// ids churn) and exposes clean press/release edges per contact, which is what
// makes real modifier chording possible - hold CMD with one finger and tap C
// with another.
//
// Forked from project 09's TouchTracker (itself forked from this project's
// single-point TouchInput), so it keeps THIS project's cadence: the panel is
// read only every CYPHER_KEYS_TOUCH_POLL_MS, and a release is committed only
// after CYPHER_KEYS_TOUCH_RELEASE_DEBOUNCE_MS of no contact, so one dropped
// GT911 frame mid-press can never read as a lift.
//
// The single-point accessors at the bottom report the PRIMARY contact and
// behave exactly like the old TouchInput, so the trackpad and the status-bar
// buttons keep their hard-won single-touch feel unchanged.
//
// Headless builds (USE_DISPLAY=0) compile to a never-touched stub.
class KeysTouch {
 public:
  static const uint8_t kMaxContacts = 5;

  struct Contact {
    bool active = false;           // finger currently down (debounced)
    bool pressedEdge = false;      // true only on the tick the finger landed
    bool releasedEdge = false;     // true only on the tick the lift committed
    uint8_t id = 0xFF;             // GT911 track id
    int16_t x = 0, y = 0;          // current mapped position
    int16_t downX = 0, downY = 0;  // position at press (hit-test against THIS)
    uint32_t downMs = 0;
    int16_t owner = -1;            // control this press is bound to (-1 = none)
    uint32_t nextRepeatMs = 0;     // hold-repeat bookkeeping (UI-managed)
    bool releasePending = false;   // saw no contact but not past the debounce
    uint32_t releasePendingSinceMs = 0;
    bool seenThisPoll = false;
  };

  void tick();  // call once per loop()

  uint8_t activeCount() const;
  Contact &contact(uint8_t i) { return contacts_[i]; }
  const Contact &contact(uint8_t i) const { return contacts_[i]; }
  uint32_t totalPresses() const { return totalPresses_; }
  String diagnostics() const;

  // --- Single-point compatibility view --------------------------------------
  // All of these report the PRIMARY contact: the lowest-index active slot, or
  // the slot that just released when none is active. Drop-in equivalents of the
  // old TouchInput accessors (position is held between polls and after a lift,
  // exactly as before).
  bool down() const { return down_; }
  int16_t x() const { return x_; }
  int16_t y() const { return y_; }
  bool pressedEdge() const { return pressedEdge_; }
  bool releasedEdge() const { return releasedEdge_; }
  int16_t releaseX() const { return releaseX_; }
  int16_t releaseY() const { return releaseY_; }
  uint32_t count() const { return totalPresses_; }

 private:
  int16_t mapX(int16_t rawX, int16_t rawY) const;
  int16_t mapY(int16_t rawX, int16_t rawY) const;
  void updatePrimary();  // refresh the single-point view from contacts_

  Contact contacts_[kMaxContacts];
  uint32_t totalPresses_ = 0;
  bool polledOnce_ = false;  // force a read on the very first tick
  uint32_t lastPollMs_ = 0;  // last time the GT911 was actually read

  // Cached single-point view (see the accessors above).
  bool down_ = false;
  bool pressedEdge_ = false;
  bool releasedEdge_ = false;
  int16_t x_ = 0;
  int16_t y_ = 0;
  int16_t releaseX_ = 0;
  int16_t releaseY_ = 0;
};

#endif
