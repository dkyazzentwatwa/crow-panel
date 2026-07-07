#include "config/ProjectConfig.h"
#include <CrowPanelShared.h>

OpsDashboard dashboard;
SerialCommandRouter router;
EventLog eventLog;
StorageManager storage;
bool loggingOn = false;
uint16_t apCount = 0;
uint16_t rotations = 0;
String lastFix = "37.7749,-122.4194";
String topAp = "StudioNet";

void refreshSurvey(const String &banner) {
  dashboard.setTile(0, "GPS", "fresh", lastFix);
  dashboard.setTile(1, "APs", String(apCount), "passive scan rows");
  dashboard.setTile(2, "Logging", loggingOn ? "ON" : "OFF", "WiGLE CSV mock");
  dashboard.setTile(3, "Top AP", topAp, "strongest signal");
  dashboard.setTile(4, "Rotate", String(rotations), "log files");
  dashboard.setTile(5, "Storage", "ok", "mock SD health");
  dashboard.setBanner(banner);
  dashboard.setFooter("SurveyOps v1 is passive mock wardriving; no join, injection, or active testing");
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

void cmdStatus(const String &) { printSystemStatus(Serial, "surveyops", storage.eventCount()); }
void cmdHistory(const String &) { eventLog.printHistory(Serial); }

void cmdGps(const String &) {
  Serial.println(String("[gps] fix=") + lastFix + " sats=8 hdop=0.9 age=120ms");
  dashboard.setDetail("GPS Fix", String("Lat/Lon ") + lastFix + "|Satellites 8|HDOP 0.9|Fresh mock fix");
  refreshSurvey("GPS fix shown");
}

void cmdScan(const String &) {
  apCount += 3;
  topAp = "StudioNet";
  Serial.println(F("[scan] StudioNet,-42,6,WPA2"));
  Serial.println(F("[scan] GuestLab,-67,11,OPEN"));
  Serial.println(F("[scan] MakerAP,-73,1,WPA3"));
  eventLog.add("Passive Wi-Fi scan");
  dashboard.setDetail("Wi-Fi Scan", "StudioNet ch6 -42|GuestLab ch11 -67|MakerAP ch1 -73");
  refreshSurvey("passive scan complete");
}

void cmdLog(const String &args) {
  String v = args;
  v.trim();
  loggingOn = v != "off";
  Serial.println(loggingOn ? F("[log] on") : F("[log] off"));
  eventLog.add(loggingOn ? "Logging enabled" : "Logging disabled");
  dashboard.setDetail("Logging", loggingOn ? "WiGLE CSV mock logging ON" : "Logging OFF");
  refreshSurvey("logging state changed");
}

void cmdFeed(const String &args) {
  String rest = args;
  String kind = nextWord(rest);
  String ssid = nextWord(rest);
  String rssi = nextWord(rest);
  if (kind == "ap" && ssid.length() > 0) {
    topAp = ssid;
    apCount++;
    Serial.println(String("[feed] ap ssid=") + ssid + " rssi=" + rssi);
    dashboard.setDetail("Fed AP", ssid + "|RSSI " + rssi + "|Added to mock WiGLE row set");
    refreshSurvey("AP row fed");
  } else {
    Serial.println(F("[feed] use: feed ap <ssid> <rssi>"));
  }
}

void cmdRotate(const String &) {
  rotations++;
  Serial.println(String("[rotate] wigle_") + String(rotations) + ".csv");
  eventLog.add("Log rotated");
  dashboard.setDetail("Rotate", String("Opened wigle_") + String(rotations) + ".csv|Previous log closed cleanly");
  refreshSurvey("log rotated");
}

void setup() {
  Logger::begin(115200);
  Logger::info("app", "CrowPanel SurveyOps Wardriver Panel");
  printHardwareProfile(Serial, activeHardwareProfile());
  storage.begin("surveyops");
  dashboard.begin("SURVEYOPS", "WARDRIVER PANEL", "PASSIVE");
  refreshSurvey("survey dashboard ready");
  eventLog.add("SurveyOps booted");
  router.begin(Serial, "surveyops");
  router.on("status", "uptime, heap, profile, flags", cmdStatus);
  router.on("history", "recent events", cmdHistory);
  router.on("gps", "show mock GPS fix", cmdGps);
  router.on("scan", "mock passive Wi-Fi scan", cmdScan);
  router.on("log", "log on|off", cmdLog);
  router.on("feed", "feed ap <ssid> <rssi>", cmdFeed);
  router.on("rotate", "rotate mock WiGLE log", cmdRotate);
}

void loop() {
  router.poll();
  dashboard.tick();
  delay(20);
}
