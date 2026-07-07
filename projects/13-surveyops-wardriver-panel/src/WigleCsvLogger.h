#ifndef SURVEYOPS_WIGLE_CSV_LOGGER_H
#define SURVEYOPS_WIGLE_CSV_LOGGER_H

#include "SurveyTypes.h"
#include <CrowPanelShared.h>

class WigleCsvLogger {
 public:
  void begin();
  bool setEnabled(bool enabled);
  bool enabled() const;
  bool logRows(const WifiApRecord *rows, uint8_t count, const GpsFix &fix);
  bool rotate();
  WigleStorageHealth health() const;
  String statusLine() const;

 private:
  String nextFileName() const;

#if USE_SD_WIGLE_LOG
  bool ensureHeader();
#endif

  bool ready_ = false;
  bool enabled_ = false;
  uint32_t rowsWritten_ = 0;
  uint32_t rowsInActive_ = 0;
  uint16_t rotations_ = 0;
  uint16_t fileIndex_ = 1;
  String activeFile_ = "mock";
  String detail_ = "mock storage";
};

#endif
