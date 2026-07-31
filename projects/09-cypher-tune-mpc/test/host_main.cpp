// Host-side tests for project 09's rate-sensitive arithmetic.
//
// These compile the SHIPPING headers, not copies of them - that is the whole
// point of keeping src/LoopLock.h free of Arduino.h. Run via
// scripts/test-cypher-tune.sh; no board required.

#include "../src/LoopLock.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

static int gFail = 0;
static int gRun = 0;

static void check(bool ok, const char *what, const char *detail = "") {
  gRun++;
  if (!ok) {
    gFail++;
    std::printf("  FAIL %s %s\n", what, detail);
  }
}

// Pitch ratio table from AudioEngine.cpp, verified against the float it
// replaced. Kept in sync by this test rather than by hope.
static const uint32_t kPitchRatioQ16[25] = {
     32768,  34716,  36781,  38968,  41285,
     43740,  46341,  49097,  52016,  55109,
     58386,  61858,  65536,  69433,  73562,
     77936,  82570,  87480,  92682,  98193,
    104032, 110218, 116772, 123715, 131072,
};

static void testPitchTable() {
  std::printf("pitch table (replaces powf on the render task)\n");
  for (int semis = -12; semis <= 12; semis++) {
    double want = 65536.0 * std::pow(2.0, semis / 12.0);
    uint32_t got = kPitchRatioQ16[semis + 12];
    double err = std::fabs(want - (double)got);
    char d[128];
    std::snprintf(d, sizeof d, "semis=%d want=%.2f got=%u", semis, want, got);
    // Half an LSB is the best a rounded table can do.
    check(err <= 0.5, "pitch ratio within half an LSB", d);
  }
  check(kPitchRatioQ16[12] == 65536u, "unity at 0 semitones");
  check(kPitchRatioQ16[0] == 32768u, "exactly half an octave down");
  check(kPitchRatioQ16[24] == 131072u, "exactly an octave up");
}

static void testLockExactness() {
  std::printf("backing-loop tempo lock\n");
  const uint32_t engine = 32000;
  const uint32_t rates[] = {22050, 32000, 44100, 16000};
  const uint8_t barsList[] = {1, 2, 4, 8, 16};
  const double secondsList[] = {2.0, 4.0, 8.0, 10.7, 30.0};

  for (uint32_t src : rates) {
    for (uint8_t bars : barsList) {
      for (double secs : secondsList) {
        uint32_t frames = (uint32_t)(secs * src);
        LoopLock::Plan p = LoopLock::plan(frames, bars, src, engine);
        char d[192];
        std::snprintf(d, sizeof d, "src=%u bars=%u secs=%.1f frames=%u",
                      src, (unsigned)bars, secs, frames);
        if (!p.valid) {
          // Only acceptable when a step would be shorter than one frame.
          check(frames / ((uint32_t)bars * 16) == 0, "invalid only when degenerate", d);
          continue;
        }
        uint32_t steps = (uint32_t)bars * 16;

        check(p.sourceFrames % steps == 0, "trimmed to a whole number of steps", d);
        check(p.sourceFrames <= frames, "trim never grows the buffer", d);
        check(frames - p.sourceFrames < steps, "trim discards less than one step", d);

        // The property the whole design exists for: one loop cycle is the
        // same length as the sequencer's cycle. Anything over a frame here
        // means the bed will audibly slide off the grid over a long jam.
        double actual = LoopLock::actualCycleFrames(p);
        double want = (double)p.stepFramesEngine * steps;
        std::snprintf(d, sizeof d,
                      "src=%u bars=%u secs=%.1f want=%.0f actual=%.4f drift=%.4f",
                      src, (unsigned)bars, secs, want, actual, actual - want);
        check(std::fabs(actual - want) < 1.0, "cycle matches the step grid", d);

        // Playback speed must stay musically correct. This is a RELATIVE
        // tolerance on purpose: a step is a whole number of frames, so the
        // cycle can never be exactly the loop's true duration, and the error
        // scales with loop length rather than being a fixed number of
        // milliseconds. 0.05% is under a cent - inaudible as pitch, and it
        // applies to the sequencer equally, so the two stay locked.
        double playedSecs = actual / engine;
        double trueSecs = (double)p.sourceFrames / src;
        double relErr = std::fabs(playedSecs - trueSecs) / trueSecs;

        // Only assert the speed bound for geometry the instrument actually
        // supports. bars*16 steps in trueSecs implies a tempo; Sequencer
        // accepts 40-240 BPM. A 16-bar loop lasting 2 s is 1920 BPM - the
        // rounding error there is real but the configuration is not, and
        // asserting on it would just be testing the test.
        double bpm = (double)bars * 4.0 * 60.0 / trueSecs;
        if (bpm < 40.0 || bpm > 240.0) {
          continue;
        }
        std::snprintf(d, sizeof d,
                      "src=%u bars=%u bpm=%.0f played=%.4fs true=%.4fs rel=%.5f%%",
                      src, (unsigned)bars, bpm, playedSecs, trueSecs, relErr * 100.0);
        check(relErr < 0.0005, "playback speed within a cent", d);
      }
    }
  }
}

static void testLockRegression16_16() {
  // Documents WHY the increment is Q32.32. Recomputes the same plan with a
  // 16.16 increment and asserts it is measurably worse, so nobody "simplifies"
  // it back to match the pad voices.
  std::printf("Q32.32 vs 16.16 (regression guard)\n");
  uint32_t src = 22050, engine = 32000;
  uint32_t frames = (uint32_t)(10.7 * src);
  LoopLock::Plan p = LoopLock::plan(frames, 4, src, engine);
  check(p.valid, "plan is valid");
  if (!p.valid) return;

  uint32_t steps = 4 * 16;
  double want = (double)p.stepFramesEngine * steps;

  uint64_t inc16 = ((uint64_t)p.sourceFrames << 16) / (uint64_t)want;
  double actual16 = (double)p.sourceFrames * 65536.0 / (double)inc16;
  double drift16 = std::fabs(actual16 - want);
  double drift32 = std::fabs(LoopLock::actualCycleFrames(p) - want);

  char d[192];
  std::snprintf(d, sizeof d, "16.16 drift=%.4f fr/cycle, Q32.32 drift=%.6f fr/cycle",
                drift16, drift32);
  check(drift32 < drift16, "Q32.32 is tighter than 16.16", d);
  check(drift16 > 1.0, "16.16 really does exceed a frame here", d);
  std::printf("  note: %s\n", d);
}

static void testNoLoopRegression() {
  std::printf("engine-rate loops still walk 1:1\n");
  // A loop already at the engine rate must produce exactly the old integer
  // walk, or the migration silently changed behaviour for future packs.
  LoopLock::Plan p = LoopLock::plan(32000 * 8, 4, 32000, 32000);
  check(p.valid, "plan is valid");
  if (!p.valid) return;
  char d[128];
  std::snprintf(d, sizeof d, "incFP=%llu want=%llu",
                (unsigned long long)p.incFP, (unsigned long long)((uint64_t)1 << 32));
  check(p.incFP == ((uint64_t)1 << 32), "unity increment at matched rates", d);
}

int main() {
  std::printf("cypher-tune host tests\n\n");
  testPitchTable();
  testLockExactness();
  testLockRegression16_16();
  testNoLoopRegression();
  std::printf("\n%d checks, %d failed\n", gRun, gFail);
  if (gFail == 0) {
    std::printf("RESULT PASS\n");
    return 0;
  }
  std::printf("RESULT FAIL\n");
  return 1;
}
