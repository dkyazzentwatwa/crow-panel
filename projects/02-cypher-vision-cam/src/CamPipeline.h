#ifndef VISION_CAM_PIPELINE_H
#define VISION_CAM_PIPELINE_H

#include "../config/ProjectConfig.h"
#include <Arduino.h>
#include <CrowPanelShared.h>

// Auto-exposure and auto-white-balance policy.
//
// This class exists because of a gap, and the gap is worth naming: Arduino core
// 3.3.8 ships the ISP's statistics ENGINES (they measure luminance and find
// white patches in hardware) but not esp_video's software pipeline controller,
// which is the piece that normally reads those measurements and decides what to
// do. Under ESP-IDF you get that for free. Here it has to be written, and this
// is it.
//
// Without it, exposure is whatever the sensor's mode table left behind: a fixed
// shutter that is roughly right in one lighting condition and badly wrong in
// every other. Point the camera at a window and the image blows out; point it
// at a dim room and it goes black.
//
// Design notes:
//   - Exposure moves BEFORE gain. Gain amplifies noise; a longer shutter does
//     not. Gain only comes up once the shutter is at its ceiling.
//   - Corrections are proportional and damped, not step-to-target. A loop that
//     jumps straight to the computed answer oscillates visibly, and a pumping
//     viewfinder looks broken even when the average exposure is right.
//   - There is a deadband. Without one the loop hunts forever on sensor noise.
//   - Manual mode is a first-class state, not a failure mode. If the automatic
//     loop misbehaves on real hardware, manual exposure still produces a usable
//     camera, which is why the UI exposes it directly.

class CamPipeline {
 public:
  void begin();

  // Call once per loop(). Cheap: it only acts on its own schedule (a few Hz),
  // because exposure statistics are a one-shot read that costs a frame-time
  // wait and there is nothing to gain from running it faster than the scene
  // changes.
  void tick();

  // --- Auto exposure -------------------------------------------------------
  void setAutoExposure(bool on);
  bool autoExposure() const { return autoExposure_; }

  // Target mean luminance, in whatever units the AE engine reports. The units
  // are not documented in the public headers, so this is calibrated by eye
  // against a real scene rather than derived - see kDefaultTarget.
  void setTargetLuminance(uint32_t target) { target_ = target; }
  uint32_t targetLuminance() const { return target_; }
  uint32_t lastLuminance() const { return lastLuma_; }

  // True once the loop has been inside the deadband for several consecutive
  // updates. Surfaced so the UI can say "converged" rather than implying the
  // exposure is final while it is still hunting.
  bool converged() const { return settleCount_ >= kSettleUpdates; }

  // --- Auto white balance --------------------------------------------------
  void setAutoWhiteBalance(bool on);
  bool autoWhiteBalance() const { return autoWhiteBalance_; }
  uint32_t lastWhitePatches() const { return lastPatches_; }

  // Drops the CCM back to unity gain and stops correcting.
  void resetWhiteBalance();

  void printStatus(Print &out) const;

 private:
  // Luminance must be this far off target before the loop reacts. Sized to sit
  // above the frame-to-frame noise in the AE statistics.
  static constexpr uint32_t kDeadband = 6;

  // Fraction of the computed correction actually applied per update. 0.35 was
  // chosen to converge in a handful of updates without visible pumping; it is
  // the main knob if the loop looks sluggish or jumpy on hardware.
  static constexpr float kDamping = 0.35f;

  // Consecutive in-deadband updates before the loop reports converged.
  static constexpr uint8_t kSettleUpdates = 3;

  // Update period. Slower than the frame rate on purpose: each update costs a
  // one-shot statistics read, and human-perceptible lighting changes happen far
  // slower than 30 Hz.
  static constexpr uint32_t kUpdateIntervalMs = 200;

  // Starting target. NOT derived from a datasheet - the AE engine's luminance
  // units are undocumented, so this is a first guess to be corrected against a
  // real scene. It is a settable field precisely because it needs tuning.
  static constexpr uint32_t kDefaultTarget = 110;

  // Analog gain register values the loop steps through, lowest noise first.
  // The SC2336's gain field is coarse/fine encoded rather than linear, so the
  // loop walks a known-good ladder instead of computing a value.
  static const uint8_t kGainLadder[];
  static const uint8_t kGainLadderSize;

  void updateExposure_();
  void updateWhiteBalance_();

  bool autoExposure_ = true;
  bool autoWhiteBalance_ = true;
  uint32_t target_ = kDefaultTarget;
  uint32_t lastLuma_ = 0;
  uint32_t lastPatches_ = 0;
  uint8_t gainIndex_ = 0;
  uint8_t settleCount_ = 0;
  uint32_t lastUpdateMs_ = 0;
  // White-balance gains are smoothed across updates; a single frame's estimate
  // is too jumpy to apply directly.
  float smoothedRedGain_ = 1.0f;
  float smoothedBlueGain_ = 1.0f;
};

#endif
