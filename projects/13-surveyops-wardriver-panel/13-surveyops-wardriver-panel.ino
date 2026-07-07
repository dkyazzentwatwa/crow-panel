#include "config/ProjectConfig.h"
#include "src/SurveyGps.h"
#include "src/WiFiSurveyScanner.h"
#include "src/WigleCsvLogger.h"
#include <CrowPanelShared.h>

OpsDashboard dashboard;
SerialCommandRouter router;
EventLog eventLog;
StorageManager storage;

SurveyGps gpsSource;
WiFiSurveyScanner wifiScanner;
WigleCsvLogger wigleLogger;

WifiApRecord lastRows[kSurveyMaxRows];
uint8_t lastRowCount = 0;
uint16_t apCount = 0;
String topAp = "StudioNet";

void refreshSurvey(const String &banner) {
  GpsFix fix = gpsSource.latest();
  WigleStorageHealth health = wigleLogger.health();
  dashboard.setTile(0, "GPS", fix.valid ? "fix" : "waiting", fix.coordinateText());
  dashboard.setTile(1, "APs", String(apCount), String(lastRowCount) + " last rows");
  dashboard.setTile(2, "Logging", health.loggingEnabled ? "ON" : "OFF", health.activeFile);
  dashboard.setTile(3, "Top AP", topAp, "strongest signal");
  dashboard.setTile(4, "Rotate", String(health.rotations), "log files");
  dashboard.setTile(5, "Storage", health.ready ? "ok" : "check", health.detail);
  dashboard.setBanner(banner);
  dashboard.setFooter("SurveyOps is passive survey/logging only; no join, injection, deauth, or credential capture");
}

String nextWord(String &line) {
  line.trim();
  int space = line.indexOf(' ');
  if (space < 0) {
    String word = line;
    line = "";
    return word;
  }
  String word = line.substring(0, space);
  line = line.substring(space + 1);
  return word;
}

String scanDetail(const WifiApRecord *rows, uint8_t count) {
  if (count == 0) {
    return "No AP rows returned";
  }
  String out;
  uint8_t limit = count < 3 ? count : 3;
  for (uint8_t i = 0; i < limit; i++) {
    if (i > 0) {
      out += "|";
    }
    out += rows[i].ssid + " ch" + String(rows[i].channel) + " " + String(rows[i].rssi) + " dBm";
  }
  return out;
}

void updateTopAp(const WifiApRecord *rows, uint8_t count) {
  if (count == 0) {
    return;
  }
  uint8_t best = 0;
  for (uint8_t i = 1; i < count; i++) {
    if (rows[i].rssi > rows[best].rssi) {
      best = i;
    }
  }
  topAp = rows[best].ssid;
}

void printApRow(const WifiApRecord &row) {
  Serial.println(String("[scan] ssid=") + row.ssid +
                 " bssid=" + row.bssid +
                 " rssi=" + String(row.rssi) +
                 " ch=" + String(row.channel) +
                 " auth=" + row.authMode);
}

void cmdStatus(const String &) {
  printSystemStatus(Serial, "surveyops", storage.eventCount());
  Serial.println(gpsSource.statusLine());
  Serial.println(wifiScanner.statusLine());
  Serial.println(wigleLogger.statusLine());
}

void cmdHistory(const String &) {
  eventLog.printHistory(Serial);
}

void cmdGps(const String &) {
  gpsSource.poll();
  GpsFix fix = gpsSource.latest();
  Serial.println(gpsSource.statusLine());
  dashboard.setDetail("GPS Fix", String("Lat/Lon ") + fix.coordinateText() +
                                     "|Quality " + fix.qualityText() +
                                     "|Source " + fix.source);
  refreshSurvey("GPS fix shown");
}

void cmdScan(const String &) {
  lastRowCount = wifiScanner.scan(lastRows, kSurveyMaxRows);
  Serial.println(wifiScanner.statusLine());
  for (uint8_t i = 0; i < lastRowCount; i++) {
    printApRow(lastRows[i]);
  }
  apCount += lastRowCount;
  updateTopAp(lastRows, lastRowCount);

  if (wigleLogger.enabled() && lastRowCount > 0) {
    bool logged = wigleLogger.logRows(lastRows, lastRowCount, gpsSource.latest());
    Serial.println(logged ? F("[log] scan rows recorded") : F("[log] scan rows not recorded"));
  }

  eventLog.add(lastRowCount > 0 ? "Passive Wi-Fi scan" : "Passive Wi-Fi scan empty");
  dashboard.setDetail("Wi-Fi Scan", scanDetail(lastRows, lastRowCount));
  refreshSurvey(lastRowCount > 0 ? "passive scan complete" : "scan returned no rows");
}

void cmdLog(const String &args) {
  String v = args;
  v.trim();
  if (v.length() == 0) {
    Serial.println(F("[log] use: log on|off"));
    return;
  }
  if (v != "on" && v != "off") {
    Serial.println(F("[log] use: log on|off"));
    return;
  }
  bool ok = wigleLogger.setEnabled(v == "on");
  Serial.println(ok ? (wigleLogger.enabled() ? F("[log] on") : F("[log] off"))
                    : F("[log] unavailable"));
  Serial.println(wigleLogger.statusLine());
  eventLog.add(wigleLogger.enabled() ? "Logging enabled" : "Logging disabled");
  dashboard.setDetail("Logging", wigleLogger.statusLine());
  refreshSurvey("logging state changed");
}

void cmdFeed(const String &args) {
  String rest = args;
  String kind = nextWord(rest);
  String ssid = nextWord(rest);
  String rssi = nextWord(rest);
  if (kind == "ap" && ssid.length() > 0) {
    WifiApRecord row;
    row.ssid = ssid;
    row.bssid = "FE:ED:00:00:00:13";
    row.rssi = rssi.length() > 0 ? rssi.toInt() : -60;
    row.channel = 0;
    row.authMode = "FEED";
    row.seenAtMs = millis();
    lastRows[0] = row;
    lastRowCount = 1;
    topAp = ssid;
    apCount++;
    if (wigleLogger.enabled()) {
      wigleLogger.logRows(&row, 1, gpsSource.latest());
    }
    Serial.println(String("[feed] ap ssid=") + ssid + " rssi=" + String(row.rssi));
    dashboard.setDetail("Fed AP", ssid + "|RSSI " + String(row.rssi) + "|Added to mock WiGLE row set");
    refreshSurvey("AP row fed");
  } else {
    Serial.println(F("[feed] use: feed ap <ssid> <rssi>"));
  }
}

void cmdRotate(const String &) {
  bool ok = wigleLogger.rotate();
  Serial.println(ok ? F("[rotate] ok") : F("[rotate] unavailable"));
  Serial.println(wigleLogger.statusLine());
  eventLog.add(ok ? "Log rotated" : "Log rotate unavailable");
  dashboard.setDetail("Rotate", wigleLogger.statusLine());
  refreshSurvey(ok ? "log rotated" : "log rotate unavailable");
}

void cmdStorage(const String &) {
  Serial.println(wigleLogger.statusLine());
  dashboard.setDetail("Storage", wigleLogger.statusLine());
  refreshSurvey("storage health shown");
}

void cmdNmea(const String &args) {
  String sentence = args;
  sentence.trim();
  if (sentence.length() == 0) {
    Serial.println(F("[nmea] use: nmea <gps sentence>"));
    return;
  }
  bool ok = gpsSource.feedNmea(sentence);
  Serial.println(ok ? F("[nmea] accepted") : F("[nmea] unavailable; compile with USE_GPS_DRIVER=1 and TinyGPSPlus"));
  Serial.println(gpsSource.statusLine());
  dashboard.setDetail("NMEA", gpsSource.statusLine());
  refreshSurvey(ok ? "NMEA parsed" : "NMEA parser unavailable");
}

void setup() {
  Logger::begin(115200);
  Logger::info("app", "CrowPanel SurveyOps Wardriver Panel");
  printHardwareProfile(Serial, activeHardwareProfile());
  storage.begin("surveyops");
  gpsSource.begin();
  wifiScanner.begin();
  wigleLogger.begin();
  dashboard.begin("SURVEYOPS", "WARDRIVER PANEL", "PASSIVE");
  refreshSurvey("survey dashboard ready");
  eventLog.add("SurveyOps booted");
  router.begin(Serial, "surveyops");
  router.on("status", "uptime, heap, profile, flags", cmdStatus);
  router.on("history", "recent events", cmdHistory);
  router.on("gps", "show GPS fix or parser state", cmdGps);
  router.on("scan", "passive Wi-Fi scan rows", cmdScan);
  router.on("log", "log on|off", cmdLog);
  router.on("feed", "feed ap <ssid> <rssi>", cmdFeed);
  router.on("rotate", "rotate WiGLE CSV log", cmdRotate);
  router.on("storage", "WiGLE storage health", cmdStorage);
  router.on("nmea", "nmea <sentence> - parser smoke test", cmdNmea);
}

void loop() {
  gpsSource.poll();
  router.poll();
  dashboard.tick();
  delay(20);
}
