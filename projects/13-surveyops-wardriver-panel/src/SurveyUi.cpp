#include "SurveyUi.h"

namespace {
String fitForLog(const String &s, size_t maxLen) {
  if (s.length() <= maxLen) return s;
  String out = s.substring(0, maxLen > 3 ? maxLen - 3 : maxLen);
  out += "...";
  return out;
}
}  // namespace

void SurveyUi::begin() {
  state_.detailTitle = "Survey Ready";
  state_.detailBody = "Run scan, gps, log on, feed ap, storage, or rotate from Serial.";
  dashboard_.begin();
  Logger::info("survey-ui", "dashboard ready");
}

void SurveyUi::update(const GpsFix &fix, const WifiApRecord *rows, uint8_t rowCount,
                      uint16_t totalAps, const String &topAp, const SurveySessionStats &session,
                      const WigleStorageHealth &storage, const String &banner) {
  state_.fix = fix;
  state_.storage = storage;
  state_.rowCount = rows == nullptr ? 0 : (rowCount < kSurveyMaxRows ? rowCount : kSurveyMaxRows);
  for (uint8_t i = 0; i < state_.rowCount; i++) {
    state_.rows[i] = rows[i];
  }
  state_.totalAps = totalAps;
  state_.topAp = topAp;
  state_.session = session;
  state_.banner = fitForLog(banner, 120);
  dashboard_.update(state_);

  if (heartbeat_.ready() && (totalAps != lastTotalAps_ || storage.loggingEnabled != lastLogging_)) {
    lastTotalAps_ = totalAps;
    lastLogging_ = storage.loggingEnabled;
    Logger::info("survey-ui", String("aps=") + String(totalAps) +
                                " latest=" + String(state_.rowCount) +
                                " log=" + (storage.loggingEnabled ? "on" : "off") +
                                " gps=" + fix.coordinateText());
  }
}

void SurveyUi::setDetail(const String &title, const String &body) {
  state_.detailTitle = fitForLog(title, 64);
  state_.detailBody = fitForLog(body, 220);
  dashboard_.update(state_);
}

bool SurveyUi::tick() {
  return dashboard_.tick();
}
