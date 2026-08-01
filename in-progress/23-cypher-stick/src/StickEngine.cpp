#include "StickEngine.h"

static_assert(STICK_MAX_KEYS == STICK_LAYOUT_MAX_KEYS,
              "STICK_MAX_KEYS and STICK_LAYOUT_MAX_KEYS must agree");
static_assert(STICK_MAX_PROFILES == STICK_LAYOUT_MAX_PROFILES,
              "STICK_MAX_PROFILES and STICK_LAYOUT_MAX_PROFILES must agree");

void StickEngine::begin(HidBackend *hid, StickProfile *profile) {
  hid_ = hid;
  profile_ = profile;
}

void StickEngine::poll() {
  if (!hid_ || !profile_) return;
  const uint32_t startUs = micros();

  touch_.tick();

  int hits[StickTouch::kMaxContacts];
  int hitCount = 0;
  if (enabled_) {
    for (uint8_t i = 0; i < StickTouch::kMaxContacts; i++) {
      const StickTouch::Contact &c = touch_.contact(i);
      if (!c.active) continue;
      // A contact landing outside every key returns -1 and is dropped by
      // stickResolve. That is the whole of our palm handling.
      hits[hitCount++] = stickHitTest(*profile_, c.x, c.y);
    }
  }

  const StickState s = stickResolve(*profile_, hits, hitCount);

  // socdResolve() MUST be called exactly once per input frame: the Last/First
  // policies infer press order from the transition between calls, so a second
  // call for the same frame sees prev == current and silently mis-resolves,
  // with no error anywhere. Anything else that wants the current direction
  // (a debug overlay, the renderer) reads hat_ below - it never re-calls.
  // socd_ is owned by this task alone and is not thread-safe.
  const uint8_t hat = socdResolve(s.up, s.down, s.left, s.right,
                                  profile_->socdPolicy, socd_);

  keysHeld_ = s.keysHeld;  // physical touch, for the renderer - not SOCD-filtered
  if (hat != hat_ || s.buttons != buttons_) {
    hat_ = hat;
    buttons_ = s.buttons;
    sends_++;
  }
  // Called unconditionally; HidBackend does its own change detection.
  hid_->gamepadState(hat, s.buttons);

  polls_++;
  const uint32_t elapsed = micros() - startUs;
  if (elapsed > worstPollUs_) worstPollUs_ = elapsed;
}
