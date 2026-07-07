#ifndef SURVEYOPS_WIFI_SURVEY_SCANNER_H
#define SURVEYOPS_WIFI_SURVEY_SCANNER_H

#include "SurveyTypes.h"
#include <CrowPanelShared.h>

class WiFiSurveyScanner {
 public:
  void begin();
  uint8_t scan(WifiApRecord *rows, uint8_t maxRows);
  const char *driverName() const;
  String statusLine() const;

 private:
  uint8_t loadMockRows(WifiApRecord *rows, uint8_t maxRows);

  bool ready_ = false;
  uint16_t scanCount_ = 0;
  String detail_ = "mock";
};

#endif
