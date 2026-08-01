#include "StickTouch.h"

#if USE_DISPLAY
#include <CrowPanelShared.h>  // CrowDisplay::touchPoints
#endif

namespace {

int16_t clampi(int32_t v, int32_t lo, int32_t hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return (int16_t)v;
}

int32_t dist2(int16_t ax, int16_t ay, int16_t bx, int16_t by) {
  int32_t dx = ax - bx;
  int32_t dy = ay - by;
  return dx * dx + dy * dy;
}

}  // namespace

// Calibration math is carried over verbatim from project 21's KeysTouch (and,
// before that, TouchInput) so an existing per-board STICK_TOUCH_* override
// keeps meaning exactly what it did.
int16_t StickTouch::mapX(int16_t rawX, int16_t rawY) const {
  int32_t raw = STICK_TOUCH_SWAP_XY ? rawY : rawX;
  int32_t span = (int32_t)STICK_TOUCH_MAX_X - STICK_TOUCH_MIN_X;
  if (span == 0) span = 1;
  int32_t v = (raw - STICK_TOUCH_MIN_X) * 1023 / span;
#if STICK_TOUCH_INVERT_X
  v = 1023 - v;
#endif
  return clampi(v, 0, 1023);
}

int16_t StickTouch::mapY(int16_t rawX, int16_t rawY) const {
  int32_t raw = STICK_TOUCH_SWAP_XY ? rawX : rawY;
  int32_t span = (int32_t)STICK_TOUCH_MAX_Y - STICK_TOUCH_MIN_Y;
  if (span == 0) span = 1;
  int32_t v = (raw - STICK_TOUCH_MIN_Y) * 599 / span;
#if STICK_TOUCH_INVERT_Y
  v = 599 - v;
#endif
  return clampi(v, 0, 599);
}

void StickTouch::tick() {
  // Retire one-tick edges and free slots that released on the previous tick.
  for (uint8_t i = 0; i < kMaxContacts; i++) {
    Contact &c = contacts_[i];
    c.pressedEdge = false;
    if (c.releasedEdge) {
      c.releasedEdge = false;
      c.id = 0xFF;
      c.owner = -1;
      c.nextRepeatMs = 0;
    }
    c.seenThisPoll = false;
  }

  // Read the panel at a fixed cadence, independent of how fast loop() runs, so
  // we never poll the GT911 faster than it refreshes (which returns empties and
  // makes the trackpad stutter). Between reads, hold the last state.
  uint32_t now = millis();
  if (polledOnce_ &&
      (int32_t)(now - lastPollMs_) < (int32_t)STICK_POLL_MS) {
    updatePrimary();
    return;
  }
  polledOnce_ = true;
  lastPollMs_ = now;

#if USE_DISPLAY
  CrowDisplay::TouchPointData raw[kMaxContacts];
  uint8_t rawCount = CrowDisplay::touchPoints(raw, kMaxContacts);
#else
  // Headless: same shape, never any contacts.
  struct { int16_t x; int16_t y; uint8_t id; } raw[kMaxContacts];
  uint8_t rawCount = 0;
#endif

  // Pass 1: match incoming points to live contacts by GT911 track id.
  bool rawMatched[kMaxContacts] = {false};
  for (uint8_t r = 0; r < rawCount; r++) {
    for (uint8_t i = 0; i < kMaxContacts; i++) {
      Contact &c = contacts_[i];
      if (c.active && !c.seenThisPoll && c.id == raw[r].id) {
        c.x = mapX(raw[r].x, raw[r].y);
        c.y = mapY(raw[r].x, raw[r].y);
        c.releasePending = false;
        c.seenThisPoll = true;
        rawMatched[r] = true;
        break;
      }
    }
  }

  // Pass 2: unmatched incoming points. Adopt the nearest unmatched live contact
  // when it is close enough (GT911 id churn), else it is a new press.
  for (uint8_t r = 0; r < rawCount; r++) {
    if (rawMatched[r]) {
      continue;
    }
    int16_t x = mapX(raw[r].x, raw[r].y);
    int16_t y = mapY(raw[r].x, raw[r].y);

    int8_t nearest = -1;
    int32_t nearestD2 =
        (int32_t)STICK_TOUCH_MATCH_RADIUS * STICK_TOUCH_MATCH_RADIUS;
    for (uint8_t i = 0; i < kMaxContacts; i++) {
      Contact &c = contacts_[i];
      if (c.active && !c.seenThisPoll) {
        int32_t d2 = dist2(c.x, c.y, x, y);
        if (d2 <= nearestD2) {
          nearestD2 = d2;
          nearest = (int8_t)i;
        }
      }
    }
    if (nearest >= 0) {
      // Same finger, new track id: keep its press binding (owner) and its
      // repeat clock, only re-point it.
      Contact &c = contacts_[nearest];
      c.id = raw[r].id;
      c.x = x;
      c.y = y;
      c.releasePending = false;
      c.seenThisPoll = true;
      continue;
    }

    for (uint8_t i = 0; i < kMaxContacts; i++) {
      Contact &c = contacts_[i];
      if (!c.active) {
        c.active = true;
        c.pressedEdge = true;
        c.releasedEdge = false;
        c.id = raw[r].id;
        c.x = c.downX = x;
        c.y = c.downY = y;
        c.downMs = now;
        c.owner = -1;
        c.nextRepeatMs = 0;
        c.releasePending = false;
        c.seenThisPoll = true;
        totalPresses_++;
        break;
      }
    }
  }

  // Live contacts that vanished this poll: debounce, then commit the release.
  for (uint8_t i = 0; i < kMaxContacts; i++) {
    Contact &c = contacts_[i];
    if (!c.active || c.seenThisPoll) {
      continue;
    }
    if (!c.releasePending) {
      c.releasePending = true;
      c.releasePendingSinceMs = now;
    } else if ((int32_t)(now - c.releasePendingSinceMs) >=
               (int32_t)STICK_LIFT_CONFIRM_MS) {
      c.active = false;
      c.releasePending = false;
      c.releasedEdge = true;  // owner/x/y stay readable for this tick
    }
    // else: within the debounce window - hold active and the last position.
  }

  updatePrimary();
}

void StickTouch::updatePrimary() {
  // Lowest-index active slot wins; if nothing is down, the slot that released
  // on this tick is still the primary so releasedEdge()/releaseX() work.
  int8_t primary = -1;
  for (uint8_t i = 0; i < kMaxContacts; i++) {
    if (contacts_[i].active) {
      primary = (int8_t)i;
      break;
    }
  }
  if (primary < 0) {
    for (uint8_t i = 0; i < kMaxContacts; i++) {
      if (contacts_[i].releasedEdge) {
        primary = (int8_t)i;
        break;
      }
    }
  }

  if (primary < 0) {
    // Nothing to report; hold the last position, as TouchInput did.
    down_ = false;
    pressedEdge_ = false;
    releasedEdge_ = false;
    return;
  }

  const Contact &c = contacts_[primary];
  down_ = c.active;
  pressedEdge_ = c.pressedEdge;
  releasedEdge_ = c.releasedEdge;
  x_ = c.x;
  y_ = c.y;
  if (c.releasedEdge) {
    releaseX_ = c.x;
    releaseY_ = c.y;
  }
}

uint8_t StickTouch::activeCount() const {
  uint8_t count = 0;
  for (uint8_t i = 0; i < kMaxContacts; i++) {
    if (contacts_[i].active) {
      count++;
    }
  }
  return count;
}

String StickTouch::diagnostics() const {
  String out = String("contacts=") + String(activeCount()) +
               " presses=" + String(totalPresses_);
  for (uint8_t i = 0; i < kMaxContacts; i++) {
    const Contact &c = contacts_[i];
    if (c.active) {
      out += String(" [") + String(i) + "] id=" + String(c.id) +
             " x=" + String(c.x) + " y=" + String(c.y) +
             " owner=" + String(c.owner);
    }
  }
  return out;
}
