#ifndef CYPHER_TUNE_SEQUENCER_H
#define CYPHER_TUNE_SEQUENCER_H

#include "../config/ProjectConfig.h"
#include <Arduino.h>

// 4-pattern, 16-step x 16-pad sequencer. Pattern cells are velocity bytes
// (0 = off, 1-127 = velocity), so any combination of pads can land on one
// step. All step/pad/pattern indices in this API are 0-based; serial command
// handlers convert from their 1-based surface at the boundary.
//
// Threading contract: cells and transport scalars are single bytes/halfwords,
// which the P4's RISC-V cores load/store atomically. The loop context (serial
// + touch UI) writes them; the audio render task only reads them plus the
// play-step cursor it owns while running. The millis fallback clock below is
// used only when the audio engine is not running, so the two clocks never
// race each other.
class Sequencer {
 public:
  static const uint8_t kSteps = 16;
  static const uint8_t kPads = 16;
  static const uint8_t kPatterns = 4;
  static const uint8_t kVelNormal = 100;
  static const uint8_t kVelAccent = 127;
  // Velocities at or above this render as accents in the UI/pattern printer.
  static const uint8_t kAccentThreshold = 120;
  // MPC swing convention: first 16th of each pair takes swing% of the pair.
  static const uint8_t kSwingMin = 50;  // straight
  static const uint8_t kSwingMax = 75;

  typedef void (*StepFireFn)(void *ctx, uint8_t step, uint8_t pad, uint8_t velocity);

  void begin(uint16_t bpm);

  // Pattern grid.
  uint8_t vel(uint8_t pattern, uint8_t step, uint8_t pad) const;
  void setVel(uint8_t pattern, uint8_t step, uint8_t pad, uint8_t velocity);
  // Current pattern: off -> on (normal). With cycleAccent, off -> normal ->
  // accent -> off (the touch UI's tri-state tap). Returns the new velocity.
  uint8_t toggleStep(uint8_t step, uint8_t pad, bool cycleAccent = false);
  void clearPattern(uint8_t pattern);
  // Current pattern, one pad's lane as bitmasks (bit i = step i).
  uint16_t stepMaskForPad(uint8_t pad) const;
  uint16_t accentMaskForPad(uint8_t pad) const;
  uint8_t activeCellCount() const;  // current pattern

  // Transport state. Setters return false when out of range.
  uint16_t bpm() const { return bpm_; }
  bool setBpm(uint16_t bpm);
  uint8_t swing() const { return swing_; }
  bool setSwing(uint8_t pct);
  uint8_t pattern() const { return pattern_; }
  bool setPattern(uint8_t pattern);
  bool playing() const { return playing_; }
  bool recording() const { return recording_; }
  bool metronome() const { return metronome_; }
  void setMetronome(bool on) { metronome_ = on; }
  void toggleRecord() { recording_ = !recording_; }
  uint8_t playStep() const { return playStep_; }

  // Transport control. In audio builds the engine's render task calls these
  // when it consumes PLAY/STOP commands; in baseline builds the loop calls
  // them directly ahead of tickMillis().
  void play();
  void stop();

  // Owned by whichever clock is active: advances the play-step cursor and
  // returns the new step index.
  uint8_t advancePlayStep();

  // Step durations under swing, in milliseconds (millis clock) or frames
  // (audio clock; rate = engine sample rate). Even-indexed steps stretch,
  // odd-indexed steps shrink, pairs keep constant length.
  uint32_t stepDurationMs(uint8_t step) const;
  uint32_t stepDurationFrames(uint8_t step, uint32_t rate) const;

  // Millis fallback clock; call every loop() when the audio engine is not
  // running. Fires fn once per active pad when a step boundary passes and
  // returns true on step advance (even if the step is empty, so the caller
  // can move a playhead).
  bool tickMillis(uint32_t nowMs, StepFireFn fn, void *ctx);
  // Record a live pad hit into the current pattern, quantized to the nearest
  // step of the millis clock (>=50% into the step rounds to the next).
  uint8_t recordPadMillis(uint8_t pad, uint8_t velocity, uint32_t nowMs);
  // Same quantize decision for the audio clock: framesIntoStep against the
  // current step's duration. Returns the target step.
  uint8_t quantizedStep(uint32_t framesIntoStep, uint32_t stepFrames) const;

  // Serial/dashboard renderings.
  String patternString() const;   // one row: '.'=empty, pad glyph, '+'=multi
  String detailString() const;
  void printPattern(Print &out) const;  // 16 pad rows x 16 velocity buckets

 private:
  volatile uint8_t vel_[kPatterns][kSteps][kPads];
  volatile uint16_t bpm_ = 92;
  volatile uint8_t swing_ = kSwingMin;
  volatile uint8_t pattern_ = 0;
  volatile bool playing_ = false;
  volatile bool recording_ = false;
  volatile bool metronome_ = false;
  volatile uint8_t playStep_ = kSteps - 1;
  uint32_t lastStepAtMs_ = 0;  // millis clock only
};

#endif
