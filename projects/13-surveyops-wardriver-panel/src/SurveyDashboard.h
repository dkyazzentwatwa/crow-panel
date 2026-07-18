#ifndef SURVEYOPS_SURVEY_DASHBOARD_H
#define SURVEYOPS_SURVEY_DASHBOARD_H

#include "../config/ProjectConfig.h"
#include "SurveyTypes.h"
#include <Arduino.h>

struct SurveyDashboardState {
  GpsFix fix;
  WigleStorageHealth storage;
  WifiApRecord rows[kSurveyMaxRows];
  uint8_t rowCount = 0;
  uint16_t totalAps = 0;
  String topAp = "StudioNet";
  String banner = "survey dashboard ready";
  String detailTitle = "Survey Ready";
  String detailBody = "Run scan, feed ap, gps, log, storage, or rotate from Serial.";
};

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
#include <CrowPanelShared.h>
#endif

class SurveyDashboard {
 public:
  void begin();
  void update(const SurveyDashboardState &state);
  void tick();

 private:
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  void handleTouch_();
  bool handleTouchAt_(int16_t tx, int16_t ty, const char *mapping);
  int8_t hitTestRow_(int16_t tx, int16_t ty) const;
  void cycleSelected_();
  void drawFull_();
  void drawHeader_();
  void drawScope_();
  void drawApList_();
  void drawGpsCard_();
  void drawDetailCard_();
  void drawFooter_();

  SurveyDashboardState state_;
  bool ready_ = false;
  bool dirty_ = true;
  bool wasTouched_ = false;
  int8_t selectedRow_ = -1;
  float sweepDeg_ = 0.0f;
  uint32_t lastFrameMs_ = 0;
  uint32_t lastTouchActionMs_ = 0;
  Throttle frameGate_{120};
  Throttle footerGate_{1000};
#endif
};

#endif
