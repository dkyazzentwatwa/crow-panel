#include "AcidGlassApp.h"
#include "AcidGlassLogic.h"

#include <CrowPanelShared.h>
#include <Preferences.h>
#include <string.h>

#if USE_DISPLAY
#include <Arduino_GFX_Library.h>
#endif

namespace {

constexpr uint32_t kStoredMagic = 0x41434944;  // ACID
constexpr uint16_t kStoredVersion = 1;
constexpr uint32_t kDemoSceneMs = 8000;
constexpr uint32_t kSaveDelayMs = 1500;
constexpr int16_t kSwipeDistance = 80;

struct FactoryPreset {
  const char *name;
  uint8_t scene;
  uint8_t palette;
  uint8_t speed;
  uint8_t zoom;
  uint8_t intensity;
  uint8_t warp;
  uint8_t feedback;
  uint8_t trails;
  uint8_t symmetry;
  uint8_t pixelScale;
  uint8_t hueRate;
  uint8_t macroX;
  uint8_t macroY;
};

const FactoryPreset kFactoryPresets[kAcidPresetCount] = {
    {"VIOLET CODE", 0, 0, 146, 132, 224, 128, 92, 118, 4, 1, 72, 128, 128},
    {"TOXIC RAIN", 1, 1, 188, 110, 232, 176, 70, 176, 3, 2, 108, 78, 166},
    {"MOLTEN CORE", 2, 2, 172, 158, 246, 204, 82, 132, 6, 1, 44, 126, 116},
    {"LIME MIRROR", 3, 3, 116, 138, 218, 104, 116, 168, 10, 1, 64, 168, 92},
    {"OCEAN BLOBS", 4, 4, 126, 112, 236, 88, 104, 154, 5, 1, 52, 104, 150},
    {"SOLAR CELLS", 5, 5, 96, 148, 228, 142, 152, 202, 6, 2, 34, 154, 102},
    {"CANDY CHAOS", 6, 6, 202, 104, 250, 194, 126, 218, 8, 1, 126, 72, 184},
    {"RED SIGNAL", 7, 7, 154, 128, 238, 220, 88, 142, 7, 2, 48, 192, 64},
    {"ICE MELT", 8, 8, 136, 116, 232, 124, 174, 224, 4, 2, 58, 120, 176},
    {"STAR DUST", 9, 9, 218, 166, 248, 156, 66, 118, 5, 1, 96, 146, 110},
    {"WHITE SCOPE", 10, 10, 104, 128, 222, 64, 138, 226, 4, 1, 22, 128, 128},
    {"RGB BLOOM", 11, 11, 148, 176, 250, 182, 112, 184, 9, 1, 116, 98, 146},
    {"LASER TEMPLE", 3, 0, 184, 122, 244, 232, 80, 152, 12, 2, 92, 212, 66},
    {"DEEP DREAM", 11, 4, 88, 208, 214, 118, 188, 238, 7, 1, 28, 82, 194},
    {"NEON HEART", 4, 1, 164, 138, 252, 154, 118, 196, 6, 1, 88, 170, 116},
    {"INSTALLATION", 6, 8, 72, 154, 220, 86, 206, 244, 8, 2, 18, 128, 128},
};

AcidGlassState factoryState(uint8_t slot) {
  const FactoryPreset &preset = kFactoryPresets[slot % kAcidPresetCount];
  AcidGlassState state;
  state.scene = preset.scene;
  state.palette = preset.palette;
  state.demo = false;
  state.frozen = false;
  state.visual.speed = preset.speed;
  state.visual.zoom = preset.zoom;
  state.visual.intensity = preset.intensity;
  state.visual.warp = preset.warp;
  state.visual.feedback = preset.feedback;
  state.visual.trails = preset.trails;
  state.visual.symmetry = preset.symmetry;
  state.visual.pixelScale = preset.pixelScale;
  state.visual.hueRate = preset.hueRate;
  state.visual.macroX = preset.macroX;
  state.visual.macroY = preset.macroY;
  return state;
}

uint16_t dimColor(uint16_t color, uint8_t amount) {
  uint16_t r = ((color >> 11) & 31) * amount / 255;
  uint16_t g = ((color >> 5) & 63) * amount / 255;
  uint16_t b = (color & 31) * amount / 255;
  return static_cast<uint16_t>((r << 11) | (g << 5) | b);
}

void bootMark(const char *text, int16_t y) {
#if USE_DISPLAY
  Arduino_GFX *g = CrowDisplay::canvas();
  if (g == nullptr) return;
  g->setTextSize(2);
  g->setTextColor(0xFFFF);
  g->setCursor(24, y);
  g->print(text);
  CrowDisplay::flush();
#else
  (void)text;
  (void)y;
#endif
}

}  // namespace

void AcidGlassApp::begin() {
#if ACID_GLASS_BRINGUP_VISUAL_ONLY
  state_ = AcidGlassState{};
#else
  loadState_();
#endif
  // Freeze is a live performance control, never a boot state. Persisting it
  // can make a healthy renderer look dead forever after a reboot.
  state_.frozen = false;
  scanPresets_();
  renderState_ = state_;
#if !ACID_GLASS_BRINGUP_VISUAL_ONLY
  audio_.mountAndIndex();  // SD must settle before the DSI framebuffer starts.
#endif
#if USE_DISPLAY
  displayReady_ = CrowDisplay::begin(activeHardwareProfile(), "ACID GLASS", true);
  bootMark("BOOT 1: DISPLAY OK", 96);
#endif
  visualsReady_ = visuals_.begin();
  if (!visualsReady_) {
    Logger::error("acid-glass", "visual buffer allocation failed");
    bootMark("BOOT 2: VISUAL BUFFER FAILED", 132);
    bootMark(visuals_.lastError(), 168);
  } else {
    bootMark(visuals_.feedbackReady() ? "BOOT 2: VISUAL BUFFER OK" : "BOOT 2: VISUAL DRY MODE", 132);
  }
#if !ACID_GLASS_BRINGUP_VISUAL_ONLY
  audio_.setVolume(state_.volume);
  audio_.setSensitivity(state_.visual.audioSensitivity);
  const bool audioReady = audio_.begin();
#if USE_ACID_GLASS_AUDIO
  bootMark(audioReady ? "BOOT 3: AUDIO READY" : "BOOT 3: AUDIO FAILED", 168);
#elif USE_ACID_GLASS_SD
  bootMark(audio_.sdReady() ? "BOOT 3: SD ON / AUDIO OFF" : "BOOT 3: SD OFF / AUDIO OFF", 168);
#else
  bootMark("BOOT 3: AUDIO DISABLED", 168);
#endif
#else
  bootMark("BOOT 3: VISUAL-ONLY", 168);
#endif
  if (state_.brightness < 40) state_.brightness = 40;
#if USE_DISPLAY
  CrowDisplay::setBacklight(state_.brightness);
#endif
#if ACID_GLASS_REMOTE_AUTOSTART
  remote_.begin(this, remoteControl_, &state_, &audio_, &visuals_);
#endif
  bootMark("BOOT 4: RUNNING", 204);
  // Produce a real frame during setup so NVS, touch, HTTP, or any later loop
  // service can never leave a successful boot screen as the final image.
  if (displayReady_ && visualsReady_) {
    const uint32_t firstFrameMs = millis();
    buildOverlay_(firstFrameMs);
    if (visuals_.render(renderState_, AudioFeatures{}, overlay_, firstFrameMs)) {
#if USE_DISPLAY
      CrowDisplay::flush();
#endif
      lastRenderMs_ = firstFrameMs;
    } else {
      renderError_ = visuals_.lastError();
      drawRenderError_();
    }
  }
  lastDemoChangeMs_ = millis();
  lastInteractionMs_ = millis();
  Logger::info("acid-glass", "controller ready; proof=uploaded");
}

void AcidGlassApp::tick() {
#if ACID_GLASS_TOUCH_ENABLED
  handleTouch_();
#endif
  const uint32_t now = millis();

#if ACID_GLASS_BRINGUP_VISUAL_ONLY
  if (!displayReady_ || !visualsReady_) {
    renderError_ = !displayReady_ ? "DISPLAY UNAVAILABLE" : visuals_.lastError();
    drawRenderError_();
    return;
  }
  const uint32_t proofFrameMs = 1000UL / max<uint16_t>(1, ACID_GLASS_RENDER_FPS);
  if (now - lastRenderMs_ >= proofFrameMs) {
    lastRenderMs_ = now;
    buildOverlay_(now);
    const bool rendered = visuals_.proofComplete(now)
                              ? visuals_.render(renderState_, AudioFeatures{}, overlay_, now)
                              : visuals_.renderBringupProof(now);
    if (rendered) {
      CrowDisplay::flush();
    } else {
      renderError_ = visuals_.lastError();
      drawRenderError_();
    }
  }
  return;
#endif

  AudioFeatures features = audio_.features();
  if (!audio_.playing()) audio_.synthFeatures(now, features);
  if (state_.demo && !state_.frozen &&
      (now - lastDemoChangeMs_ >= kDemoSceneMs ||
       (features.onset > 220 && now - lastDemoChangeMs_ >= 3000))) {
    nextScene_(1, ControlSource::kDemo);
    if ((state_.scene & 1) == 0) nextPalette_(1, ControlSource::kDemo);
    lastDemoChangeMs_ = now;
  }

  const uint32_t frameMs = 1000UL / max<uint16_t>(1, ACID_GLASS_RENDER_FPS);
  const uint32_t pacedFrameMs = 1000UL / max<uint8_t>(1, targetFps_);
  if (displayReady_ && !state_.frozen && now - lastRenderMs_ >= pacedFrameMs) {
    if (now - lastRenderMs_ > pacedFrameMs * 2) {
      droppedFrames_ += (now - lastRenderMs_) / pacedFrameMs - 1;
    }
    lastRenderMs_ = now;
    updateRenderState_();
    buildOverlay_(now);
    if (visuals_.render(renderState_, features, overlay_, now)) {
#if USE_DISPLAY
      CrowDisplay::flush();
#endif
      noteFrame_();
    }
  }
  if (savePending_ && now - lastSaveMs_ >= kSaveDelayMs) saveState_();
  // Service the hosted-C6 remote after the frame path. If the network stack
  // ever stalls, it cannot suppress the first or current visual frame.
  remote_.tick();
}

void AcidGlassApp::drawRenderError_() {
#if USE_DISPLAY
  Arduino_GFX *g = CrowDisplay::canvas();
  if (g == nullptr) return;
  g->fillRect(0, 220, 1024, 88, 0x7800);
  g->setTextSize(2);
  g->setTextColor(0xFFFF);
  g->setCursor(24, 238);
  g->print(F("RENDER ERROR:"));
  g->setCursor(24, 268);
  g->print(renderError_);
  CrowDisplay::flush();
#endif
}

bool AcidGlassApp::remoteControl_(void *context, const ControlEvent &event) {
  return static_cast<AcidGlassApp *>(context)->control(event);
}

bool AcidGlassApp::control(const ControlEvent &event) {
  String action(event.action);
  action.toLowerCase();
  if (action == "next") {
    nextScene_(1, event.source);
  } else if (action == "previous" || action == "prev") {
    nextScene_(-1, event.source);
  } else if (action == "scene") {
    if (event.value < 0 || event.value >= kAcidSceneCount) return false;
    state_.scene = event.value;
    startTransition_();
    markChanged_(event.source);
  } else if (action == "palette") {
    if (event.value < 0 || event.value >= kAcidPaletteCount) return false;
    state_.palette = event.value;
    startTransition_();
    markChanged_(event.source);
  } else if (action == "quality") {
    if (event.value < 0 || event.value > 2) return false;
    state_.qualityMode = static_cast<AcidQualityMode>(event.value);
    stableFrameCount_ = 0;
    markChanged_(event.source);
  } else if (action == "set") {
    if (!setParameter_(event.key, event.value)) return false;
    markChanged_(event.source);
  } else if (action == "randomize") {
    randomize_(event.source);
  } else if (action == "demo") {
    state_.demo = event.value != 0;
    state_.frozen = false;
    savePending_ = true;
    lastSaveMs_ = millis();
    lastInteractionMs_ = millis();
  } else if (action == "freeze") {
    state_.frozen = event.value < 0 ? !state_.frozen : event.value != 0;
    markChanged_(event.source);
  } else if (action == "hud") {
    state_.hud = event.value < 0 ? !state_.hud : event.value != 0;
    markChanged_(event.source);
  } else if (action == "volume") {
    state_.volume = min<int32_t>(100, max<int32_t>(0, event.value));
    audio_.setVolume(state_.volume);
    markChanged_(event.source);
  } else if (action == "play") {
    uint8_t index = event.value >= 0 && event.value < audio_.trackCount()
                        ? event.value
                        : audio_.activeTrack();
    if (!audio_.play(index)) return false;
    state_.demo = false;
    markChanged_(event.source);
  } else if (action == "stop") {
    audio_.stop();
  } else if (action == "tracknext") {
    audio_.next();
    state_.demo = false;
  } else if (action == "trackprev") {
    audio_.previous();
    state_.demo = false;
  } else if (action == "preset") {
    if (event.value < 0 || event.value >= kAcidPresetCount) return false;
    String mode(event.key);
    mode.toLowerCase();
    return mode == "save" ? savePreset_(event.value) : loadPreset_(event.value, event.source);
  } else {
    return false;
  }
  return true;
}

bool AcidGlassApp::setParameter_(const char *keyValue, int32_t value) {
  String key(keyValue != nullptr ? keyValue : "");
  key.toLowerCase();
  uint8_t v = clampByte(value);
  if (key == "speed") state_.visual.speed = v;
  else if (key == "zoom") state_.visual.zoom = v;
  else if (key == "intensity") state_.visual.intensity = v;
  else if (key == "warp") state_.visual.warp = v;
  else if (key == "feedback") state_.visual.feedback = v;
  else if (key == "trails") state_.visual.trails = v;
  else if (key == "symmetry") state_.visual.symmetry = max<uint8_t>(1, v);
  else if (key == "pixels" || key == "pixelscale") state_.visual.pixelScale = max<uint8_t>(1, v);
  else if (key == "hue" || key == "huerate") state_.visual.hueRate = v;
  else if (key == "sensitivity" || key == "audio") {
    state_.visual.audioSensitivity = v;
    audio_.setSensitivity(v);
  } else if (key == "macrox" || key == "x") state_.visual.macroX = v;
  else if (key == "macroy" || key == "y") state_.visual.macroY = v;
  else if (key == "brightness") {
    state_.brightness = max<uint8_t>(40, v);
#if USE_DISPLAY
    CrowDisplay::setBacklight(state_.brightness);
#endif
  } else if (key == "safe") state_.safeFlash = value != 0;
  else return false;
  return true;
}

void AcidGlassApp::nextScene_(int8_t direction, ControlSource source) {
  state_.scene = AcidGlassLogic::wrapIndex(state_.scene + direction, kAcidSceneCount);
  startTransition_();
  markChanged_(source);
}

void AcidGlassApp::nextPalette_(int8_t direction, ControlSource source) {
  state_.palette = AcidGlassLogic::wrapIndex(state_.palette + direction, kAcidPaletteCount);
  startTransition_();
  markChanged_(source);
}

void AcidGlassApp::randomize_(ControlSource source) {
  auto nextRandom = [this]() {
    randomState_ ^= randomState_ << 13;
    randomState_ ^= randomState_ >> 17;
    randomState_ ^= randomState_ << 5;
    return randomState_;
  };
  state_.scene = nextRandom() % kAcidSceneCount;
  state_.palette = nextRandom() % kAcidPaletteCount;
  state_.visual.speed = 70 + nextRandom() % 180;
  state_.visual.zoom = 60 + nextRandom() % 190;
  state_.visual.intensity = 150 + nextRandom() % 106;
  state_.visual.warp = nextRandom() & 0xFF;
  state_.visual.feedback = nextRandom() % 190;
  state_.visual.trails = 80 + nextRandom() % 160;
  state_.visual.symmetry = 1 + nextRandom() % 12;
  state_.visual.macroX = nextRandom() & 0xFF;
  state_.visual.macroY = nextRandom() & 0xFF;
  startTransition_();
  markChanged_(source);
}

void AcidGlassApp::markChanged_(ControlSource source) {
  lastInteractionMs_ = millis();
  if (source != ControlSource::kDemo) {
    state_.demo = false;
    savePending_ = true;
    lastSaveMs_ = millis();
  }
}

void AcidGlassApp::startTransition_() {
  transitionStartedMs_ = millis();
}

void AcidGlassApp::updateRenderState_() {
  renderState_.scene = state_.scene;
  renderState_.palette = state_.palette;
  renderState_.brightness = state_.brightness;
  renderState_.volume = state_.volume;
  renderState_.demo = state_.demo;
  renderState_.frozen = state_.frozen;
  renderState_.hud = state_.hud;
  renderState_.safeFlash = state_.safeFlash;
  renderState_.qualityMode = state_.qualityMode;
  renderState_.visual.speed = AcidGlassLogic::smoothByte(renderState_.visual.speed, state_.visual.speed);
  renderState_.visual.zoom = AcidGlassLogic::smoothByte(renderState_.visual.zoom, state_.visual.zoom);
  renderState_.visual.intensity = AcidGlassLogic::smoothByte(renderState_.visual.intensity, state_.visual.intensity);
  renderState_.visual.warp = AcidGlassLogic::smoothByte(renderState_.visual.warp, state_.visual.warp);
  renderState_.visual.feedback = AcidGlassLogic::smoothByte(renderState_.visual.feedback, state_.visual.feedback);
  renderState_.visual.trails = AcidGlassLogic::smoothByte(renderState_.visual.trails, state_.visual.trails);
  renderState_.visual.symmetry = AcidGlassLogic::smoothByte(renderState_.visual.symmetry, state_.visual.symmetry);
  renderState_.visual.pixelScale = AcidGlassLogic::smoothByte(renderState_.visual.pixelScale, state_.visual.pixelScale);
  renderState_.visual.hueRate = AcidGlassLogic::smoothByte(renderState_.visual.hueRate, state_.visual.hueRate);
  renderState_.visual.macroX = AcidGlassLogic::smoothByte(renderState_.visual.macroX, state_.visual.macroX);
  renderState_.visual.macroY = AcidGlassLogic::smoothByte(renderState_.visual.macroY, state_.visual.macroY);
}

void AcidGlassApp::buildOverlay_(uint32_t nowMs) {
  overlay_.hud = state_.hud;
  overlay_.sheet = drawer_;
  overlay_.targetFps = targetFps_;
  overlay_.qualityTier = qualityTier_;
  overlay_.presentUs = visuals_.lastPresentUs();
  overlay_.droppedFrames = droppedFrames_;
  overlay_.ppaReady = visuals_.ppaReady();
  overlay_.ppaRequested = visuals_.ppaRequested();
  overlay_.sdReady = audio_.sdReady();
  overlay_.audioPlaying = audio_.playing();
  overlay_.remoteReady = remote_.ready();
  overlay_.transition = transitionStartedMs_ == 0
                            ? 255
                            : min<uint32_t>(255, (nowMs - transitionStartedMs_) * 255UL / 360UL);
  if (overlay_.transition >= 255) transitionStartedMs_ = 0;
  if (presetMessage_[0] != '\0' && static_cast<int32_t>(presetMessageUntil_ - nowMs) > 0) {
    strlcpy(overlay_.toast, presetMessage_, sizeof(overlay_.toast));
  } else {
    overlay_.toast[0] = '\0';
  }
  visuals_.setPresentationTelemetry(targetFps_, qualityTier_, droppedFrames_,
                                    drawer_ != kAcidSheetClosed || overlay_.toast[0] != '\0');
}

void AcidGlassApp::noteFrame_() {
  const uint32_t frameUs = visuals_.lastFrameUs();
  const uint8_t mode = static_cast<uint8_t>(state_.qualityMode);
  if (mode == static_cast<uint8_t>(AcidQualityMode::kAuto) && frameUs <= 19000) stableFrameCount_++;
  else stableFrameCount_ = 0;
  targetFps_ = AcidGlassLogic::adaptiveTargetFps(frameUs, targetFps_, stableFrameCount_, mode);
  qualityTier_ = AcidGlassLogic::qualityTierFor(targetFps_, mode);
}

void AcidGlassApp::handleTouch_() {
#if USE_DISPLAY
  CrowDisplay::TouchPointData points[5];
  uint8_t count = CrowDisplay::touchPoints(points, 5);
  if (count > 0) {
    lastTouchX_ = points[0].x;
    lastTouchY_ = points[0].y;
    if (!touchDown_) {
      touchDown_ = true;
      touchStartedMs_ = millis();
      touchStartX_ = lastTouchX_;
      touchStartY_ = lastTouchY_;
      pinchStartZoom_ = state_.visual.zoom;
      pinchStartDistance_ = count >= 2 ? abs(points[0].x - points[1].x) + abs(points[0].y - points[1].y) : 0;
      if (count >= 3 && lastTouchCount_ < 3) randomize_(ControlSource::kTouch);
    }
    if (count >= 2 && pinchStartDistance_ > 0) {
      int16_t distance = abs(points[0].x - points[1].x) + abs(points[0].y - points[1].y);
      state_.visual.zoom = clampByte(pinchStartZoom_ + (distance - pinchStartDistance_) / 2);
      state_.visual.feedback = clampByte((points[0].y + points[1].y) * 255L / 1200L);
      markChanged_(ControlSource::kTouch);
    } else if (count == 1 && drawer_ != kAcidSheetClosed &&
               updateDrawerDrag_(lastTouchX_, lastTouchY_)) {
      // Live sliders own this drag; persistence is still deferred.
      markChanged_(ControlSource::kTouch);
    } else if (count == 1 && drawer_ == kAcidSheetClosed && millis() - touchStartedMs_ > 120) {
      state_.visual.macroX = AcidGlassLogic::mapCoordinate(lastTouchX_, 1023);
      state_.visual.macroY = AcidGlassLogic::mapCoordinate(lastTouchY_, 599);
    }
  } else if (touchDown_) {
    touchDown_ = false;
    int16_t dx = lastTouchX_ - touchStartX_;
    int16_t dy = lastTouchY_ - touchStartY_;
    uint32_t held = millis() - touchStartedMs_;
    if (handleDrawerTouch_(lastTouchX_, lastTouchY_, held)) {
      // Drawer controls own this release.
    } else if (held > 800 && abs(dx) < 35 && abs(dy) < 35) {
      state_.frozen = !state_.frozen;
      markChanged_(ControlSource::kTouch);
    } else if (abs(dx) > kSwipeDistance && abs(dx) > abs(dy)) {
      nextScene_(dx > 0 ? -1 : 1, ControlSource::kTouch);
    } else if (abs(dy) > kSwipeDistance) {
      nextPalette_(dy > 0 ? -1 : 1, ControlSource::kTouch);
    } else if (lastTouchX_ > 900 && lastTouchY_ < 100) {
      state_.hud = !state_.hud;
      markChanged_(ControlSource::kTouch);
    } else {
      state_.visual.macroX = AcidGlassLogic::mapCoordinate(lastTouchX_, 1023);
      state_.visual.macroY = AcidGlassLogic::mapCoordinate(lastTouchY_, 599);
      markChanged_(ControlSource::kTouch);
    }
  }
  lastTouchCount_ = count;
#endif
}

bool AcidGlassApp::handleDrawerTouch_(int16_t x, int16_t y, uint32_t heldMs) {
#if USE_DISPLAY
  if (y >= 552) {
    uint8_t requested = min<uint8_t>(3, max<int16_t>(0, x) / 256);
    drawer_ = drawer_ == requested ? kAcidSheetClosed : requested;
    state_.hud = true;
    return true;
  }
  if (drawer_ == kAcidSheetClosed || y < 360) return false;
  if (drawer_ == 0) {
    if (y < 440) {
      uint8_t button = min<uint8_t>(3, max<int16_t>(0, x) / 256);
      if (button == 0) nextScene_(-1, ControlSource::kTouch);
      else if (button == 1) nextScene_(1, ControlSource::kTouch);
      else if (button == 2) nextPalette_(-1, ControlSource::kTouch);
      else nextPalette_(1, ControlSource::kTouch);
      return true;
    }
    if (y < 540) {
      uint8_t col = min<uint8_t>(7, max<int16_t>(0, x) / 128);
      uint8_t row = y < 480 ? 0 : 1;
      uint8_t slot = row * 8 + col;
      if (heldMs >= 700) savePreset_(slot);
      else loadPreset_(slot, ControlSource::kTouch);
    }
    return true;
  }
  if (drawer_ == 1) {
    if (y >= 500 && y < 540) {
      nextPalette_(x < 512 ? -1 : 1, ControlSource::kTouch);
    }
    return true;
  }
  if (drawer_ == 3 && y < 450) {
    uint8_t button = min<uint8_t>(3, max<int16_t>(0, x) / 256);
    if (button == 0) audio_.previous();
    else if (button == 1) { audio_.play(audio_.activeTrack()); state_.demo = false; }
    else if (button == 2) audio_.stop();
    else { audio_.next(); state_.demo = false; }
    return true;
  }
  return true;
#else
  (void)x;
  (void)y;
  (void)heldMs;
  return false;
#endif
}

bool AcidGlassApp::updateDrawerDrag_(int16_t x, int16_t y) {
#if USE_DISPLAY
  const uint8_t value = clampByte((x - 224) * 255L / 760L);
  if (drawer_ == 1) {
    if (y >= 390 && y < 430) return setParameter_("intensity", value);
    if (y >= 430 && y < 470) return setParameter_("hue", value);
    if (y >= 470 && y < 510) return setParameter_("safe", value > 127);
  } else if (drawer_ == 2) {
    static const char *const keys[] = {"speed", "zoom", "warp", "feedback", "trails", "pixels"};
    if (y >= 380 && y < 540) {
      uint8_t row = min<uint8_t>(5, (y - 380) / 27);
      return setParameter_(keys[row], value);
    }
  } else if (drawer_ == 3) {
    if (y >= 465 && y < 505) {
      state_.volume = min<uint8_t>(100, value * 100L / 255L);
      audio_.setVolume(state_.volume);
      return true;
    }
    if (y >= 505 && y < 545) return setParameter_("sensitivity", value);
  }
#else
  (void)x;
  (void)y;
#endif
  return false;
}

void AcidGlassApp::drawHud_() {
#if USE_DISPLAY
  if (!state_.hud) return;
  Arduino_GFX *g = CrowDisplay::canvas();
  if (g == nullptr) return;
  const uint16_t black = 0x0000, white = 0xFFFF, pink = 0xF81F, lime = 0x87E0;
  g->fillRect(0, 0, 1024, 54, dimColor(black, 230));
  g->setTextWrap(false);
  g->setTextSize(2);
  g->setTextColor(pink);
  g->setCursor(18, 18);
  g->print(F("ACID GLASS"));
  g->setTextColor(white);
  g->setCursor(180, 18);
  g->print(AcidGlassVisuals::sceneName(state_.scene));
  g->setTextColor(lime);
  g->setCursor(515, 18);
  g->print(AcidGlassVisuals::paletteName(state_.palette));
  g->setTextColor(white);
  g->setCursor(790, 18);
  g->printf("%u FPS  PPA %s", visuals_.fps(), visuals_.ppaReady()
                                         ? "ON"
                                         : (visuals_.ppaRequested() ? "ERR" : "CPU"));

  g->fillRect(0, 552, 1024, 48, black);
  g->setTextSize(1);
  g->setTextColor(white);
  g->setCursor(16, 568);
  g->printf("X %3u  Y %3u  ZOOM %3u  WARP %3u  FEEDBACK %3u  AUDIO %3u",
            state_.visual.macroX, state_.visual.macroY, state_.visual.zoom,
            state_.visual.warp, state_.visual.feedback, state_.visual.audioSensitivity);
  g->setCursor(16, 584);
  g->printf("%s  |  %s  |  %s  |  %s", audio_.sdReady() ? "SD ON" : "SD OFF",
            audio_.playing() ? "PLAY" : "DEMO", audio_.trackName(audio_.activeTrack()),
            remote_.ready() ? remote_.url() : "REMOTE OFF");
  drawDrawer_();
  if (presetMessage_[0] != '\0' && static_cast<int32_t>(presetMessageUntil_ - millis()) > 0) {
    g->fillRoundRect(330, 68, 364, 38, 8, black);
    g->drawRoundRect(330, 68, 364, 38, 8, lime);
    g->setTextSize(1);
    g->setTextColor(lime);
    g->setCursor(354, 84);
    g->print(presetMessage_);
  }
#endif
}

void AcidGlassApp::drawDrawer_() {
#if USE_DISPLAY
  Arduino_GFX *g = CrowDisplay::canvas();
  if (g == nullptr) return;
  const uint16_t black = 0x0000, white = 0xFFFF, pink = 0xF81F, lime = 0x87E0;
  if (drawer_ > 0) {
    g->fillRect(0, 360, 1024, 192, black);
    g->drawFastHLine(0, 360, 1024, pink);
    g->setTextSize(2);
    g->setTextColor(white);
    if (drawer_ == 1) {
      const char *labels[] = {"SPEED", "WARP", "FEEDBACK", "SYMMETRY"};
      uint8_t values[] = {state_.visual.speed, state_.visual.warp,
                          state_.visual.feedback, static_cast<uint8_t>(state_.visual.symmetry * 255 / 12)};
      for (uint8_t i = 0; i < 4; ++i) {
        int16_t y = 378 + i * 40;
        g->setCursor(24, y + 8);
        g->print(labels[i]);
        g->drawRect(260, y, 720, 22, 0x4208);
        g->fillRect(263, y + 3, values[i] * 714L / 255L, 16, i & 1 ? lime : pink);
      }
    } else if (drawer_ == 2) {
      const char *buttons[] = {"PREV", "PLAY", "STOP", "NEXT"};
      for (uint8_t i = 0; i < 4; ++i) {
        g->drawRect(i * 256 + 8, 378, 240, 56, i == 1 ? lime : pink);
        g->setCursor(i * 256 + 92, 399);
        g->print(buttons[i]);
      }
      g->setCursor(24, 463); g->print(F("VOLUME"));
      g->drawRect(260, 458, 720, 22, 0x4208);
      g->fillRect(263, 461, state_.volume * 714L / 100L, 16, lime);
      g->setCursor(24, 510); g->print(F("AUDIO"));
      g->drawRect(260, 505, 720, 22, 0x4208);
      g->fillRect(263, 508, state_.visual.audioSensitivity * 714L / 255L, 16, pink);
      g->setTextSize(1);
      g->setCursor(24, 540);
      g->printf("%s  |  UNDERRUNS %lu", audio_.status(),
                static_cast<unsigned long>(audio_.underruns()));
    } else {
      g->setTextSize(1);
      for (uint8_t i = 0; i < kAcidPresetCount; ++i) {
        int16_t x = (i % 8) * 128 + 5;
        int16_t y = 374 + (i / 8) * 52;
        uint16_t outline = activePreset_ == static_cast<int8_t>(i) ? lime : pink;
        g->drawRect(x, y, 118, 42, outline);
        g->fillRect(x + 3, y + 3, 54, 5,
                    AcidGlassVisuals::paletteColor(presetPalettes_[i], 72));
        g->fillRect(x + 57, y + 3, 58, 5,
                    AcidGlassVisuals::paletteColor(presetPalettes_[i], 196));
        g->setTextColor(outline);
        g->setCursor(x + 7, y + 13);
        g->printf("%02u %s", i + 1, kFactoryPresets[i].name);
        g->setCursor(x + 7, y + 29);
        g->print((userPresetMask_ & (1U << i)) ? "USER HOLD=SAVE" : "FACTORY");
      }
      const char *system[] = {state_.demo ? "DEMO ON" : "DEMO OFF",
                              state_.safeFlash ? "SAFE ON" : "SAFE OFF", "DIM -", "BRIGHT +"};
      for (uint8_t i = 0; i < 4; ++i) {
        g->drawRect(i * 256 + 8, 485, 240, 54, i < 2 ? lime : pink);
        g->setCursor(i * 256 + 92, 507);
        g->print(system[i]);
      }
    }
  }
  const char *tabs[] = {"LIVE", "VISUAL", "AUDIO", "PRESETS"};
  g->setTextSize(2);
  for (uint8_t i = 0; i < 4; ++i) {
    uint16_t color = drawer_ == i ? lime : 0x4208;
    g->drawRect(i * 256, 552, 256, 48, color);
    g->setTextColor(drawer_ == i ? lime : white);
    g->setCursor(i * 256 + 82, 569);
    g->print(tabs[i]);
  }
#endif
}

void AcidGlassApp::loadState_() {
  Preferences preferences;
  if (!preferences.begin("acidglass", true)) return;
  StoredState stored;
  if (preferences.getBytesLength("state") == sizeof(stored)) {
    preferences.getBytes("state", &stored, sizeof(stored));
    if (stored.magic == kStoredMagic && stored.version == kStoredVersion &&
        stored.state.scene < kAcidSceneCount && stored.state.palette < kAcidPaletteCount) {
      state_ = stored.state;
    }
  }
  preferences.end();
}

void AcidGlassApp::saveState_() {
  StoredState stored;
  stored.magic = kStoredMagic;
  stored.version = kStoredVersion;
  stored.state = state_;
  stored.state.frozen = false;
  Preferences preferences;
  if (preferences.begin("acidglass", false)) {
    preferences.putBytes("state", &stored, sizeof(stored));
    preferences.end();
  }
  savePending_ = false;
}

bool AcidGlassApp::savePreset_(uint8_t slot) {
  if (slot >= kAcidPresetCount) return false;
  char key[5];
  snprintf(key, sizeof(key), "p%u", slot);
  StoredState stored;
  stored.magic = kStoredMagic;
  stored.version = kStoredVersion;
  stored.state = state_;
  stored.state.frozen = false;
  Preferences preferences;
  bool ok = preferences.begin("acidglass", false) &&
            preferences.putBytes(key, &stored, sizeof(stored)) == sizeof(stored);
  preferences.end();
  if (ok) {
    userPresetMask_ |= static_cast<uint16_t>(1U << slot);
    presetPalettes_[slot] = state_.palette;
    activePreset_ = slot;
    showPresetMessage_("SAVED", slot);
  }
  return ok;
}

bool AcidGlassApp::loadPreset_(uint8_t slot, ControlSource source) {
  if (slot >= kAcidPresetCount) return false;
  char key[5];
  snprintf(key, sizeof(key), "p%u", slot);
  StoredState stored;
  Preferences preferences;
  bool ok = preferences.begin("acidglass", true) &&
            preferences.getBytesLength(key) == sizeof(stored) &&
            preferences.getBytes(key, &stored, sizeof(stored)) == sizeof(stored) &&
            stored.magic == kStoredMagic && stored.version == kStoredVersion;
  preferences.end();
  if (ok && stored.state.scene < kAcidSceneCount && stored.state.palette < kAcidPaletteCount) {
    state_ = stored.state;
  } else {
    state_ = factoryState(slot);
  }
  state_.demo = false;
  state_.frozen = false;
  startTransition_();
  audio_.setVolume(state_.volume);
  audio_.setSensitivity(state_.visual.audioSensitivity);
#if USE_DISPLAY
  CrowDisplay::setBacklight(state_.brightness);
#endif
  activePreset_ = slot;
  presetPalettes_[slot] = state_.palette;
  showPresetMessage_((userPresetMask_ & (1U << slot)) ? "LOADED USER" : "LOADED", slot);
  markChanged_(source);
  return true;
}

void AcidGlassApp::scanPresets_() {
  userPresetMask_ = 0;
  for (uint8_t i = 0; i < kAcidPresetCount; ++i) {
    presetPalettes_[i] = kFactoryPresets[i].palette;
  }
  Preferences preferences;
  if (!preferences.begin("acidglass", true)) return;
  for (uint8_t i = 0; i < kAcidPresetCount; ++i) {
    char key[5];
    snprintf(key, sizeof(key), "p%u", i);
    StoredState stored;
    bool valid = preferences.getBytesLength(key) == sizeof(stored) &&
                 preferences.getBytes(key, &stored, sizeof(stored)) == sizeof(stored) &&
                 stored.magic == kStoredMagic && stored.version == kStoredVersion &&
                 stored.state.scene < kAcidSceneCount &&
                 stored.state.palette < kAcidPaletteCount;
    if (valid) {
      userPresetMask_ |= static_cast<uint16_t>(1U << i);
      presetPalettes_[i] = stored.state.palette;
    }
  }
  preferences.end();
}

void AcidGlassApp::showPresetMessage_(const char *action, uint8_t slot) {
  snprintf(presetMessage_, sizeof(presetMessage_), "%s %02u  %s", action, slot + 1,
           kFactoryPresets[slot % kAcidPresetCount].name);
  presetMessageUntil_ = millis() + 1400;
}

void AcidGlassApp::printStatus(Print &out) const {
  out.print(F("[acid-glass] proof=uploaded scene="));
  out.print(state_.scene);
  out.print(' ');
  out.print(AcidGlassVisuals::sceneName(state_.scene));
  out.print(F(" palette="));
  out.print(state_.palette);
  out.print(' ');
  out.print(AcidGlassVisuals::paletteName(state_.palette));
  out.print(F(" demo="));
  out.print(state_.demo ? F("on") : F("off"));
  out.print(F(" frozen="));
  out.print(state_.frozen ? F("yes") : F("no"));
  out.print(F(" target_fps="));
  out.print(targetFps_);
  out.print(F(" quality_tier="));
  out.print(qualityTier_ + 1);
  out.print(F(" quality_mode="));
  out.print(static_cast<uint8_t>(state_.qualityMode));
  out.print(F(" dropped="));
  out.println(droppedFrames_);
  visuals_.printStatus(out);
  audio_.printStatus(out);
  remote_.printStatus(out);
}

void AcidGlassApp::printScenes(Print &out) const {
  for (uint8_t i = 0; i < kAcidSceneCount; ++i) {
    out.print(i == state_.scene ? F("* ") : F("  "));
    out.print(i);
    out.print(F(": "));
    out.println(AcidGlassVisuals::sceneName(i));
  }
}

void AcidGlassApp::printPalettes(Print &out) const {
  for (uint8_t i = 0; i < kAcidPaletteCount; ++i) {
    out.print(i == state_.palette ? F("* ") : F("  "));
    out.print(i);
    out.print(F(": "));
    out.println(AcidGlassVisuals::paletteName(i));
  }
}

void AcidGlassApp::printTracks(Print &out) const {
  if (audio_.trackCount() == 0) {
    out.println(F("[tracks] no WAV files found under /acid-glass/music"));
    return;
  }
  for (uint8_t i = 0; i < audio_.trackCount(); ++i) {
    out.print(i == audio_.activeTrack() ? F("* ") : F("  "));
    out.print(i);
    out.print(F(": "));
    out.println(audio_.trackName(i));
  }
}

void AcidGlassApp::printPresets(Print &out) const {
  Preferences preferences;
  bool opened = preferences.begin("acidglass", true);
  for (uint8_t i = 0; i < kAcidPresetCount; ++i) {
    char key[5];
    snprintf(key, sizeof(key), "p%u", i);
    out.print(i + 1);
    out.print(F(": "));
    out.print(opened && preferences.getBytesLength(key) == sizeof(StoredState) ? F("user") : F("factory"));
    out.print(F("  "));
    out.println(kFactoryPresets[i].name);
  }
  if (opened) preferences.end();
}

void AcidGlassApp::printTouch(Print &out) const {
  out.print(F("[touch] contacts="));
  out.print(lastTouchCount_);
  out.print(F(" last="));
  out.print(lastTouchX_);
  out.print(',');
  out.println(lastTouchY_);
}

void AcidGlassApp::benchmark(Print &out, uint16_t seconds) {
  out.print(F("[bench] non-blocking benchmark armed for "));
  out.print(seconds);
  out.println(F("s; read live FPS/frame_us with status"));
}
