#include "WigleCsvLogger.h"

// Include SD_MMC.h directly under the flag - do NOT wrap it in __has_include.
// arduino-cli discovers libraries by preprocessing sources before the library
// is on the include path, so a __has_include guard can silently compile out
// the logger.
#if USE_SD_WIGLE_LOG
#include <SD_MMC.h>
#define SURVEYOPS_HAS_SDMMC_DRIVER 1
#else
#define SURVEYOPS_HAS_SDMMC_DRIVER 0
#endif

namespace {
String csvEscape(const String &value) {
  String out = "\"";
  for (uint16_t i = 0; i < value.length(); i++) {
    char c = value[i];
    if (c == '"') {
      out += "\"\"";
    } else {
      out += c;
    }
  }
  out += "\"";
  return out;
}

String wigleTypeFor(const WifiApRecord &row) {
  (void)row;
  return "WIFI";
}
}  // namespace

void WigleCsvLogger::begin() {
  activeFile_ = nextFileName();
#if USE_SD_WIGLE_LOG
#if SURVEYOPS_HAS_SDMMC_DRIVER
  bool alreadyMounted = SD_MMC.cardType() != CARD_NONE;
  if (!alreadyMounted && !SD_MMC.begin("/sdcard", SURVEYOPS_SDMMC_1BIT != 0)) {
    detail_ = "SD_MMC mount failed";
    Logger::error("wigle", detail_);
    return;
  }
  ready_ = SD_MMC.cardType() != CARD_NONE;
  if (!ready_) {
    detail_ = "SD_MMC card not present";
    Logger::error("wigle", detail_);
    return;
  }
  detail_ = String("SD_MMC ready ") + (SURVEYOPS_SDMMC_1BIT ? "1-bit" : "4-bit");
  if (!ensureHeader()) {
    ready_ = false;
    Logger::error("wigle", detail_);
    return;
  }
  Logger::info("wigle", String("SD WiGLE logger ready file=") + activeFile_);
#else
  detail_ = "SD_MMC.h not available";
  Logger::error("wigle", "USE_SD_WIGLE_LOG=1 but SD_MMC.h was not found");
#endif
#else
  ready_ = true;
  detail_ = "mock logger; no SD writes";
  activeFile_ = "mock_wigle_001.csv";
  Logger::info("wigle", "mock WiGLE CSV logger active (USE_SD_WIGLE_LOG=0)");
#endif
}

bool WigleCsvLogger::setEnabled(bool enabled) {
  if (enabled && !ready_) {
    enabled_ = false;
    return false;
  }
  enabled_ = enabled;
  detail_ = enabled_ ? "logging enabled" : "logging disabled";
  return true;
}

bool WigleCsvLogger::enabled() const {
  return enabled_;
}

bool WigleCsvLogger::logRows(const WifiApRecord *rows, uint8_t count, const GpsFix &fix) {
  if (!enabled_ || count == 0) {
    return true;
  }
  if (rows == nullptr) {
    return false;
  }

#if USE_SD_WIGLE_LOG
#if SURVEYOPS_HAS_SDMMC_DRIVER
  if (!ready_) {
    return false;
  }
  if (!ensureHeader()) {
    return false;
  }
  File file = SD_MMC.open(activeFile_.c_str(), FILE_WRITE);
  if (!file) {
    detail_ = String("open failed ") + activeFile_;
    return false;
  }
  file.seek(file.size());
  for (uint8_t i = 0; i < count; i++) {
    file.print(rows[i].bssid.length() ? rows[i].bssid : "00:00:00:00:00:00");
    file.print(',');
    file.print(csvEscape(rows[i].ssid));
    file.print(',');
    file.print(csvEscape(rows[i].authMode));
    file.print(',');
    file.print(fix.timestamp);
    file.print(',');
    file.print(rows[i].channel);
    file.print(',');
    file.print(rows[i].rssi);
    file.print(',');
    file.print(fix.valid ? String(fix.latitude, 6) : "");
    file.print(',');
    file.print(fix.valid ? String(fix.longitude, 6) : "");
    file.print(',');
    file.print(fix.valid ? String(fix.altitudeMeters, 1) : "");
    file.print(',');
    file.print(fix.valid ? String(fix.hdop, 1) : "");
    file.print(',');
    file.println(wigleTypeFor(rows[i]));
  }
  file.flush();
  file.close();
#else
  return false;
#endif
#else
  (void)rows;
  (void)fix;
#endif

  rowsWritten_ += count;
  rowsInActive_ += count;
  detail_ = String("rows=") + String(rowsWritten_);
  if (rowsInActive_ >= SURVEYOPS_WIGLE_ROTATE_ROWS) {
    rotate();
  }
  return true;
}

bool WigleCsvLogger::rotate() {
  if (!ready_) {
    return false;
  }
  rotations_++;
  fileIndex_++;
  rowsInActive_ = 0;
  activeFile_ = nextFileName();
#if USE_SD_WIGLE_LOG
#if SURVEYOPS_HAS_SDMMC_DRIVER
  return ensureHeader();
#else
  return false;
#endif
#else
  detail_ = String("mock rotated to ") + activeFile_;
  return true;
#endif
}

WigleStorageHealth WigleCsvLogger::health() const {
  WigleStorageHealth h;
  h.flagEnabled = USE_SD_WIGLE_LOG;
  h.ready = ready_;
  h.loggingEnabled = enabled_;
  h.rowsWritten = rowsWritten_;
  h.rotations = rotations_;
  h.activeFile = activeFile_;
  h.detail = detail_;
#if USE_SD_WIGLE_LOG && SURVEYOPS_HAS_SDMMC_DRIVER
  if (ready_) {
    h.detail += String(" total=") + String((uint32_t)(SD_MMC.totalBytes() / 1024)) +
                "KB used=" + String((uint32_t)(SD_MMC.usedBytes() / 1024)) + "KB";
  }
#endif
  return h;
}

String WigleCsvLogger::statusLine() const {
  WigleStorageHealth h = health();
  return String("[storage] sd_flag=") + (h.flagEnabled ? "on" : "off") +
         " ready=" + (h.ready ? "yes" : "no") +
         " logging=" + (h.loggingEnabled ? "on" : "off") +
         " file=" + h.activeFile +
         " rows=" + String(h.rowsWritten) +
         " rotations=" + String(h.rotations) +
         " detail=" + h.detail;
}

String WigleCsvLogger::nextFileName() const {
  String n = String(fileIndex_);
  while (n.length() < 3) {
    n = "0" + n;
  }
#if USE_SD_WIGLE_LOG
  return String(SURVEYOPS_WIGLE_FILE_PREFIX) + "_" + n + ".csv";
#else
  return String("mock_wigle_") + n + ".csv";
#endif
}

#if USE_SD_WIGLE_LOG
bool WigleCsvLogger::ensureHeader() {
#if SURVEYOPS_HAS_SDMMC_DRIVER
  if (!ready_) {
    return false;
  }
  bool exists = SD_MMC.exists(activeFile_.c_str());
  File file = SD_MMC.open(activeFile_.c_str(), FILE_WRITE);
  if (!file) {
    detail_ = String("header open failed ") + activeFile_;
    return false;
  }
  if (!exists || file.size() == 0) {
    file.println("WigleWifi-1.4,appRelease=SurveyOps,model=CrowPanel,release=compile-verified");
    file.println("MAC,SSID,AuthMode,FirstSeen,Channel,RSSI,CurrentLatitude,CurrentLongitude,AltitudeMeters,AccuracyMeters,Type");
  }
  file.close();
  detail_ = String("active ") + activeFile_;
  return true;
#else
  return false;
#endif
}
#endif
