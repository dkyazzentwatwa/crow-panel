#ifndef VISION_GUARD_UI_H
#define VISION_GUARD_UI_H

#include "../config/ProjectConfig.h"
#include <Arduino.h>
#include <CrowPanelShared.h>
#include "CameraManager.h"
#include "InspectionWorkflow.h"

// Touch-first inspection console for the 1024x600 CrowPanel DSI panel, built on
// the shared CrowDisplay bring-up + Widgets toolkit (dark "ops" palette,
// FreeSans fonts, cards/bars/pills). Five screens navigated by a bottom tab
// bar, an honest camera placeholder (no fake frames), a tappable checklist, a
// per-item result hero, and an auditable history list.
//
// tick() reads touch and the InspectionWorkflow model and returns an action for
// the sketch to execute. The UI never mutates inspection state directly - it
// renders the model and reports intent - so every touch action has a 1:1 serial
// command equivalent.

enum VisionScreen : uint8_t {
  SCR_LIVE = 0,
  SCR_SCAN,
  SCR_CHECKS,
  SCR_RESULT,
  SCR_HISTORY,
  SCR_COUNT,
};

enum class VisionEventType : uint8_t {
  None = 0,
  Scan,        // capture + inspect a new code
  CycleItem,   // index = checklist row to cycle
  ReEvaluate,  // re-roll the current run's items
  OpenRun,     // index = history age index to open in Result
};

struct VisionEvent {
  VisionEventType type = VisionEventType::None;
  int16_t index = -1;
};

const char *visionScreenName(VisionScreen s);

class VisionGuardUi {
 public:
  void begin(InspectionWorkflow *workflow);

  // Latest camera status, pushed from the sketch once per loop() so the Live
  // screen can render a fresh (synthetic) frame counter without the UI owning
  // the camera object.
  void setCameraStatus(const CameraStatus &status, const char *sourceName);

  // Call once per loop(). Returns the action the user launched this frame.
  VisionEvent tick();

  // Navigation + repaint hooks the sketch drives from serial commands.
  void showScreen(VisionScreen s);
  VisionScreen screen() const { return screen_; }
  const char *screenName() const { return visionScreenName(screen_); }
  void markDirty();

  // Serial-parity helpers (work in headless builds too).
  void printTouchDiagnostics(Print &out) const;
  void renderSerial(Print &out) const;   // dump the active screen as text

 private:
  InspectionWorkflow *wf_ = nullptr;
  VisionScreen screen_ = SCR_LIVE;
  CameraStatus cam_{false, 0, 0, "n/a", 0};
  const char *camSource_ = "mock-camera";
  CrowTouch touch_;
  uint8_t histPage_ = 0;   // History-screen paging (7 rows per page)

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  bool ready_ = false;
  bool dirty_ = true;
  uint32_t lastDrawMs_ = 0;

  VisionEvent handleTouch_();
  void draw_();
  void drawHeader_();
  void drawLive_();
  void drawScan_();
  void drawChecks_();
  void drawResult_();
  void drawHistory_();
#endif
};

#endif
