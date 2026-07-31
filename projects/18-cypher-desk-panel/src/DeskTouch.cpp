#include "DeskTouch.h"

#if USE_DISPLAY
#include <CrowPanelShared.h>  // CrowDisplay::touchPoints
#endif

namespace {

int16_t clampAxis(int32_t value, int32_t low, int32_t high) {
  if (value < low) return static_cast<int16_t>(low);
  if (value > high) return static_cast<int16_t>(high);
  return static_cast<int16_t>(value);
}

int32_t distanceSquared(int16_t ax, int16_t ay, int16_t bx, int16_t by) {
  const int32_t dx = ax - bx;
  const int32_t dy = ay - by;
  return dx * dx + dy * dy;
}

}  // namespace

// Calibration math carried over verbatim from the old DeskTouchService so an
// existing per-board CYPHER_DESK_TOUCH_* override keeps meaning what it did.
int16_t DeskTouch::mapX(int16_t rawX, int16_t rawY) const {
  int32_t raw = CYPHER_DESK_TOUCH_SWAP_XY ? rawY : rawX;
  int32_t span = static_cast<int32_t>(CYPHER_DESK_TOUCH_MAX_X) - CYPHER_DESK_TOUCH_MIN_X;
  if (span == 0) span = 1;
  int32_t value = (raw - CYPHER_DESK_TOUCH_MIN_X) * 1023 / span;
#if CYPHER_DESK_TOUCH_INVERT_X
  value = 1023 - value;
#endif
  return clampAxis(value, 0, 1023);
}

int16_t DeskTouch::mapY(int16_t rawX, int16_t rawY) const {
  int32_t raw = CYPHER_DESK_TOUCH_SWAP_XY ? rawX : rawY;
  int32_t span = static_cast<int32_t>(CYPHER_DESK_TOUCH_MAX_Y) - CYPHER_DESK_TOUCH_MIN_Y;
  if (span == 0) span = 1;
  int32_t value = (raw - CYPHER_DESK_TOUCH_MIN_Y) * 599 / span;
#if CYPHER_DESK_TOUCH_INVERT_Y
  value = 599 - value;
#endif
  return clampAxis(value, 0, 599);
}

void DeskTouch::tick() {
  // Retire one-tick edges and free slots that released on the previous tick.
  for (uint8_t i = 0; i < kMaxContacts; ++i) {
    Contact &contact = contacts_[i];
    contact.pressedEdge = false;
    if (contact.releasedEdge) {
      contact.releasedEdge = false;
      contact.id = 0xFF;
      contact.owner = -1;
      contact.nextRepeatMs = 0;
    }
    contact.seenThisPoll = false;
  }

  // Read the panel at a fixed cadence, independent of how fast loop() runs.
  // Between reads, hold the last state.
  const uint32_t now = millis();
  if (polledOnce_ && static_cast<int32_t>(now - lastPollMs_) <
                         static_cast<int32_t>(CYPHER_DESK_TOUCH_POLL_MS)) {
    updatePrimary();
    return;
  }
  polledOnce_ = true;
  lastPollMs_ = now;

#if USE_DISPLAY
  CrowDisplay::TouchPointData raw[kMaxContacts];
  const uint8_t rawCount = CrowDisplay::touchPoints(raw, kMaxContacts);
#else
  // Headless: same shape, never any contacts.
  struct {
    int16_t x;
    int16_t y;
    uint8_t id;
  } raw[kMaxContacts] = {};
  const uint8_t rawCount = 0;
#endif

  if (rawCount > 0) {
    lastRawX_ = raw[0].x;
    lastRawY_ = raw[0].y;
  }

  // Pass 1: match incoming points to live contacts by GT911 track id.
  bool matched[kMaxContacts] = {false};
  for (uint8_t r = 0; r < rawCount; ++r) {
    for (uint8_t i = 0; i < kMaxContacts; ++i) {
      Contact &contact = contacts_[i];
      if (contact.active && !contact.seenThisPoll && contact.id == raw[r].id) {
        contact.x = mapX(raw[r].x, raw[r].y);
        contact.y = mapY(raw[r].x, raw[r].y);
        contact.releasePending = false;
        contact.seenThisPoll = true;
        matched[r] = true;
        break;
      }
    }
  }

  // Pass 2: unmatched points. Adopt the nearest unmatched live contact when it
  // is close enough (GT911 id churn), else it is a new press.
  for (uint8_t r = 0; r < rawCount; ++r) {
    if (matched[r]) continue;
    const int16_t x = mapX(raw[r].x, raw[r].y);
    const int16_t y = mapY(raw[r].x, raw[r].y);

    int8_t nearest = -1;
    int32_t nearestDistance =
        static_cast<int32_t>(CYPHER_DESK_TOUCH_MATCH_RADIUS) * CYPHER_DESK_TOUCH_MATCH_RADIUS;
    for (uint8_t i = 0; i < kMaxContacts; ++i) {
      Contact &contact = contacts_[i];
      if (contact.active && !contact.seenThisPoll) {
        const int32_t distance = distanceSquared(contact.x, contact.y, x, y);
        if (distance <= nearestDistance) {
          nearestDistance = distance;
          nearest = static_cast<int8_t>(i);
        }
      }
    }
    if (nearest >= 0) {
      // Same finger, new track id: keep its press binding and repeat clock,
      // only re-point it.
      Contact &contact = contacts_[nearest];
      contact.id = raw[r].id;
      contact.x = x;
      contact.y = y;
      contact.releasePending = false;
      contact.seenThisPoll = true;
      continue;
    }

    for (uint8_t i = 0; i < kMaxContacts; ++i) {
      Contact &contact = contacts_[i];
      if (!contact.active) {
        contact.active = true;
        contact.pressedEdge = true;
        contact.releasedEdge = false;
        contact.id = raw[r].id;
        contact.x = contact.downX = x;
        contact.y = contact.downY = y;
        contact.downMs = now;
        contact.owner = -1;
        contact.nextRepeatMs = 0;
        contact.releasePending = false;
        contact.seenThisPoll = true;
        ++totalPresses_;
        break;
      }
    }
  }

  // Live contacts that vanished this poll: debounce, then commit the release.
  for (uint8_t i = 0; i < kMaxContacts; ++i) {
    Contact &contact = contacts_[i];
    if (!contact.active || contact.seenThisPoll) continue;
    if (!contact.releasePending) {
      contact.releasePending = true;
      contact.releasePendingSinceMs = now;
    } else if (static_cast<int32_t>(now - contact.releasePendingSinceMs) >=
               static_cast<int32_t>(CYPHER_DESK_TOUCH_RELEASE_DEBOUNCE_MS)) {
      contact.active = false;
      contact.releasePending = false;
      contact.releasedEdge = true;  // owner/x/y stay readable for this tick
    }
    // else: within the debounce window - hold active and the last position.
  }

  updatePrimary();
}

void DeskTouch::forgetBindings() {
  // Used when waking a dimmed panel: the tap that woke it must not also fire
  // whatever it landed on, and a pending release must not replay as a key-up.
  for (uint8_t i = 0; i < kMaxContacts; ++i) {
    contacts_[i].owner = -1;
    contacts_[i].nextRepeatMs = 0;
  }
}

void DeskTouch::updatePrimary() {
  // Lowest-index active slot wins; if nothing is down, the slot that released
  // on this tick is still primary so releasedEdge()/releaseX() work.
  int8_t primary = -1;
  for (uint8_t i = 0; i < kMaxContacts; ++i) {
    if (contacts_[i].active) {
      primary = static_cast<int8_t>(i);
      break;
    }
  }
  if (primary < 0) {
    for (uint8_t i = 0; i < kMaxContacts; ++i) {
      if (contacts_[i].releasedEdge) {
        primary = static_cast<int8_t>(i);
        break;
      }
    }
  }

  if (primary < 0) {
    down_ = false;
    pressedEdge_ = false;
    releasedEdge_ = false;
    return;
  }

  const Contact &contact = contacts_[primary];
  down_ = contact.active;
  pressedEdge_ = contact.pressedEdge;
  releasedEdge_ = contact.releasedEdge;
  x_ = contact.x;
  y_ = contact.y;
  if (contact.releasedEdge) {
    releaseX_ = contact.x;
    releaseY_ = contact.y;
  }
}

uint8_t DeskTouch::activeCount() const {
  uint8_t count = 0;
  for (uint8_t i = 0; i < kMaxContacts; ++i) {
    if (contacts_[i].active) ++count;
  }
  return count;
}

String DeskTouch::diagnostics() const {
  String out = String("contacts=") + activeCount() + " presses=" + totalPresses_ +
               " raw=" + lastRawX_ + "," + lastRawY_;
  for (uint8_t i = 0; i < kMaxContacts; ++i) {
    const Contact &contact = contacts_[i];
    if (contact.active) {
      out += String(" [") + i + "] id=" + contact.id + " x=" + contact.x + " y=" + contact.y +
             " owner=" + contact.owner;
    }
  }
  return out;
}
