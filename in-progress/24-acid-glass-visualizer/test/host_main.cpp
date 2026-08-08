#include <cstdint>
#include <iostream>

#include "../src/AcidGlassLogic.h"

int checks = 0;
int failures = 0;

void check(bool condition, const char *message) {
  checks++;
  if (!condition) {
    failures++;
    std::cerr << "FAIL: " << message << '\n';
  }
}

int main() {
  using namespace AcidGlassLogic;
  check(wrapIndex(-1, 12) == 11, "scene wraps backward");
  check(wrapIndex(12, 12) == 0, "scene wraps forward");
  check(wrapIndex(5, 12) == 5, "scene preserves in-range index");
  check(wrapIndex(9, 0) == 0, "empty catalog is safe");

  check(mapCoordinate(-10, 1023) == 0, "touch clamps below panel");
  check(mapCoordinate(0, 1023) == 0, "touch maps origin");
  check(mapCoordinate(1023, 1023) == 255, "touch maps far edge");
  check(mapCoordinate(512, 1023) >= 127 && mapCoordinate(512, 1023) <= 128,
        "touch maps panel center");

  check(validPcmFormat(1, 1, 16, 8000), "minimum mono PCM accepted");
  check(validPcmFormat(1, 2, 16, 48000), "maximum stereo PCM accepted");
  check(!validPcmFormat(3, 2, 16, 44100), "float WAV rejected");
  check(!validPcmFormat(1, 4, 16, 44100), "multichannel WAV rejected");
  check(!validPcmFormat(1, 2, 24, 44100), "24-bit WAV rejected");
  check(!validPcmFormat(1, 2, 16, 7999), "too-low sample rate rejected");
  check(!validPcmFormat(1, 2, 16, 48001), "too-high sample rate rejected");

  check(resampleStepQ16(44100) == 65536, "44.1 kHz is unity Q16");
  check(resampleStepQ16(22050) == 32768, "22.05 kHz is half-step Q16");
  check(resampleStepQ16(48000) > 65536, "48 kHz consumes faster than output");
  check(resampleStepQ16(0) == 0, "zero source rate is safe");

  check(smoothByte(0, 255, 72) > 0 && smoothByte(0, 255, 72) < 255,
        "smoothing advances without snapping");
  check(smoothByte(128, 128) == 128, "smoothing preserves settled value");
  check(smoothByte(1, 0, 1) == 0, "smoothing converges at one-byte distance");
  check(adaptiveTargetFps(30000, 45, 0, 0) == 30, "slow auto frame drops to 30 FPS");
  check(adaptiveTargetFps(23000, 45, 0, 0) == 36, "moderate auto frame drops to 36 FPS");
  check(adaptiveTargetFps(15000, 30, 149, 0) == 30, "auto pacing waits before recovering");
  check(adaptiveTargetFps(15000, 30, 150, 0) == 36, "auto pacing recovers with hysteresis");
  check(adaptiveTargetFps(12000, 36, 0, 1) == 30, "performance mode locks 30 FPS");
  check(adaptiveTargetFps(35000, 30, 0, 2) == 45, "quality mode requests 45 FPS");
  check(qualityTierFor(30, 0) == 0, "30 FPS maps to performance quality tier");
  check(qualityTierFor(36, 0) == 1, "36 FPS maps to balanced quality tier");
  check(qualityTierFor(45, 0) == 2, "45 FPS maps to high quality tier");
  check(qualityTierFor(45, 1) == 0, "performance mode forces low cost tier");

  for (int i = -100; i <= 100; ++i) {
    uint8_t wrapped = wrapIndex(i, 12);
    check(wrapped < 12, "wrapped scene always stays in catalog");
  }
  for (int x = -50; x <= 1100; ++x) {
    uint8_t mapped = mapCoordinate(x, 1023);
    check(mapped <= 255, "mapped touch always stays byte-sized");
  }

  std::cout << "Acid Glass host checks: " << checks << ", failures: " << failures << '\n';
  return failures == 0 ? 0 : 1;
}
