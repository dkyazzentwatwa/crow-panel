#ifndef CYPHER_DESK_TOUCH_H
#define CYPHER_DESK_TOUCH_H

#include "../config/ProjectConfig.h"
#include <Arduino.h>

// Multi-contact touch state machine over CrowDisplay::touchPoints().
//
// Ported from project 21's KeysTouch. It replaces DeskTouchService, which read
// a single point and reported taps on RELEASE - while the Writer ran its own
// separate loop reading the same panel and reporting taps on PRESS. Two tap
// models in one product meant the launcher and the editor never felt the same,
// and nothing could hold one key while pressing another.
//
// Tracks up to 5 fingers by GT911 track id, with a proximity fallback for
// panels whose ids churn, and exposes clean press/release edges per contact.
// The panel is read only every CYPHER_DESK_TOUCH_POLL_MS, and a release is
// committed only after CYPHER_DESK_TOUCH_RELEASE_DEBOUNCE_MS of no contact.
//
// The single-point accessors at the bottom report the PRIMARY contact and are
// drop-in equivalents of the old single-touch view, so app chrome that only
// ever wanted one finger keeps working unchanged.
//
// Headless builds (USE_DISPLAY=0) compile to a never-touched stub.
class DeskTouch {
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
    int16_t owner = -1;         // control this press is bound to (-1 = none)
    uint32_t nextRepeatMs = 0;  // hold-repeat bookkeeping (UI-managed)
    bool releasePending = false;
    uint32_t releasePendingSinceMs = 0;
    bool seenThisPoll = false;
  };

  void tick();  // call once per loop()
  void forgetBindings();  // drop every owner (used when waking from idle dim)

  uint8_t activeCount() const;
  Contact &contact(uint8_t index) { return contacts_[index]; }
  const Contact &contact(uint8_t index) const { return contacts_[index]; }
  uint32_t totalPresses() const { return totalPresses_; }
  String diagnostics() const;

  // --- Single-point compatibility view -------------------------------------
  bool down() const { return down_; }
  int16_t x() const { return x_; }
  int16_t y() const { return y_; }
  bool pressedEdge() const { return pressedEdge_; }
  bool releasedEdge() const { return releasedEdge_; }
  int16_t releaseX() const { return releaseX_; }
  int16_t releaseY() const { return releaseY_; }
  uint32_t count() const { return totalPresses_; }
  int16_t lastRawX() const { return lastRawX_; }
  int16_t lastRawY() const { return lastRawY_; }

 private:
  int16_t mapX(int16_t rawX, int16_t rawY) const;
  int16_t mapY(int16_t rawX, int16_t rawY) const;
  void updatePrimary();

  Contact contacts_[kMaxContacts];
  uint32_t totalPresses_ = 0;
  bool polledOnce_ = false;
  uint32_t lastPollMs_ = 0;

  bool down_ = false;
  bool pressedEdge_ = false;
  bool releasedEdge_ = false;
  int16_t x_ = 0;
  int16_t y_ = 0;
  int16_t releaseX_ = 0;
  int16_t releaseY_ = 0;
  int16_t lastRawX_ = 0;  // for the `touch` diagnostics command
  int16_t lastRawY_ = 0;
};

#endif
