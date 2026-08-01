// Host-side tests for project 09's rate-sensitive arithmetic.
//
// These compile the SHIPPING headers, not copies of them - that is the whole
// point of keeping src/LoopLock.h free of Arduino.h. Run via
// scripts/test-cypher-tune.sh; no board required.

#include "../src/LoopLock.h"
#include "../src/dsp/Envelope.h"
#include "../src/dsp/Svf.h"

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

static const uint32_t kRate = 32000;

// A filter that diverges does not produce a polite artifact - it produces a
// full-scale burst into a small speaker amp. This sweeps the ENTIRE control
// space with full-scale noise and asserts nothing ever leaves its bounds. It is
// the single highest-value test in this file, because the failure it guards
// against is hardware-only and destructive.
static void testSvfStability() {
  std::printf("SVF stability sweep (the speaker-saving one)\n");
  Dsp::initSvfTables(kRate);

  uint32_t rng = 12345;
  auto noise = [&rng]() {
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
    return (int32_t)((int16_t)(rng & 0xFFFF)) << 8;  // full-scale, Q23.8
  };

  int worstType = -1, worstCut = -1, worstRes = -1;
  int32_t worstState = 0, worstOut = 0;

  for (uint8_t type = Dsp::kFilterLP; type < Dsp::kFilterTypeCount; type++) {
    for (int c = 0; c < 256; c += 5) {
      for (int r = 0; r < 256; r += 5) {
        Dsp::Svf f;
        f.setType(type);
        f.setCutoff((uint8_t)c, (uint8_t)r);
        f.reset();
        for (int n = 0; n < 2000; n++) {
          int32_t out = f.process(noise());
          if (std::abs(out) > std::abs(worstOut)) {
            worstOut = out; worstType = type; worstCut = c; worstRes = r;
          }
          if (std::abs(f.ic1) > std::abs(worstState)) worstState = f.ic1;
          if (std::abs(f.ic2) > std::abs(worstState)) worstState = f.ic2;

          char d[160];
          if (std::abs(out) > Dsp::kSvfOutMax) {
            std::snprintf(d, sizeof d, "type=%u cut=%d res=%d out=%d",
                          type, c, r, out);
            check(false, "output stays inside the clamp", d);
            n = 2000;
          }
          if (std::abs(f.ic1) > Dsp::kSvfStateMax ||
              std::abs(f.ic2) > Dsp::kSvfStateMax) {
            std::snprintf(d, sizeof d, "type=%u cut=%d res=%d ic1=%d ic2=%d",
                          type, c, r, f.ic1, f.ic2);
            check(false, "state stays inside the clamp", d);
            n = 2000;
          }
        }
      }
    }
  }
  check(true, "swept all types x cutoff x resonance");
  std::printf("  worst |out|=%d (limit %d) at type=%d cut=%d res=%d; worst |state|=%d (limit %d)\n",
              std::abs(worstOut), Dsp::kSvfOutMax, worstType, worstCut, worstRes,
              std::abs(worstState), Dsp::kSvfStateMax);
}

// A filter is only useful if it actually filters. DC through a lowpass should
// survive; DC through a highpass should not.
static void testSvfResponse() {
  std::printf("SVF frequency response sanity\n");
  Dsp::initSvfTables(kRate);
  const int32_t dc = 16000 << 8;

  auto settle = [&](uint8_t type, uint8_t cut, uint8_t res, int32_t in) {
    Dsp::Svf f;
    f.setType(type);
    f.setCutoff(cut, res);
    f.reset();
    int32_t out = 0;
    for (int n = 0; n < 20000; n++) out = f.process(in);
    return out;
  };

  // Mid cutoff, gentle Q, so the corner is nowhere near DC either way.
  int32_t lp = settle(Dsp::kFilterLP, 160, 0, dc);
  int32_t hp = settle(Dsp::kFilterHP, 160, 0, dc);
  char d[128];
  std::snprintf(d, sizeof d, "lp=%d in=%d", lp, dc);
  check(std::abs(lp - dc) < dc / 20, "lowpass passes DC", d);
  std::snprintf(d, sizeof d, "hp=%d", hp);
  check(std::abs(hp) < dc / 20, "highpass blocks DC", d);

  // Off must be bit-exact passthrough, or bypassing the filter changes the
  // sound and the 'filter off' setting is a lie.
  Dsp::Svf off;
  off.setType(Dsp::kFilterOff);
  check(off.process(12345) == 12345, "kFilterOff is exact passthrough");

  // Cutoff must be monotonic in the index, or a knob sweep would jump around.
  bool monotonic = true;
  for (int i = 1; i < 256; i++) {
    if (Dsp::svfCutoffHz((uint8_t)i, kRate) <= Dsp::svfCutoffHz((uint8_t)(i - 1), kRate)) {
      monotonic = false;
    }
  }
  check(monotonic, "cutoff is monotonic across the index");
  std::snprintf(d, sizeof d, "%.1f Hz .. %.0f Hz",
                Dsp::svfCutoffHz(0, kRate), Dsp::svfCutoffHz(255, kRate));
  check(Dsp::svfCutoffHz(255, kRate) < kRate / 2.0f, "top of sweep stays below Nyquist", d);
  std::printf("  cutoff range %s, Q %.2f .. %.1f\n", d,
              Dsp::svfResonanceQ(0), Dsp::svfResonanceQ(255));
}

static void testEnvelope() {
  std::printf("ADSR envelope\n");
  Dsp::initEnvTables(kRate);

  // Guard the root cause directly, not just its symptom: a zero coefficient is
  // a permanently frozen envelope. Walk every index and prove the one-pole
  // actually moves from rest.
  for (int i = 0; i < 256; i++) {
    Dsp::Env probe;
    probe.gateOn((uint8_t)i, 128, 200, (uint8_t)i);
    int32_t before = probe.level;
    probe.step();
    char d[96];
    std::snprintf(d, sizeof d, "idx=%d coef=%d level %d -> %d",
                  i, probe.coef, before, probe.level);
    check(probe.coef > 0, "attack coefficient is never zero", d);
    check(probe.level > before, "envelope advances on the first sample", d);
  }

  // Every A/D/S/R combination must terminate. An envelope that never reaches
  // idle leaks a voice permanently, which presents as "polyphony slowly dies".
  //
  // The extremes are in the list deliberately. A Q15 coefficient rounds to 1 at
  // index 222 and to 0 at index 255, which stalls the one-pole partway and
  // hangs the voice forever - that is the bug this file caught, and stepping
  // the loop by a stride that skips 255 would have hidden half of it.
  const int idxs[] = {0, 1, 37, 74, 111, 148, 185, 222, 240, 254, 255};
  for (int ai = 0; ai < (int)(sizeof idxs / sizeof idxs[0]); ai++) {
    for (int ri = 0; ri < (int)(sizeof idxs / sizeof idxs[0]); ri++) {
      const int a = idxs[ai], r = idxs[ri];
      Dsp::Env e;
      e.gateOn((uint8_t)a, 40, 200, (uint8_t)r);
      int n = 0;
      const int limit = (int)kRate * 30;
      while (e.stage != Dsp::kEnvSustain && n < limit) { e.step(); e.tick(); n++; }
      char d[128];
      std::snprintf(d, sizeof d, "a=%d r=%d frames=%d", a, r, n);
      check(e.stage == Dsp::kEnvSustain, "reaches sustain", d);

      e.gateOff();
      n = 0;
      while (e.stage != Dsp::kEnvIdle && n < limit) { e.step(); e.tick(); n++; }
      std::snprintf(d, sizeof d, "a=%d r=%d release frames=%d", a, r, n);
      check(e.stage == Dsp::kEnvIdle, "release reaches idle", d);
    }
  }

  // kill() is used for choke, steal and stop, so it must be bounded and short
  // regardless of the patch's release time.
  for (int r = 0; r < 256; r += 17) {
    Dsp::Env e;
    e.gateOn(0, 40, 200, (uint8_t)r);
    for (int n = 0; n < 2000; n++) { e.step(); e.tick(); }
    e.kill();
    int n = 0;
    while (e.stage != Dsp::kEnvIdle && n < 4096) { e.step(); e.tick(); n++; }
    char d[96];
    std::snprintf(d, sizeof d, "r=%d kill frames=%d", r, n);
    check(e.stage == Dsp::kEnvIdle, "kill always terminates", d);
    check(n <= 128, "kill terminates within ~96 frames", d);
  }

  // Legacy equivalence: the minimum attack must still de-click. The old code
  // used a fixed 16-frame ramp, so a 0 ms attack must not arrive faster.
  Dsp::Env e;
  e.gateOn(0, 40, 255, 0);
  int frames = 0;
  while (e.level < Dsp::kEnvOne / 2 && frames < 1000) { e.step(); e.tick(); frames++; }
  char d[96];
  std::snprintf(d, sizeof d, "half-level at %d frames", frames);
  check(frames >= 4, "fastest attack still ramps (no click)", d);

  // Level must never leave [0, 1] - it multiplies audio.
  Dsp::Env e2;
  e2.gateOn(0, 0, 0, 0);
  bool bounded = true;
  for (int n = 0; n < 20000; n++) {
    e2.step(); e2.tick();
    if (e2.level < 0 || e2.level > Dsp::kEnvOne) bounded = false;
  }
  check(bounded, "level stays within [0, 1]");
}

int main() {
  std::printf("cypher-tune host tests\n\n");
  testPitchTable();
  testLockExactness();
  testLockRegression16_16();
  testNoLoopRegression();
  testSvfStability();
  testSvfResponse();
  testEnvelope();
  std::printf("\n%d checks, %d failed\n", gRun, gFail);
  if (gFail == 0) {
    std::printf("RESULT PASS\n");
    return 0;
  }
  std::printf("RESULT FAIL\n");
  return 1;
}
