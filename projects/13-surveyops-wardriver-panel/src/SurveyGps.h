#ifndef SURVEYOPS_SURVEY_GPS_H
#define SURVEYOPS_SURVEY_GPS_H

#include "SurveyTypes.h"
#include <CrowPanelShared.h>

#if USE_GPS_DRIVER
#if defined(__has_include)
#if __has_include(<TinyGPSPlus.h>)
#include <TinyGPSPlus.h>
#define SURVEYOPS_HAS_TINYGPSPLUS 1
#else
#define SURVEYOPS_HAS_TINYGPSPLUS 0
#endif
#else
#define SURVEYOPS_HAS_TINYGPSPLUS 0
#endif
#else
#define SURVEYOPS_HAS_TINYGPSPLUS 0
#endif

class SurveyGps {
 public:
  void begin();
  void poll();
  bool feedNmea(const String &sentence);
  GpsFix latest() const;
  const char *driverName() const;
  String statusLine() const;

 private:
  void setMockFix();

#if USE_GPS_DRIVER && SURVEYOPS_HAS_TINYGPSPLUS
  void updateFromParser(const char *source);
  TinyGPSPlus parser_;
#endif

  GpsFix fix_;
  bool parserReady_ = false;
  bool serialReady_ = false;
  uint32_t charsSeen_ = 0;
  String detail_ = "mock";
};

#endif
