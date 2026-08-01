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

  // contactCount tracks every LIVE contact fed into hits[], not confirmed
  // hits: a palm landing between keys still occupies a slot and hit-tests to
  // -1, which stickResolve() then drops. Don't read this as a hit tally.
  int hits[StickTouch::kMaxContacts];
  int contactCount = 0;
  if (enabled_) {
    for (uint8_t i = 0; i < StickTouch::kMaxContacts; i++) {
      const StickTouch::Contact &c = touch_.contact(i);
      if (!c.active) continue;
      // Hit-test against downX/downY (press position), not x/y (live
      // position) -- a deliberate call, not a default. This panel has no
      // tactile edge, which is the whole concept's documented weakness: a
      // finger drifting during a long defensive hold must not silently slide
      // off its key and drop a block mid-string, so once a contact lands on
      // a key it owns that key for its entire life. The cost is real: no
      // sliding between adjacent buttons (plink/piano inputs), which a
      // physical stick supports and this can't. An annoying limitation beats
      // a catastrophic one, so this loses that ability on purpose.
      //
      // A contact landing outside every key returns -1 and is dropped by
      // stickResolve. That is the whole of our palm handling.
      hits[contactCount++] = stickHitTest(*profile_, c.downX, c.downY);
    }
  }

  const StickState s = stickResolve(*profile_, hits, contactCount);

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
    changes_++;
  }
  // Called unconditionally; HidBackend does its own change detection (and
  // that detection is NOT the same test as changes_ above -- see changes()).
  hid_->gamepadState(hat, s.buttons);

  polls_++;
  const uint32_t elapsed = micros() - startUs;
  if (elapsed > worstPollUs_) worstPollUs_ = elapsed;
}
