#include "Sequencer.h"

namespace {

char padGlyph(uint8_t padIndex) {
  uint8_t padNumber = padIndex + 1;
  if (padNumber < 10) {
    return '0' + padNumber;
  }
  return 'A' + (padNumber - 10);
}

char velocityBucket(uint8_t velocity) {
  if (velocity == 0) {
    return '.';
  }
  if (velocity < 60) {
    return '-';
  }
  if (velocity < Sequencer::kAccentThreshold) {
    return '=';
  }
  return '#';
}

}  // namespace

void Sequencer::begin(uint16_t bpm) {
  for (uint8_t p = 0; p < kPatterns; p++) {
    clearPattern(p);
  }
  setBpm(bpm);
  swing_ = kSwingMin;
  pattern_ = 0;
  playing_ = false;
  recording_ = false;
  metronome_ = false;
  playStep_ = kSteps - 1;
  lastStepAtMs_ = 0;
}

uint8_t Sequencer::vel(uint8_t pattern, uint8_t step, uint8_t pad) const {
  if (pattern >= kPatterns || step >= kSteps || pad >= kPads) {
    return 0;
  }
  return vel_[pattern][step][pad];
}

void Sequencer::setVel(uint8_t pattern, uint8_t step, uint8_t pad, uint8_t velocity) {
  if (pattern >= kPatterns || step >= kSteps || pad >= kPads) {
    return;
  }
  if (velocity > 127) {
    velocity = 127;
  }
  vel_[pattern][step][pad] = velocity;
}

uint8_t Sequencer::toggleStep(uint8_t step, uint8_t pad, bool cycleAccent) {
  if (step >= kSteps || pad >= kPads) {
    return 0;
  }
  uint8_t current = vel_[pattern_][step][pad];
  uint8_t next;
  if (current == 0) {
    next = kVelNormal;
  } else if (cycleAccent && current < kAccentThreshold) {
    next = kVelAccent;
  } else {
    next = 0;
  }
  vel_[pattern_][step][pad] = next;
  return next;
}

void Sequencer::clearPattern(uint8_t pattern) {
  if (pattern >= kPatterns) {
    return;
  }
  for (uint8_t s = 0; s < kSteps; s++) {
    for (uint8_t p = 0; p < kPads; p++) {
      vel_[pattern][s][p] = 0;
    }
  }
}

uint16_t Sequencer::stepMaskForPad(uint8_t pad) const {
  if (pad >= kPads) {
    return 0;
  }
  uint16_t mask = 0;
  for (uint8_t s = 0; s < kSteps; s++) {
    if (vel_[pattern_][s][pad] != 0) {
      mask |= (uint16_t)1 << s;
    }
  }
  return mask;
}

uint16_t Sequencer::accentMaskForPad(uint8_t pad) const {
  if (pad >= kPads) {
    return 0;
  }
  uint16_t mask = 0;
  for (uint8_t s = 0; s < kSteps; s++) {
    if (vel_[pattern_][s][pad] >= kAccentThreshold) {
      mask |= (uint16_t)1 << s;
    }
  }
  return mask;
}

uint8_t Sequencer::activeCellCount() const {
  uint8_t count = 0;
  for (uint8_t s = 0; s < kSteps; s++) {
    for (uint8_t p = 0; p < kPads; p++) {
      if (vel_[pattern_][s][p] != 0) {
        count++;
      }
    }
  }
  return count;
}

bool Sequencer::setBpm(uint16_t bpm) {
  if (bpm < 40 || bpm > 240) {
    return false;
  }
  bpm_ = bpm;
  return true;
}

bool Sequencer::setSwing(uint8_t pct) {
  if (pct < kSwingMin || pct > kSwingMax) {
    return false;
  }
  swing_ = pct;
  return true;
}

bool Sequencer::setPattern(uint8_t pattern) {
  if (pattern >= kPatterns) {
    return false;
  }
  pattern_ = pattern;
  return true;
}

void Sequencer::play() {
  playStep_ = kSteps - 1;
  lastStepAtMs_ = 0;
  playing_ = true;
}

void Sequencer::stop() {
  playing_ = false;
  lastStepAtMs_ = 0;
}

uint8_t Sequencer::advancePlayStep() {
  playStep_ = (playStep_ + 1) % kSteps;
  return playStep_;
}

uint32_t Sequencer::stepDurationMs(uint8_t step) const {
  // A swing pair is two 16ths; the even-indexed step takes swing% of it.
  uint32_t pairMs = 2 * (60000UL / bpm_ / 4);
  uint32_t evenMs = pairMs * swing_ / 100;
  uint32_t ms = (step % 2 == 0) ? evenMs : pairMs - evenMs;
  return ms == 0 ? 1 : ms;
}

uint32_t Sequencer::stepDurationFrames(uint8_t step, uint32_t rate) const {
  // Locked to a loop: the pair length comes from the loop, not from BPM, so
  // the grid can never drift away from the backing track.
  uint64_t pairFrames = lockedStepFrames_ != 0
                            ? (uint64_t)lockedStepFrames_ * 2
                            : (uint64_t)rate * 2 * 60 / bpm_ / 4;
  uint64_t evenFrames = pairFrames * swing_ / 100;
  uint64_t frames = (step % 2 == 0) ? evenFrames : pairFrames - evenFrames;
  return frames == 0 ? 1 : (uint32_t)frames;
}

void Sequencer::setLockedStepFrames(uint32_t framesPerStep) {
  lockedStepFrames_ = framesPerStep;
}

uint16_t Sequencer::effectiveBpmTenths(uint32_t rate) const {
  if (lockedStepFrames_ == 0 || rate == 0) {
    return (uint16_t)(bpm_ * 10);
  }
  // 16 steps per bar, 4 beats per bar -> a beat is 4 steps.
  uint32_t beatFrames = lockedStepFrames_ * 4;
  if (beatFrames == 0) {
    return (uint16_t)(bpm_ * 10);
  }
  return (uint16_t)(((uint64_t)rate * 600) / beatFrames);
}

bool Sequencer::tickMillis(uint32_t nowMs, StepFireFn fn, void *ctx) {
  if (!playing_) {
    return false;
  }
  if (lastStepAtMs_ != 0 && nowMs - lastStepAtMs_ < stepDurationMs(playStep_)) {
    return false;
  }
  // First tick after play() fires step 0 immediately.
  lastStepAtMs_ = (lastStepAtMs_ == 0) ? nowMs
                                       : lastStepAtMs_ + stepDurationMs(playStep_);
  uint8_t step = advancePlayStep();
  if (fn != nullptr) {
    for (uint8_t pad = 0; pad < kPads; pad++) {
      uint8_t velocity = vel_[pattern_][step][pad];
      if (velocity != 0) {
        fn(ctx, step, pad, velocity);
      }
    }
  }
  return true;
}

uint8_t Sequencer::recordPadMillis(uint8_t pad, uint8_t velocity, uint32_t nowMs) {
  if (pad >= kPads) {
    return 0;
  }
  uint8_t target;
  if (!playing_ || lastStepAtMs_ == 0) {
    target = 0;
  } else {
    uint32_t elapsed = nowMs - lastStepAtMs_;
    target = quantizedStep(elapsed, stepDurationMs(playStep_));
  }
  if (velocity == 0) {
    velocity = kVelNormal;
  }
  vel_[pattern_][target][pad] = velocity;
  return target;
}

uint8_t Sequencer::quantizedStep(uint32_t intoStep, uint32_t stepLength) const {
  uint8_t step = playStep_;
  if (stepLength != 0 && intoStep * 2 >= stepLength) {
    step = (step + 1) % kSteps;
  }
  return step;
}

String Sequencer::patternString() const {
  String row;
  for (uint8_t s = 0; s < kSteps; s++) {
    uint8_t hits = 0;
    uint8_t firstPad = 0;
    for (uint8_t p = 0; p < kPads; p++) {
      if (vel_[pattern_][s][p] != 0) {
        if (hits == 0) {
          firstPad = p;
        }
        hits++;
      }
    }
    if (hits == 0) {
      row += '.';
    } else if (hits == 1) {
      row += padGlyph(firstPad);
    } else {
      row += '+';
    }
  }
  return row;
}

String Sequencer::detailString() const {
  return String("Pattern ") + (char)('A' + pattern_) + " " + patternString() +
         "|Swing " + String(swing_) + "%" +
         "|Step " + String(playStep_ + 1) +
         "|Cells " + String(activeCellCount());
}

void Sequencer::printPattern(Print &out) const {
  out.println(String("[pattern] ") + (char)('A' + pattern_) +
              " bpm=" + String(bpm_) + " swing=" + String(swing_) +
              "% (rows=pads, cols=steps; .=off -=soft ==normal #=accent)");
  for (uint8_t p = 0; p < kPads; p++) {
    String row = String("  ");
    row += padGlyph(p);
    row += " ";
    for (uint8_t s = 0; s < kSteps; s++) {
      row += velocityBucket(vel_[pattern_][s][p]);
      if (s % 4 == 3 && s != kSteps - 1) {
        row += ' ';
      }
    }
    out.println(row);
  }
}
