// Host-side tests for project 23. Compiles the SHIPPING sources, not copies —
// SocdCleaner.h and StickLayout.h are deliberately free of Arduino.h so this
// is possible. Run via scripts/test-cypher-stick.sh; no board required.

#include "../src/SocdCleaner.h"

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

static void testSocdSingleDirections() {
  std::printf("socd: single directions pass through on every policy\n");
  for (uint8_t p = kSocdNeutral; p <= kSocdUpPriority; p++) {
    SocdMemory m;
    check(socdResolve(true, false, false, false, p, m) == kHatUp, "up");
    SocdMemory m2;
    check(socdResolve(false, true, false, false, p, m2) == kHatDown, "down");
    SocdMemory m3;
    check(socdResolve(false, false, true, false, p, m3) == kHatLeft, "left");
    SocdMemory m4;
    check(socdResolve(false, false, false, true, p, m4) == kHatRight, "right");
    SocdMemory m5;
    check(socdResolve(false, false, false, false, p, m5) == kHatCenter, "none");
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

static void testSocdAllCombosLegal() {
  std::printf("socd: all 16 combos x 4 policies produce a legal hat\n");
  for (uint8_t p = kSocdNeutral; p <= kSocdUpPriority; p++) {
    for (int bits = 0; bits < 16; bits++) {
      SocdMemory m;
      uint8_t hat = socdResolve(bits & 1, bits & 2, bits & 4, bits & 8, p, m);
      char d[64];
      std::snprintf(d, sizeof d, "policy=%u bits=%d hat=%u", p, bits, hat);
      check(hat <= kHatUpLeft, "hat within 0..8", d);
    }
  }
}

int main() {
  testSocdSingleDirections();
  testSocdDiagonals();
  testSocdNeutral();
  testSocdUpPriority();
  testSocdLastInput();
  testSocdFirstInput();
  testSocdAllCombosLegal();
  std::printf("\n%d checks, %d failures\n", gRun, gFail);
  return gFail ? 1 : 0;
}
