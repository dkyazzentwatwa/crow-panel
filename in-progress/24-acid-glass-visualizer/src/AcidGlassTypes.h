#ifndef ACID_GLASS_TYPES_H
#define ACID_GLASS_TYPES_H

#include <Arduino.h>

constexpr uint8_t kAcidSceneCount = 12;
constexpr uint8_t kAcidPaletteCount = 12;
constexpr uint8_t kAcidBandCount = 8;
constexpr uint8_t kAcidPresetCount = 16;

enum class AcidQualityMode : uint8_t {
  kAuto = 0,
  kPerformance = 1,
  kQuality = 2,
};

constexpr uint8_t kAcidSheetClosed = 0xFF;

struct AudioFeatures {
  uint8_t peak = 0;
  uint8_t rms = 0;
  uint8_t onset = 0;
  uint8_t bands[kAcidBandCount] = {};
  uint32_t sequence = 0;
};

struct VisualParams {
  uint8_t speed = 150;
  uint8_t zoom = 128;
  uint8_t intensity = 210;
  uint8_t warp = 128;
  uint8_t feedback = 96;
  uint8_t trails = 112;
  uint8_t symmetry = 4;
  uint8_t pixelScale = 1;
  uint8_t hueRate = 80;
  uint8_t audioSensitivity = 170;
  uint8_t macroX = 128;
  uint8_t macroY = 128;
};

struct AcidGlassState {
  uint8_t scene = 0;
  uint8_t palette = 0;
  uint8_t brightness = 220;
  uint8_t volume = 70;
  bool demo = true;
  bool frozen = false;
  bool hud = true;
  bool safeFlash = true;
  AcidQualityMode qualityMode = AcidQualityMode::kAuto;
  VisualParams visual;
};

// Everything needed to paint the pixel cockpit. It deliberately contains
// values, not driver pointers, so rendering remains one source-buffer pass.
struct AcidGlassOverlay {
  bool hud = true;
  uint8_t sheet = kAcidSheetClosed;
  uint8_t targetFps = 30;
  uint8_t qualityTier = 0;
  uint32_t presentUs = 0;
  uint32_t droppedFrames = 0;
  bool ppaReady = false;
  bool ppaRequested = false;
  bool sdReady = false;
  bool audioPlaying = false;
  bool remoteReady = false;
  uint8_t transition = 255;
  char toast[40] = {};
};

enum class ControlSource : uint8_t {
  kTouch,
  kSerial,
  kRemote,
  kDemo,
};

struct ControlEvent {
  ControlSource source = ControlSource::kSerial;
  char action[16] = {};
  char key[20] = {};
  int32_t value = 0;
};

inline uint8_t clampByte(int32_t value) {
  if (value < 0) return 0;
  if (value > 255) return 255;
  return static_cast<uint8_t>(value);
}

#endif
