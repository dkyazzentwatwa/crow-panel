#ifndef CYPHER_STICK_STICK_TOUCH_H
#define CYPHER_STICK_STICK_TOUCH_H

#include "../config/ProjectConfig.h"
#include <Arduino.h>

// Multi-contact touch tracking for the fightstick, forked from project 21's
// KeysTouch and retuned for latency.
//
// The real change is the POLL RATE, not the debounce: every STICK_POLL_MS
// (2 ms) instead of 16 ms, so a press reaches the host ~14 ms sooner.
//
// The release debounce is KEPT, and keeping it is deliberate. Project 21 sized
// its 30 ms window to "bridge a few dropped 8 ms frames" of observed GT911
// flicker on this panel. Dropping that to one poll would convert every flicker
// into a spurious release — in a fighting game, a dropped block or a lost
// charge. A late release is survivable; a phantom one is not. STICK_LIFT_
// CONFIRM_MS is 24 ms, slightly tighter than P21 only because we sample ~4x
// more often.
//
// So: presses are fast, releases are safe, and the two are not symmetric.
//
// The GT911 tracks at most 5 contacts. That ceiling is hardware.
class StickTouch {
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
