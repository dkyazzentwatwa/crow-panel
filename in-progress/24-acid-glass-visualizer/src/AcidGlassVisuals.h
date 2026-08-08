#ifndef ACID_GLASS_VISUALS_H
#define ACID_GLASS_VISUALS_H

#include "../config/ProjectConfig.h"
#include "AcidGlassTypes.h"

class Print;

class AcidGlassVisuals {
 public:
  static constexpr int16_t kWidth = 256;
  static constexpr int16_t kHeight = 150;

  bool begin();
  bool render(const AcidGlassState &state, const AudioFeatures &audio,
              const AcidGlassOverlay &overlay, uint32_t nowMs);
  bool renderBringupProof(uint32_t nowMs);
  bool proofComplete(uint32_t nowMs) const { return nowMs - proofStartedMs_ >= 2800; }
  void printStatus(Print &out) const;

  static const char *sceneName(uint8_t scene);
  static const char *paletteName(uint8_t palette);
  static uint16_t paletteColor(uint8_t palette, uint8_t value, uint8_t hue = 0);
  static int8_t sceneIndex(const String &name);
  static int8_t paletteIndex(const String &name);

  uint32_t frameCount() const { return frameCount_; }
  uint16_t fps() const { return fps_; }
  uint32_t lastFrameUs() const { return lastFrameUs_; }
  uint32_t lastPresentUs() const { return lastPresentUs_; }
  void setPresentationTelemetry(uint8_t targetFps, uint8_t qualityTier,
                                uint32_t droppedFrames, bool uiDirty) {
    targetFps_ = targetFps;
    qualityTier_ = qualityTier;
    droppedFrames_ = droppedFrames;
    uiDirty_ = uiDirty;
  }
  uint8_t targetFps() const { return targetFps_; }
  uint8_t qualityTier() const { return qualityTier_; }
  uint32_t droppedFrames() const { return droppedFrames_; }
  bool uiDirty() const { return uiDirty_; }
  bool ppaReady() const { return ppaReady_; }
  bool ppaRequested() const { return ppaRequested_; }
  uint32_t ppaFailures() const { return ppaFailures_; }
  bool internalBuffer() const { return internalBuffer_; }
  bool ready() const { return frame_ != nullptr; }
  bool feedbackReady() const { return feedback_ != nullptr; }
  const char *lastError() const { return lastError_; }

 private:
  uint16_t *frame_ = nullptr;
  uint16_t *feedback_ = nullptr;
  void *ppa_ = nullptr;
  bool ppaReady_ = false;
  bool ppaRequested_ = ACID_GLASS_USE_PPA != 0;
  uint32_t ppaFailures_ = 0;
  int32_t ppaLastError_ = 0;
  bool internalBuffer_ = false;
  uint8_t sine_[256] = {};
  uint32_t frameCount_ = 0;
  uint32_t fpsWindowMs_ = 0;
  uint16_t fpsWindowFrames_ = 0;
  uint16_t fps_ = 0;
  uint32_t lastFrameUs_ = 0;
  uint32_t lastPresentUs_ = 0;
  uint8_t renderQuality_ = 0;
  uint8_t targetFps_ = 30;
  uint8_t qualityTier_ = 0;
  uint32_t droppedFrames_ = 0;
  bool uiDirty_ = true;
  uint32_t proofStartedMs_ = 0;
  const char *lastError_ = "not started";
  uint32_t rng_ = 0xA51D6A55;

  uint8_t wave_(uint16_t phase) const { return sine_[phase & 0xFF]; }
  uint8_t field_(uint8_t scene, int16_t x, int16_t y, uint32_t tick,
                 const AcidGlassState &state, const AudioFeatures &audio);
  uint16_t color_(uint8_t value, uint8_t palette, uint8_t hue) const;
  void renderAscii_(uint32_t tick, const AcidGlassState &state, const AudioFeatures &audio);
  void renderAttractor_(uint32_t tick, const AcidGlassState &state, const AudioFeatures &audio);
  void renderPixelMelt_(uint32_t tick, const AcidGlassState &state, const AudioFeatures &audio);
  void renderScopeGarden_(uint32_t tick, const AcidGlassState &state,
                          const AudioFeatures &audio);
  void blendFeedback_(uint8_t amount);
  void composeOverlay_(const AcidGlassState &state, const AcidGlassOverlay &overlay);
  void fillRect_(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
  void line_(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color);
  void text_(int16_t x, int16_t y, const char *text, uint16_t color);
  void number_(int16_t x, int16_t y, uint32_t value, uint16_t color);
  void slider_(int16_t y, const char *label, uint8_t value, uint16_t color);
  bool blit_();
};

#endif
