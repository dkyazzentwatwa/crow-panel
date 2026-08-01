// Host-side tests for project 23's SOCD cleaner. Compiles the SHIPPING
// sources, not copies — SocdCleaner.h and StickLayout.h (Task 3) are
// deliberately free of Arduino.h so this is possible. Run via
// scripts/test-cypher-stick.sh; no board required.
//
// Why this exists: a physical arcade stick's Left and Right microswitches are
// mechanically exclusive, so "holding both" cannot happen. A touch panel has
// no such interlock — two fingers can hold both directions at once, and that
// input still has to collapse onto ONE of the eight legal hat values before
// it reaches the USB gamepad HID report, because a hat value is a single
// 0-8 enum with no way to encode "both". Which value it collapses to is not a
// free choice: competitive fighting-game rulesets specify one of a handful of
// standard SOCD resolutions (neutral, last-input-priority, first-input-
// priority, up-priority), and resolving it wrong is a competitive-integrity
// bug, not a cosmetic one. These tests pin that resolution logic.

#include "../src/SocdCleaner.h"

#include <cstddef>
#include <cstdio>

static int gFail = 0;
static int gRun = 0;

static void check(bool ok, const char *what, const char *detail = "") {
  gRun++;
  if (!ok) {
    gFail++;
    std::printf("  FAIL %s %s\n", what, detail);
  }
}

static const char *policyName(uint8_t p) {
  switch (p) {
    case kSocdNeutral: return "neutral";
    case kSocdLastInput: return "last-input";
    case kSocdFirstInput: return "first-input";
    case kSocdUpPriority: return "up-priority";
    default: return "unknown";
  }
}

// Decodes the up/down/left/right bit pattern used by the exhaustive sweeps
// below into a short direction string (e.g. "LR", "UD", "none") instead of a
// raw integer, so a failure is readable without cross-referencing the bit
// convention. Bit order matches the socdResolve() call: bit0 up, bit1 down,
// bit2 left, bit3 right.
static void bitsName(int bits, char *out, std::size_t outSize) {
  char tmp[8];
  char *p = tmp;
  if (bits & 1) *p++ = 'U';
  if (bits & 2) *p++ = 'D';
  if (bits & 4) *p++ = 'L';
  if (bits & 8) *p++ = 'R';
  *p = '\0';
  std::snprintf(out, outSize, "%s", (tmp[0] == '\0') ? "none" : tmp);
}

// Decodes which of up/down/left/right a hat value visually reports as held.
static void hatImplies(uint8_t hat, bool *up, bool *down, bool *left, bool *right) {
  *up = (hat == kHatUp || hat == kHatUpRight || hat == kHatUpLeft);
  *down = (hat == kHatDown || hat == kHatDownRight || hat == kHatDownLeft);
  *left = (hat == kHatLeft || hat == kHatDownLeft || hat == kHatUpLeft);
  *right = (hat == kHatRight || hat == kHatUpRight || hat == kHatDownRight);
}

// The core legality invariant: a hat may never report a direction the player
// is not actually holding. This is what catches a wrong-but-legal diagonal
// (e.g. reporting UpRight for an Up+Left press) that "hat <= kHatUpLeft" on
// its own cannot - every hat value is in range, just the wrong one.
static bool hatMatchesHeld(uint8_t hat, bool up, bool down, bool left, bool right) {
  bool iu, id, il, ir;
  hatImplies(hat, &iu, &id, &il, &ir);
  if (iu && !up) return false;
  if (id && !down) return false;
  if (il && !left) return false;
  if (ir && !right) return false;
  return true;
}

static void testSocdSingleDirections() {
  std::printf("socd: single directions pass through on every policy\n");
  for (uint8_t p = kSocdNeutral; p <= kSocdUpPriority; p++) {
    char d[32];
    std::snprintf(d, sizeof d, "policy=%s", policyName(p));
    SocdMemory m;
    check(socdResolve(true, false, false, false, p, m) == kHatUp, "up", d);
    SocdMemory m2;
    check(socdResolve(false, true, false, false, p, m2) == kHatDown, "down", d);
    SocdMemory m3;
    check(socdResolve(false, false, true, false, p, m3) == kHatLeft, "left", d);
    SocdMemory m4;
    check(socdResolve(false, false, false, true, p, m4) == kHatRight, "right", d);
    SocdMemory m5;
    check(socdResolve(false, false, false, false, p, m5) == kHatCenter, "none", d);
  }
}

static void testSocdDiagonals() {
  std::printf("socd: legal diagonals\n");
  SocdMemory m;
  check(socdResolve(true, false, false, true, kSocdNeutral, m) == kHatUpRight, "up+right");
  SocdMemory m2;
  check(socdResolve(false, true, true, false, kSocdNeutral, m2) == kHatDownLeft, "down+left");
}

static void testSocdNeutral() {
  std::printf("socd: neutral policy\n");
  SocdMemory m;
  check(socdResolve(false, false, true, true, kSocdNeutral, m) == kHatCenter, "L+R -> center");
  SocdMemory m2;
  check(socdResolve(true, true, false, false, kSocdNeutral, m2) == kHatCenter, "U+D -> center");
}

static void testSocdUpPriority() {
  std::printf("socd: up priority\n");
  SocdMemory m;
  check(socdResolve(true, true, false, false, kSocdUpPriority, m) == kHatUp, "U+D -> up");
  SocdMemory m2;
  check(socdResolve(false, false, true, true, kSocdUpPriority, m2) == kHatCenter,
        "L+R falls back to neutral");
}

static void testSocdLastInput() {
  std::printf("socd: last input priority\n");
  SocdMemory m;
  // Hold left, then add right: right is newest and wins.
  check(socdResolve(false, false, true, false, kSocdLastInput, m) == kHatLeft, "left first");
  check(socdResolve(false, false, true, true, kSocdLastInput, m) == kHatRight, "right added wins");
  // Release right; left alone resumes.
  check(socdResolve(false, false, true, false, kSocdLastInput, m) == kHatLeft, "left resumes");
}

static void testSocdFirstInput() {
  std::printf("socd: first input priority\n");
  SocdMemory m;
  check(socdResolve(false, false, true, false, kSocdFirstInput, m) == kHatLeft, "left first");
  check(socdResolve(false, false, true, true, kSocdFirstInput, m) == kHatLeft, "left keeps it");
  check(socdResolve(false, false, false, true, kSocdFirstInput, m) == kHatRight, "left released");
}

// Task 9 deserialises the policy byte from an SD-card profile, so a corrupt
// or future/unknown value must not crash or invent a side - it must degrade
// to neutral. Both directions must be held to actually exercise the switch's
// default branch; a single direction never reaches it.
static void testSocdUnknownPolicyDegradesToNeutral() {
  std::printf("socd: unknown policy value (corrupt profile) degrades to neutral\n");
  SocdMemory m;
  check(socdResolve(false, false, true, true, 99, m) == kHatCenter, "policy=99 -> center");
}

// Regression guard for mutation: kSocdFirstInput's "both arrived together"
// fall-through replaced with an unconditional `else winner = 1;`. Neither
// direction was held before this poll, so there is no "first" - the tie must
// stay neutral, and stay neutral for as long as both are held, not just on
// the poll where the tie happened.
static void testSocdFirstInputSimultaneousStaysCenter() {
  std::printf("socd: first-input, L+R pressed on the SAME poll stays centered for the whole hold\n");
  SocdMemory m;
  for (int i = 0; i < 4; i++) {
    char d[32];
    std::snprintf(d, sizeof d, "poll %d", i);
    check(socdResolve(false, false, true, true, kSocdFirstInput, m) == kHatCenter,
          "no first-presser to favor", d);
  }
}

// Regression guard for mutation: kSocdLastInput adding
// `else if (aIsNew && bIsNew) winner = 2;`. When both directions arrive on
// the same poll neither is "more recent" than the other, so the standing
// winner (centered - neither has ever taken it) must be left alone, and stay
// that way across repeated polls of the same hold.
static void testSocdLastInputSimultaneousKeepsStandingWinner() {
  std::printf("socd: last-input, L+R pressed on the SAME poll keeps the standing (centered) winner\n");
  SocdMemory m;
  for (int i = 0; i < 4; i++) {
    char d[32];
    std::snprintf(d, sizeof d, "poll %d", i);
    check(socdResolve(false, false, true, true, kSocdLastInput, m) == kHatCenter,
          "no most-recent presser to favor", d);
  }
}

// Regression guard for mutation: deleting the three `winner = 0;` resets in
// resolveAxis's not-both-held branches. Left held first, right joins (left
// keeps priority, as testSocdFirstInput already covers) - but then left
// releases and RE-joins with right still down. Right was the one already
// held when left came back, so right should win this time. A stale winner
// left over from the first hold would incorrectly keep it on left.
static void testSocdFirstInputReleaseThenRepress() {
  std::printf("socd: first-input recomputes priority after a release, not before\n");
  SocdMemory m;
  check(socdResolve(false, false, true, false, kSocdFirstInput, m) == kHatLeft, "left alone");
  check(socdResolve(false, false, true, true, kSocdFirstInput, m) == kHatLeft,
        "left was first, keeps it");
  check(socdResolve(false, false, false, true, kSocdFirstInput, m) == kHatRight,
        "left released, right alone (unambiguous)");
  check(socdResolve(false, false, true, true, kSocdFirstInput, m) == kHatRight,
        "right was first this time, keeps it");
}

// Maps a hat produced while driving only ONE axis (so the result is always
// Center or one of that axis's two sides) to what the OTHER axis's driver
// should report for the identical press pattern. No diagonal is ever
// reachable in this mapping because the undriven axis is held at
// (false, false) throughout, which resolveAxis always resolves to 0.
static uint8_t mirrorAcrossAxes(uint8_t oneAxisHat) {
  switch (oneAxisHat) {
    case kHatLeft: return kHatUp;
    case kHatRight: return kHatDown;
    default: return kHatCenter;
  }
}

// resolveAxis is shared between the vertical and horizontal calls specifically
// so both axes behave identically - nothing had verified that assumption for
// the vertical axis under a stateful policy. Every existing stateful test
// drives (false, false, left, right); this drives (up, down, false, false)
// through the same press patterns under the same policy and checks the two
// results mirror (left<->up, right<->down). UpPriority is excluded - it is
// deliberately asymmetric (up beats down, but horizontal falls to neutral),
// so it has no mirror to keep.
static void testSocdAxisMirror() {
  std::printf("socd: vertical axis mirrors horizontal (left<->up, right<->down) under symmetric policies\n");
  const uint8_t kSymmetricPolicies[] = {kSocdNeutral, kSocdLastInput, kSocdFirstInput};
  for (uint8_t policy : kSymmetricPolicies) {
    for (int bits1 = 0; bits1 < 4; bits1++) {
      for (int bits2 = 0; bits2 < 4; bits2++) {
        bool a1 = bits1 & 1, b1 = bits1 & 2;
        bool a2 = bits2 & 1, b2 = bits2 & 2;

        SocdMemory mh, mv;
        uint8_t h1 = socdResolve(false, false, a1, b1, policy, mh);
        uint8_t v1 = socdResolve(a1, b1, false, false, policy, mv);
        uint8_t h2 = socdResolve(false, false, a2, b2, policy, mh);
        uint8_t v2 = socdResolve(a2, b2, false, false, policy, mv);

        char d[96];
        std::snprintf(d, sizeof d, "policy=%s a1=%d b1=%d a2=%d b2=%d h=(%u,%u) v=(%u,%u)",
                      policyName(policy), a1, b1, a2, b2, h1, h2, v1, v2);
        check(mirrorAcrossAxes(h1) == v1 && mirrorAcrossAxes(h2) == v2,
              "vertical result mirrors horizontal result", d);
      }
    }
  }
}

// Regression guard for the untested `winner = 0;` store in the kSocdNeutral /
// default case: the settings screen can change SOCD policy while the player
// is still holding both directions. Left is held first under last-input
// (right wins), then the policy switches to neutral WHILE STILL HOLDING
// BOTH - the standing "right" winner must be cleared, not just masked for
// one poll, or the next policy switch (to first-input) would read it back
// and treat right as having been there first when it demonstrably was not.
static void testSocdPolicySwitchMidHoldReresolves() {
  std::printf("socd: switching policy mid-hold re-resolves from scratch, not from a stale winner\n");
  SocdMemory m;
  check(socdResolve(false, false, true, false, kSocdLastInput, m) == kHatLeft,
        "left alone under last-input");
  check(socdResolve(false, false, true, true, kSocdLastInput, m) == kHatRight,
        "right added, right wins (last-input)");
  check(socdResolve(false, false, true, true, kSocdNeutral, m) == kHatCenter,
        "switch to neutral mid-hold: standing winner is cleared, not carried over");
  check(socdResolve(false, false, true, true, kSocdFirstInput, m) == kHatCenter,
        "switch to first-input mid-hold: no first-presser exists post-clear, stays neutral");
}

// Regression guard for the untested `winner = 0;` store in the kSocdUpPriority
// case, on the vertical axis (the only axis that ever reaches it - see
// socdResolve's hPolicy remap). Down wins under last-input, then the policy
// switches to up-priority while still holding both (up correctly wins,
// unconditionally, whether or not the store fires) - the store only becomes
// observable on the NEXT switch back to last-input, which must see a cleared
// winner and re-resolve to neutral rather than replaying the stale "down".
static void testSocdUpPriorityStoreResetsOnPolicySwitch() {
  std::printf("socd: switching to up-priority mid-hold clears the standing winner too\n");
  SocdMemory m;
  check(socdResolve(true, false, false, false, kSocdLastInput, m) == kHatUp,
        "up alone under last-input");
  check(socdResolve(true, true, false, false, kSocdLastInput, m) == kHatDown,
        "down added, down wins (last-input)");
  check(socdResolve(true, true, false, false, kSocdUpPriority, m) == kHatUp,
        "switch to up-priority mid-hold: up wins outright");
  check(socdResolve(true, true, false, false, kSocdLastInput, m) == kHatCenter,
        "switch back to last-input: standing winner was cleared, not left at 'down'");
}

// Exhaustive sweep over PAIRS of polls: one SocdMemory is shared across both
// calls in a pair (hoisted out of the per-poll resolve), so this reaches the
// prev-state seeding and the earliest a standing winner can be SET (poll 2)
// - state a fresh-memory-per-combo sweep never reaches.
//
// It does NOT reach a poll that READS a previously-set winner back: the
// earliest that happens is poll 3 (set on poll 2, read on poll 3), and a
// two-poll sweep structurally can't get there - from a reset, "both held"
// always means both sides are new, so poll 1 of any pair can never SET a
// winner either. That state is exercised by the named regression tests
// above and by testSocdPolicySwitchMidHoldReresolves /
// testSocdUpPriorityStoreResetsOnPolicySwitch below, not by this sweep. What
// this sweep buys is breadth: every policy x every one-step transition,
// checked against the invariants below, not depth into the state machine.
//
// 4 policies x 16 starting combos x 16 following combos = 1024 poll pairs.
static void testSocdAllTransitionsLegal() {
  const int kPolicies = 4, kCombos = 16;
  std::printf("socd: state transitions stay legal (%d policies x %d x %d = %d poll pairs)\n",
              kPolicies, kCombos, kCombos, kPolicies * kCombos * kCombos);
  for (uint8_t p = kSocdNeutral; p <= kSocdUpPriority; p++) {
    for (int bits1 = 0; bits1 < kCombos; bits1++) {
      for (int bits2 = 0; bits2 < kCombos; bits2++) {
        SocdMemory m;
        bool up1 = bits1 & 1, down1 = bits1 & 2, left1 = bits1 & 4, right1 = bits1 & 8;
        bool up2 = bits2 & 1, down2 = bits2 & 2, left2 = bits2 & 4, right2 = bits2 & 8;
        uint8_t hat1 = socdResolve(up1, down1, left1, right1, p, m);
        uint8_t hat2 = socdResolve(up2, down2, left2, right2, p, m);

        char b1[8], b2[8], d[96];
        bitsName(bits1, b1, sizeof b1);
        bitsName(bits2, b2, sizeof b2);
        std::snprintf(d, sizeof d, "policy=%s %s->%s hat1=%u hat2=%u",
                      policyName(p), b1, b2, hat1, hat2);
        bool legal = hat1 <= kHatUpLeft && hat2 <= kHatUpLeft;
        bool noPhantomDirection = hatMatchesHeld(hat1, up1, down1, left1, right1) &&
                                   hatMatchesHeld(hat2, up2, down2, left2, right2);
        check(legal && noPhantomDirection,
              "both polls stay legal and never report an unheld direction", d);
      }
    }
  }
}

int main() {
  std::printf("[host] SocdCleaner: single-poll resolution semantics\n");
  testSocdSingleDirections();
  testSocdDiagonals();
  testSocdNeutral();
  testSocdUpPriority();
  testSocdLastInput();
  testSocdFirstInput();
  testSocdUnknownPolicyDegradesToNeutral();

  std::printf("\n[host] SocdCleaner: persistent-winner state across polls\n");
  testSocdFirstInputSimultaneousStaysCenter();
  testSocdLastInputSimultaneousKeepsStandingWinner();
  testSocdFirstInputReleaseThenRepress();
  testSocdAxisMirror();
  testSocdPolicySwitchMidHoldReresolves();
  testSocdUpPriorityStoreResetsOnPolicySwitch();

  std::printf("\n[host] SocdCleaner: exhaustive transition sweep\n");
  testSocdAllTransitionsLegal();

  std::printf("\n%d checks, %d failures\n", gRun, gFail);
  if (gFail == 0) {
    std::printf("RESULT PASS\n");
    return 0;
  }
  std::printf("RESULT FAIL\n");
  return 1;
}
