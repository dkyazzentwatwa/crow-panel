#include "TouchInput.h"

#if USE_DISPLAY
#include <CrowPanelShared.h>  // CrowDisplay::touchPoint
#endif

namespace {
int16_t clampi(int32_t v, int32_t lo, int32_t hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return (int16_t)v;
}
}  // namespace

int16_t TouchInput::mapX(int16_t rawX, int16_t rawY) const {
  int32_t raw = CYPHER_KEYS_TOUCH_SWAP_XY ? rawY : rawX;
  int32_t span = (int32_t)CYPHER_KEYS_TOUCH_MAX_X - CYPHER_KEYS_TOUCH_MIN_X;
  if (span == 0) span = 1;
  int32_t v = (raw - CYPHER_KEYS_TOUCH_MIN_X) * 1023 / span;
#if CYPHER_KEYS_TOUCH_INVERT_X
  v = 1023 - v;
#endif
  return clampi(v, 0, 1023);
}

int16_t TouchInput::mapY(int16_t rawX, int16_t rawY) const {
  int32_t raw = CYPHER_KEYS_TOUCH_SWAP_XY ? rawX : rawY;
  int32_t span = (int32_t)CYPHER_KEYS_TOUCH_MAX_Y - CYPHER_KEYS_TOUCH_MIN_Y;
  if (span == 0) span = 1;
  int32_t v = (raw - CYPHER_KEYS_TOUCH_MIN_Y) * 599 / span;
#if CYPHER_KEYS_TOUCH_INVERT_Y
  v = 599 - v;
#endif
  return clampi(v, 0, 599);
}

void TouchInput::tick() {
  pressedEdge_ = false;
  releasedEdge_ = false;

  // Read the panel at a fixed cadence, independent of how fast loop() runs, so
  // we never poll the GT911 faster than it refreshes (which returns empties and
  // makes the trackpad stutter). Between reads, hold the last state.
  uint32_t now = millis();
  if (polledOnce_ && (int32_t)(now - lastPollMs_) < (int32_t)CYPHER_KEYS_TOUCH_POLL_MS) {
    return;
  }
  polledOnce_ = true;
  lastPollMs_ = now;

  bool raw = false;
  int16_t rawX = 0;
  int16_t rawY = 0;
#if USE_DISPLAY
  raw = CrowDisplay::touchPoint(rawX, rawY);
#endif

  if (raw) {
    // Live contact: update position and cancel any pending release so a brief
    // sensor dropout in the middle of a touch does not end it.
    x_ = mapX(rawX, rawY);
    y_ = mapY(rawX, rawY);
    releasePending_ = false;
    if (!down_) {
      down_ = true;
      pressedEdge_ = true;
      ++count_;
    }
  } else if (down_) {
    // No contact while we think a finger is down: start (or continue) the
    // debounce window; only commit the release once it has held long enough.
    if (!releasePending_) {
      releasePending_ = true;
      releasePendingSinceMs_ = now;
    } else if ((int32_t)(now - releasePendingSinceMs_) >=
               (int32_t)CYPHER_KEYS_TOUCH_RELEASE_DEBOUNCE_MS) {
      down_ = false;
      releasePending_ = false;
      releasedEdge_ = true;
      releaseX_ = x_;
      releaseY_ = y_;
    }
    // else: within the debounce window - hold down_ and the last position.
  }
}
