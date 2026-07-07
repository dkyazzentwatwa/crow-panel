#include "SurveyGps.h"

void SurveyGps::begin() {
#if USE_GPS_DRIVER
#if SURVEYOPS_HAS_TINYGPSPLUS
  parserReady_ = true;
  fix_ = GpsFix();
  fix_.source = "tinygpsplus";
  detail_ = "waiting for NMEA";
  if (SURVEYOPS_GPS_RX_PIN < 0) {
    detail_ = "GPS RX pin not configured";
    Logger::error("gps", "set SURVEYOPS_GPS_RX_PIN/TX_PIN before hardware GPS bring-up");
    return;
  }
  Serial1.begin(SURVEYOPS_GPS_SERIAL_BAUD, SERIAL_8N1, SURVEYOPS_GPS_RX_PIN,
                SURVEYOPS_GPS_TX_PIN);
  serialReady_ = true;
  detail_ = String("Serial1 rx=") + String(SURVEYOPS_GPS_RX_PIN) +
            " tx=" + String(SURVEYOPS_GPS_TX_PIN) +
            " baud=" + String(SURVEYOPS_GPS_SERIAL_BAUD);
  Logger::info("gps", "TinyGPSPlus enabled; passive NMEA parsing only");
#else
  fix_ = GpsFix();
  fix_.source = "tinygpsplus-missing";
  detail_ = "TinyGPSPlus library not available";
  Logger::error("gps", "USE_GPS_DRIVER=1 but TinyGPSPlus.h was not found");
#endif
#else
  setMockFix();
  detail_ = "mock San Francisco fix";
  Logger::info("gps", "mock GPS fix active (USE_GPS_DRIVER=0)");
#endif
}

void SurveyGps::poll() {
#if USE_GPS_DRIVER && SURVEYOPS_HAS_TINYGPSPLUS
  if (!serialReady_) {
    return;
  }
  uint16_t consumed = 0;
  while (Serial1.available() > 0 && consumed < 256) {
    parser_.encode((char)Serial1.read());
    consumed++;
    charsSeen_++;
  }
  if (consumed > 0) {
    updateFromParser("nmea-serial");
  }
#endif
}

bool SurveyGps::feedNmea(const String &sentence) {
#if USE_GPS_DRIVER && SURVEYOPS_HAS_TINYGPSPLUS
  if (!parserReady_) {
    return false;
  }
  for (uint16_t i = 0; i < sentence.length(); i++) {
    parser_.encode(sentence[i]);
    charsSeen_++;
  }
  parser_.encode('\r');
  parser_.encode('\n');
  charsSeen_ += 2;
  updateFromParser("nmea-feed");
  return true;
#else
  (void)sentence;
  return false;
#endif
}

GpsFix SurveyGps::latest() const {
  return fix_;
}

const char *SurveyGps::driverName() const {
#if USE_GPS_DRIVER
#if SURVEYOPS_HAS_TINYGPSPLUS
  return "tinygpsplus";
#else
  return "tinygpsplus-missing";
#endif
#else
  return "mock";
#endif
}

String SurveyGps::statusLine() const {
  String line = String("[gps] driver=") + driverName();
  line += String(" serial=") + (serialReady_ ? "ready" : "off");
  line += String(" fix=") + fix_.coordinateText();
  line += String(" sats=") + String(fix_.satellites);
  line += String(" hdop=") + String(fix_.hdop, 1);
  line += String(" age_ms=") + String(fix_.ageMs);
  line += String(" source=") + fix_.source;
  line += String(" detail=") + detail_;
  return line;
}

void SurveyGps::setMockFix() {
  fix_.valid = true;
  fix_.latitude = 37.774900;
  fix_.longitude = -122.419400;
  fix_.altitudeMeters = 18.0;
  fix_.satellites = 8;
  fix_.hdop = 0.9;
  fix_.ageMs = 120;
  fix_.timestamp = "mock";
  fix_.source = "mock";
}

#if USE_GPS_DRIVER && SURVEYOPS_HAS_TINYGPSPLUS
void SurveyGps::updateFromParser(const char *source) {
  if (!parser_.location.isValid()) {
    detail_ = String("chars=") + String(charsSeen_) + " no valid location yet";
    return;
  }

  fix_.valid = true;
  fix_.latitude = parser_.location.lat();
  fix_.longitude = parser_.location.lng();
  fix_.altitudeMeters = parser_.altitude.isValid() ? parser_.altitude.meters() : 0.0;
  fix_.satellites = parser_.satellites.isValid() ? parser_.satellites.value() : 0;
  fix_.hdop = parser_.hdop.isValid() ? parser_.hdop.hdop() : 0.0;
  fix_.ageMs = parser_.location.age();
  fix_.source = source;

  if (parser_.date.isValid() && parser_.time.isValid()) {
    char stamp[24];
    snprintf(stamp, sizeof(stamp), "%04d-%02d-%02dT%02d:%02d:%02dZ",
             parser_.date.year(), parser_.date.month(), parser_.date.day(),
             parser_.time.hour(), parser_.time.minute(), parser_.time.second());
    fix_.timestamp = stamp;
  } else {
    fix_.timestamp = String("uptime-") + String(millis());
  }

  detail_ = String("chars=") + String(charsSeen_) + " parsed";
}
#endif
