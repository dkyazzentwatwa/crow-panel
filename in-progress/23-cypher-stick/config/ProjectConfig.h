#ifndef CYPHER_STICK_PROJECT_CONFIG_H
#define CYPHER_STICK_PROJECT_CONFIG_H

// Cypher Stick — touch fightstick. Flags default off (mock-first); real builds
// pass -D flags so the shared library sees them too (see CLAUDE.md).

// Max keys in one profile. Sizes fixed arrays — do not raise casually.
#ifndef STICK_MAX_KEYS
#define STICK_MAX_KEYS 20
#endif

// Profiles held in RAM / stored on SD.
#ifndef STICK_MAX_PROFILES
#define STICK_MAX_PROFILES 8
#endif

// Press and release are deliberately asymmetric, and conflating them was the
// original design error here.
//
// A PRESS commits on the first poll that sees contact. That is the latency this
// project exists for, and nothing debounces it.
//
// A RELEASE cannot work that way. Project 21 documents, from this panel, that
// the GT911 "briefly drops/re-reports a contact (flicker) during a real touch",
// and sized its 30 ms debounce to "bridge a few dropped 8 ms frames". That is
// on-hardware evidence, not caution. Committing a lift on the first empty poll
// would turn every flicker into a spurious release — a dropped block or a
// dropped charge, which is worse than a late one.
//
// So a lift is confirmed only after this long with no contact. Wall-clock
// rather than a poll count, so it stays correct if STICK_POLL_MS changes.
// 24 ms is slightly tighter than project 21's 30 ms because we sample ~4x more
// often and can distinguish a real lift sooner. Lower it ONLY with hardware
// evidence that flicker is absent — the repo's existing evidence says it is not.
#ifndef STICK_LIFT_CONFIRM_MS
#define STICK_LIFT_CONFIRM_MS 24
#endif

// Stick task cadence on core 1. The GT911 reports at 100 Hz, so polling much
// faster than this only burns I2C bandwidth.
#ifndef STICK_POLL_MS
#define STICK_POLL_MS 2
#endif

// GT911 touch calibration for StickTouch, carried over unchanged from project
// 21's CYPHER_KEYS_TOUCH_* defaults (see KeysTouch.cpp) — override per board
// after a `touch` diagnostic run.
#ifndef STICK_TOUCH_MIN_X
#define STICK_TOUCH_MIN_X 0
#endif
#ifndef STICK_TOUCH_MAX_X
#define STICK_TOUCH_MAX_X 1023
#endif
#ifndef STICK_TOUCH_MIN_Y
#define STICK_TOUCH_MIN_Y 0
#endif
#ifndef STICK_TOUCH_MAX_Y
#define STICK_TOUCH_MAX_Y 599
#endif
#ifndef STICK_TOUCH_SWAP_XY
#define STICK_TOUCH_SWAP_XY 0
#endif
#ifndef STICK_TOUCH_INVERT_X
#define STICK_TOUCH_INVERT_X 0
#endif
#ifndef STICK_TOUCH_INVERT_Y
#define STICK_TOUCH_INVERT_Y 0
#endif

// Multi-contact matching: the GT911 keeps a track id per finger, but on some
// panels those ids churn between samples. An unmatched incoming point that
// lands within this many mapped pixels of an unmatched live contact is
// treated as the same finger continuing, not as a new press. Carried over
// unchanged from project 21's CYPHER_KEYS_TOUCH_MATCH_RADIUS.
#ifndef STICK_TOUCH_MATCH_RADIUS
#define STICK_TOUCH_MATCH_RADIUS 48
#endif

#include <AppConfig.h>

#endif
