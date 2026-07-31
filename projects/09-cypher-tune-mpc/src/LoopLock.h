#ifndef CYPHER_TUNE_LOOP_LOCK_H
#define CYPHER_TUNE_LOOP_LOCK_H

#include <stdint.h>

// Backing-loop tempo lock arithmetic, deliberately free of Arduino.h, SD and
// ProjectConfig so scripts/test-cypher-tune.sh can compile the real code on the
// host rather than a copy of it. This used to live inline in selectLoop(),
// where it could not be tested and where two different frame counts were easy
// to confuse.
//
// The problem it solves: a backing loop and the step sequencer are two clocks.
// The loop wraps on its own buffer; the sequencer counts rendered output
// frames. If a cycle of one is not exactly a cycle of the other, the bed slides
// off the grid - slowly enough to sound like "the loop feature is subtly
// broken" rather than like a bug.
//
// Two frame counts exist and mixing them is silent and awful:
//   SOURCE frames - what the buffer holds, at the WAV's own rate. Every pack
//                   from scripts/build-loop-packs.py is 22050.
//   ENGINE frames - what the sequencer counts, at CYPHER_TUNE_ENGINE_RATE.
// They were equal while both were 22050, which is why the distinction did not
// exist before and why moving to 32 kHz broke it.
namespace LoopLock {

struct Plan {
  uint32_t sourceFrames = 0;   // trimmed buffer length, in source frames
  uint32_t stepFramesEngine = 0;  // give this to Sequencer::setLockedStepFrames
  uint64_t incFP = 0;          // Q32.32 playback increment for the loop voice
  bool valid = false;
};

// Q32.32, not the 16.16 the pad voices use. A pad one-shot is a few hundred ms,
// so 16.16 truncation never accumulates; a backing loop runs all session, and
// at 22050 -> 32000 a 16.16 increment drifts up to ~7 frames per cycle, about
// 10 ms after 50 bars. Q32.32 brings that under 1e-4 frames per cycle.
inline uint64_t rateRatioFP(uint32_t srcRate, uint32_t dstRate) {
  if (srcRate == 0 || dstRate == 0) {
    return (uint64_t)1 << 32;
  }
  return ((uint64_t)srcRate << 32) / dstRate;
}

// bars * 16 sixteenth-steps. `frames` is the raw source length from loops.txt.
inline Plan plan(uint32_t frames, uint8_t bars, uint32_t srcRate, uint32_t engineRate) {
  Plan out;
  uint32_t steps = (uint32_t)bars * 16;
  if (steps == 0 || frames == 0 || srcRate == 0 || engineRate == 0) {
    return out;
  }

  // 1. Trim to a whole number of steps. The per-step division has a remainder,
  //    and leaving it would make the buffer slightly longer than the grid it
  //    drives - at most 63 source frames, under 3 ms, but it accumulates every
  //    cycle.
  uint32_t stepFramesSrc = frames / steps;
  if (stepFramesSrc == 0) {
    return out;
  }
  out.sourceFrames = stepFramesSrc * steps;

  // 2. Pick the engine-frame step length from the trimmed length, not the raw
  //    one, so steps 3 and 4 agree with step 1.
  //
  //    Round rather than floor. A step length is a whole number of frames, so
  //    the cycle cannot be exactly the loop's true duration; flooring always
  //    lands short, which plays the bed slightly fast by up to steps-1 frames
  //    per cycle (8 ms at 16 bars). Rounding halves that and centres it, for
  //    free. The residual is a tempo offset under a quarter of a cent, and it
  //    applies to the sequencer too - the grid and the loop stay locked to
  //    each other either way, which is the property that actually matters.
  uint64_t engineFrames = ((uint64_t)out.sourceFrames * engineRate) / srcRate;
  out.stepFramesEngine = (uint32_t)((engineFrames + steps / 2) / steps);
  if (out.stepFramesEngine == 0) {
    return out;
  }

  // 3. Derive the increment from the cycle the sequencer will actually count,
  //    rather than from srcRate/engineRate. This is what makes the wrap and the
  //    grid inseparable: one cycle is exactly steps * stepFramesEngine engine
  //    frames by construction. The pitch adjustment involved is far under a
  //    cent.
  uint64_t cycleEngine = (uint64_t)out.stepFramesEngine * steps;
  out.incFP = ((uint64_t)out.sourceFrames << 32) / cycleEngine;
  if (out.incFP == 0) {
    return out;
  }

  out.valid = true;
  return out;
}

// Engine frames one full loop cycle actually takes, given the plan. Tests
// assert this lands within a frame of stepFramesEngine * steps; it is not
// used at runtime.
inline double actualCycleFrames(const Plan &p) {
  if (!p.valid) {
    return 0.0;
  }
  return (double)p.sourceFrames * 4294967296.0 / (double)p.incFP;
}

}  // namespace LoopLock

#endif
