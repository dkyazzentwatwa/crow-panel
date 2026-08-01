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

// Exhaustive sweep over PAIRS of polls: one SocdMemory is shared across both
// calls in a pair (hoisted out of the per-poll resolve), so this walks the
// persistent-winner state Last/First exist for - not just fresh-memory,
// first-poll state. 4 policies x 16 starting combos x 16 following combos =
// 1024 poll pairs. The property under test (every hat stays in 0..8) is
// structurally hard to violate on its own; the point of sweeping transitions
// is coverage of the state-carrying code paths that the mutation tests above
// found gaps in, not the assertion's strength in isolation.
static void testSocdAllTransitionsLegal() {
  const int kPolicies = 4, kCombos = 16;
  std::printf("socd: state transitions stay legal (%d policies x %d x %d = %d poll pairs)\n",
              kPolicies, kCombos, kCombos, kPolicies * kCombos * kCombos);
  for (uint8_t p = kSocdNeutral; p <= kSocdUpPriority; p++) {
    for (int bits1 = 0; bits1 < kCombos; bits1++) {
      for (int bits2 = 0; bits2 < kCombos; bits2++) {
        SocdMemory m;
        uint8_t hat1 = socdResolve(bits1 & 1, bits1 & 2, bits1 & 4, bits1 & 8, p, m);
        uint8_t hat2 = socdResolve(bits2 & 1, bits2 & 2, bits2 & 4, bits2 & 8, p, m);

        char b1[8], b2[8], d[96];
        bitsName(bits1, b1, sizeof b1);
        bitsName(bits2, b2, sizeof b2);
        std::snprintf(d, sizeof d, "policy=%s %s->%s hat1=%u hat2=%u",
                      policyName(p), b1, b2, hat1, hat2);
        check(hat1 <= kHatUpLeft && hat2 <= kHatUpLeft, "both polls in a transition stay legal", d);
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

  std::printf("\n[host] SocdCleaner: persistent-winner state across polls\n");
  testSocdFirstInputSimultaneousStaysCenter();
  testSocdLastInputSimultaneousKeepsStandingWinner();
  testSocdFirstInputReleaseThenRepress();

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
