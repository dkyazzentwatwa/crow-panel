#ifndef INKWELL_INK_GESTURES_H
#define INKWELL_INK_GESTURES_H

// Touch gesture recognizer for the reader: turns the GT911's noisy contact
// stream into clean one-shot events. Pure C++ (no Arduino) so the host
// suite drives it with scripted contact streams; the .ino feeds it
// CrowDisplay::touchPoint() + millis() once per loop.
//
// Why not the shared CrowTouch: its release debounce is exactly what we
// need (the GT911 drops contact for a poll or two mid-touch, and treating
// that as finger-up fires phantom taps mid-swipe -- the first hardware
// boot's "glitchy" feel), but its map/clamp stage is hard-coded to the
// landscape 1024x600 logical space and would clamp portrait Y at 599.
// This class keeps CrowTouch's debounce window (same constant) and adds
// what a paged reader needs on top -- tap-vs-swipe classification with a
// movement slop -- mirroring how the other touch-first projects
// (09 TouchTracker, 21 KeysTouch, 22 GbInput) wrap the raw point with
// their own state machine instead of stretching CrowTouch.
//
// Rules (the same contract CrowTouch's header documents for hit-tests):
// - Tap fires on RELEASE, reported at the PRESS position, so a finger
//   that wobbles a few px still hits the control it pressed.
// - A horizontal move >= kSwipeMinPx that stays clearly flatter than tall
//   (|dx| >= 1.5*|dy|) is a swipe; direction is the finger's motion.
// - Anything else that left the tap slop is a wandering drag and fires
//   NOTHING -- a drag from one control to another hits neither.

#include <stdint.h>

namespace Ink {

struct GestureEvent {
  enum Kind : uint8_t { None, Tap, SwipeLeft, SwipeRight };
  Kind kind = None;
  int16_t x = 0;  // press position (for Tap and swipes alike)
  int16_t y = 0;
};

class Gestures {
 public:
  // CrowTouch's proven window (AppConfig CROW_TOUCH_RELEASE_DEBOUNCE_MS):
  // contact must stay absent this long before a release commits.
  static constexpr uint32_t kReleaseDebounceMs = 30;
  // GT911 jitter allowance before a press stops counting as a tap.
  static constexpr int16_t kTapSlopPx = 18;
  // Minimum horizontal travel for a page-turn swipe.
  static constexpr int16_t kSwipeMinPx = 80;

  // Feed one poll; the returned event is edge-style (non-None on at most
  // the single tick a gesture completes).
  GestureEvent tick(bool contact, int16_t cx, int16_t cy, uint32_t nowMs) {
    GestureEvent ev;
    pressedEdge_ = false;
    if (contact) {
      x_ = cx;
      y_ = cy;
      releasePending_ = false;
      if (!down_) {
        down_ = true;
        pressedEdge_ = true;
        downX_ = cx;
        downY_ = cy;
        moved_ = false;
      } else if (absi(cx - downX_) > kTapSlopPx ||
                 absi(cy - downY_) > kTapSlopPx) {
        moved_ = true;  // latches: a round trip back to the start is no tap
      }
    } else if (down_) {
      if (!releasePending_) {
        releasePending_ = true;
        releasePendingSinceMs_ = nowMs;
      } else if ((int32_t)(nowMs - releasePendingSinceMs_) >=
                 (int32_t)kReleaseDebounceMs) {
        down_ = false;
        releasePending_ = false;
        const int16_t adx = absi(x_ - downX_);
        const int16_t ady = absi(y_ - downY_);
        if (!moved_) {
          ev.kind = GestureEvent::Tap;
        } else if (adx >= kSwipeMinPx &&
                   (int32_t)adx * 2 >= (int32_t)ady * 3) {
          ev.kind = x_ < downX_ ? GestureEvent::SwipeLeft
                                : GestureEvent::SwipeRight;
        }
        ev.x = downX_;
        ev.y = downY_;
      }
      // else: inside the debounce window -- hold down_ and the last
      // position so a brief sensor dropout mid-gesture never splits it.
    }
    return ev;
  }

  bool down() const { return down_; }
  bool pressedEdge() const { return pressedEdge_; }  // true only on press tick
  int16_t x() const { return x_; }                   // last live position
  int16_t y() const { return y_; }
  int16_t downX() const { return downX_; }
  int16_t downY() const { return downY_; }

 private:
  static int16_t absi(int32_t v) { return (int16_t)(v < 0 ? -v : v); }

  bool down_ = false;
  bool pressedEdge_ = false;
  bool releasePending_ = false;
  bool moved_ = false;
  uint32_t releasePendingSinceMs_ = 0;
  int16_t x_ = 0, y_ = 0;
  int16_t downX_ = 0, downY_ = 0;
};

}  // namespace Ink

#endif  // INKWELL_INK_GESTURES_H
