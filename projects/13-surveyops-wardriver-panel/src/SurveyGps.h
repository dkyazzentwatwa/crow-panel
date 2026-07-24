#ifndef SURVEYOPS_SURVEY_GPS_H
#define SURVEYOPS_SURVEY_GPS_H

#include "SurveyTypes.h"
#include <CrowPanelShared.h>

// Include TinyGPSPlus.h directly under the flag - do NOT wrap it in
// __has_include. arduino-cli decides which libraries to link by preprocessing
// sources before the library is on the include path, so a __has_include guard
// evaluates false during discovery, the library never gets linked, and the
// driver silently compiles out even with USE_GPS_DRIVER=1.
#if USE_GPS_DRIVER
#include <TinyGPSPlus.h>
#define SURVEYOPS_HAS_TINYGPSPLUS 1
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
