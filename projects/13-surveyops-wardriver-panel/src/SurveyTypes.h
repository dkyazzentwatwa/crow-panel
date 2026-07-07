#ifndef SURVEYOPS_SURVEY_TYPES_H
#define SURVEYOPS_SURVEY_TYPES_H

#include "../config/ProjectConfig.h"
#include <Arduino.h>

constexpr uint8_t kSurveyMaxRows = 8;

struct GpsFix {
  bool valid = false;
  double latitude = 0.0;
  double longitude = 0.0;
  double altitudeMeters = 0.0;
  uint32_t satellites = 0;
  double hdop = 0.0;
  uint32_t ageMs = 0;
  String timestamp = "uptime";
  String source = "mock";

  String coordinateText() const {
    if (!valid) {
      return "no-fix";
    }
    return String(latitude, 6) + "," + String(longitude, 6);
  }

  String qualityText() const {
    if (!valid) {
      return "waiting";
    }
    return String(satellites) + " sats hdop=" + String(hdop, 1);
  }
};

struct WifiApRecord {
  String ssid;
  String bssid;
  String authMode;
  int32_t rssi = 0;
  uint8_t channel = 0;
  bool hidden = false;
  unsigned long seenAtMs = 0;
};

struct WigleStorageHealth {
  bool flagEnabled = false;
  bool ready = false;
  bool loggingEnabled = false;
  uint32_t rowsWritten = 0;
  uint16_t rotations = 0;
  String activeFile = "mock";
  String detail = "mock storage";
};

#endif
