// Auto-exposure / auto-white-balance control loops.
//
// HARDWARE-VERIFIED (V1.2 panel, 2026-07-24): the loops converge and the image
// auto-colours correctly on a live indoor scene.
//
// Still untested: behaviour across a wide brightness range. "Looks right
// indoors" is not "converges properly walking from a dim room to a window",
// and kDefaultTarget has not been tuned against a measured reference.
//
// Getting here took fixing two bugs that only a real image could reveal - a
// missing black level correction (hazy, grey blacks) and an AWB acceptance
// window too narrow to ever see an uncorrected frame. See risk register #26
// and #27; the second is the more instructive of the two.
//
// A REAL BUG FOUND ON HARDWARE, worth remembering: these statistics reads block
// the calling task until the ISP raises its interrupt. Originally both ran every
// update with a 120 ms timeout, i.e. up to 240 ms of stall per 200 ms window.
// Average frame rate still looked fine (20-30 fps) because the loop ran fast
// between stalls - but taps landing inside a stall were swallowed, and the
// touchscreen appeared broken. Averages hid it; only the worst case mattered.
// Hence the short timeout and the one-read-per-update alternation in tick().

#include "CamPipeline.h"

// SC2336 analog gain ladder. The gain field at 0x3e09 is coarse/fine encoded,
// not a linear multiplier, so the loop walks known-good register values rather
// than computing one. Roughly 1x, 2x, 4x, 8x, 16x - ascending noise.
const uint8_t CamPipeline::kGainLadder[] = {0x00, 0x01, 0x03, 0x07, 0x0F, 0x1F};
const uint8_t CamPipeline::kGainLadderSize =
    sizeof(CamPipeline::kGainLadder) / sizeof(CamPipeline::kGainLadder[0]);

void CamPipeline::begin() {
  lastUpdateMs_ = millis();
  gainIndex_ = 0;
  settleCount_ = 0;
  smoothedRedGain_ = 1.0f;
  smoothedBlueGain_ = 1.0f;
}

void CamPipeline::setAutoExposure(bool on) {
  autoExposure_ = on;
  settleCount_ = 0;
  Logger::info("ae", on ? "auto exposure on" : "auto exposure off (manual)");
}

void CamPipeline::setAutoWhiteBalance(bool on) {
  autoWhiteBalance_ = on;
  if (!on) resetWhiteBalance();
  Logger::info("awb", on ? "auto white balance on" : "auto white balance off");
}

void CamPipeline::resetWhiteBalance() {
  smoothedRedGain_ = 1.0f;
  smoothedBlueGain_ = 1.0f;
  CrowCamera::setColorGains(1.0f, 1.0f, 1.0f);
}

void CamPipeline::updateExposure_() {
  uint32_t luma = 0;
  if (!CrowCamera::meanLuminance(luma)) return;
  lastLuma_ = luma;

  const int32_t error = (int32_t)target_ - (int32_t)luma;
  if ((uint32_t)abs(error) <= kDeadband) {
    if (settleCount_ < kSettleUpdates) settleCount_++;
    return;
  }
  settleCount_ = 0;

  Sc2336Sensor *sensor = CrowCamera::sensor();
  if (sensor == nullptr) return;

  const uint32_t current = sensor->exposure() ? sensor->exposure() : 1;
  const uint32_t maxExposure = Sc2336Sensor::maxExposure();

  // Proportional in the ratio domain, because exposure is multiplicative:
  // doubling the shutter doubles the light. Working in ratios means the same
  // damping constant behaves the same at both ends of the range.
  //
  // Guard the divide: a luma of 0 (pitch black, or a stalled sensor) would
  // otherwise produce an infinite ratio.
  const float ratio = (luma > 0) ? ((float)target_ / (float)luma) : 2.0f;
  const float damped = 1.0f + (ratio - 1.0f) * kDamping;
  // Never move more than 4x in one step, however far off the scene is - a
  // single huge jump is what makes a viewfinder flash white.
  const float clamped = damped < 0.25f ? 0.25f : (damped > 4.0f ? 4.0f : damped);

  uint32_t next = (uint32_t)((float)current * clamped);
  if (next < 1) next = 1;

  if (next > maxExposure) {
    // Shutter is at its ceiling and the image is still dark. Only now is gain
    // worth spending, because it amplifies noise along with signal.
    next = maxExposure;
    if (gainIndex_ + 1 < kGainLadderSize) {
      gainIndex_++;
      sensor->setAnalogGain(kGainLadder[gainIndex_]);
    }
  } else if (error < 0 && gainIndex_ > 0 && next < maxExposure / 2) {
    // Plenty of light and room to shorten the shutter: back the gain off first
    // so the image gets cleaner, not just darker.
    gainIndex_--;
    sensor->setAnalogGain(kGainLadder[gainIndex_]);
    return;
  }

  sensor->setExposure(next);
}

void CamPipeline::updateWhiteBalance_() {
  float redGain = 1.0f;
  float blueGain = 1.0f;
  uint32_t patches = 0;

  // A false return means the scene had too few neutral references to judge.
  // Keep the previous correction rather than drifting toward a bad estimate -
  // a stable slightly-wrong tint is much less objectionable than a colour cast
  // that wanders as the camera moves.
  if (!CrowCamera::whiteBalanceEstimate(redGain, blueGain, patches)) {
    lastPatches_ = patches;
    return;
  }
  lastPatches_ = patches;

  // The AWB engine samples AFTER the CCM, so it is measuring the result of the
  // correction already applied. The estimate is therefore a RESIDUAL error to
  // fold into the existing gains, not an absolute answer to replace them.
  float currentR = 1.0f, currentG = 1.0f, currentB = 1.0f;
  CrowCamera::colorGains(currentR, currentG, currentB);

  const float wantR = currentR * redGain;
  const float wantB = currentB * blueGain;

  // Exponential smoothing. Same reasoning as the exposure damping: converge
  // over a few updates instead of snapping and oscillating.
  smoothedRedGain_ += (wantR - smoothedRedGain_) * kDamping;
  smoothedBlueGain_ += (wantB - smoothedBlueGain_) * kDamping;

  // Green stays at unity and is the reference the other two are measured
  // against; scaling all three would change exposure, which is the AE loop's
  // job and not this one's.
  CrowCamera::setColorGains(smoothedRedGain_, 1.0f, smoothedBlueGain_);
}

void CamPipeline::tick() {
  if (!CrowCamera::ready() || !CrowCamera::streaming()) return;

  const uint32_t now = millis();
  if (now - lastUpdateMs_ < kUpdateIntervalMs) return;
  lastUpdateMs_ = now;

  // AT MOST ONE statistics read per update, alternating between the two loops.
  //
  // Both reads block the calling task until the ISP raises its statistics
  // interrupt. Running them back to back doubles the worst-case stall, and this
  // runs inside the render loop - where the cost is not just frame rate but
  // TOUCH: a tap needs at least two touch samples to produce a release edge, so
  // a loop that stalls long enough will silently swallow taps and look like a
  // dead touchscreen. Alternating halves the worst case and costs nothing but a
  // slower convergence that no one can perceive at 2.5 Hz per loop.
  alternateStats_ = !alternateStats_;
  if (alternateStats_) {
    if (autoExposure_) updateExposure_();
  } else {
    if (autoWhiteBalance_) updateWhiteBalance_();
  }
}

void CamPipeline::printStatus(Print &out) const {
  out.print(F("[ae] "));
  out.print(autoExposure_ ? F("auto") : F("manual"));
  out.print(F(" luma="));
  out.print(lastLuma_);
  out.print('/');
  out.print(target_);
  out.print(F(" gain_step="));
  out.print(gainIndex_);
  out.print('/');
  out.print(kGainLadderSize - 1);
  out.print(F(" "));
  out.println(converged() ? F("converged") : F("hunting"));

  float r = 1.0f, g = 1.0f, b = 1.0f;
  CrowCamera::colorGains(r, g, b);
  out.print(F("[awb] "));
  out.print(autoWhiteBalance_ ? F("auto") : F("off"));
  out.print(F(" white_patches="));
  out.print(lastPatches_);
  out.print(F(" ccm_gains r="));
  out.print(r, 2);
  out.print(F(" g="));
  out.print(g, 2);
  out.print(F(" b="));
  out.println(b, 2);
}
