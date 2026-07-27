#ifndef SURVEYOPS_SURVEY_UI_H
#define SURVEYOPS_SURVEY_UI_H

#include "SurveyDashboard.h"
#include <CrowPanelShared.h>

class SurveyUi {
 public:
  void begin();
  void update(const GpsFix &fix, const WifiApRecord *rows, uint8_t rowCount,
              uint16_t totalAps, const String &topAp, const SurveySessionStats &session,
              const WigleStorageHealth &storage, const String &banner);
  void setDetail(const String &title, const String &body);
  bool tick();

 private:
  SurveyDashboard dashboard_;
  SurveyDashboardState state_;
  Throttle heartbeat_{5000};
  uint16_t lastTotalAps_ = 0xFFFF;
  bool lastLogging_ = false;
};

#endif
