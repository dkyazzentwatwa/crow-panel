#ifndef ACID_GLASS_APP_H
#define ACID_GLASS_APP_H

#include "../config/ProjectConfig.h"
#include "AcidGlassAudio.h"
#include "AcidGlassRemote.h"
#include "AcidGlassTypes.h"
#include "AcidGlassVisuals.h"

class Print;

class AcidGlassApp {
 public:
  void begin();
  void tick();
  bool control(const ControlEvent &event);
  void printStatus(Print &out) const;
  void printScenes(Print &out) const;
  void printPalettes(Print &out) const;
  void printTracks(Print &out) const;
  void printPresets(Print &out) const;
  void printTouch(Print &out) const;
  void benchmark(Print &out, uint16_t seconds);
  const AcidGlassState &state() const { return state_; }

 private:
  struct StoredState {
    uint32_t magic = 0x41434944;
    uint16_t version = 1;
    AcidGlassState state;
  };

  AcidGlassState state_;
  AcidGlassState renderState_;
  AcidGlassOverlay overlay_;
  AcidGlassVisuals visuals_;
  AcidGlassAudio audio_;
  AcidGlassRemote remote_;
  bool displayReady_ = false;
  bool visualsReady_ = false;
  const char *renderError_ = "not started";
  bool touchDown_ = false;
  uint8_t lastTouchCount_ = 0;
  int16_t touchStartX_ = 0, touchStartY_ = 0;
  int16_t lastTouchX_ = 0, lastTouchY_ = 0;
  int16_t pinchStartDistance_ = 0;
  uint8_t pinchStartZoom_ = 128;
  uint32_t touchStartedMs_ = 0;
  uint32_t lastRenderMs_ = 0;
  uint32_t stableFrameCount_ = 0;
  uint32_t droppedFrames_ = 0;
  uint32_t transitionStartedMs_ = 0;
  uint8_t targetFps_ = 30;
  uint8_t qualityTier_ = 0;
  uint32_t lastDemoChangeMs_ = 0;
  uint32_t lastInteractionMs_ = 0;
  uint32_t lastSaveMs_ = 0;
  bool savePending_ = false;
  uint8_t drawer_ = kAcidSheetClosed;
  int8_t activePreset_ = -1;
  uint16_t userPresetMask_ = 0;
  uint8_t presetPalettes_[kAcidPresetCount] = {};
  char presetMessage_[40] = {};
  uint32_t presetMessageUntil_ = 0;
  uint32_t randomState_ = 0xAC1D6124;

  static bool remoteControl_(void *context, const ControlEvent &event);
  void handleTouch_();
  bool handleDrawerTouch_(int16_t x, int16_t y, uint32_t heldMs);
  bool updateDrawerDrag_(int16_t x, int16_t y);
  void updateRenderState_();
  void buildOverlay_(uint32_t nowMs);
  void noteFrame_();
  void startTransition_();
  void drawHud_();
  void drawRenderError_();
  void drawDrawer_();
  void nextScene_(int8_t direction, ControlSource source);
  void nextPalette_(int8_t direction, ControlSource source);
  void randomize_(ControlSource source);
  bool setParameter_(const char *key, int32_t value);
  void markChanged_(ControlSource source);
  void loadState_();
  void saveState_();
  bool savePreset_(uint8_t slot);
  bool loadPreset_(uint8_t slot, ControlSource source);
  void scanPresets_();
  void showPresetMessage_(const char *action, uint8_t slot);
};

#endif
