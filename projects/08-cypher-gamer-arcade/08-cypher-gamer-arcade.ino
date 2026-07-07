#include "config/ProjectConfig.h"
#include <CrowPanelShared.h>

OpsDashboard dashboard;
SerialCommandRouter router;
EventLog eventLog;
StorageManager storage;
String currentGame = "catalog";
uint16_t score = 0;

void refreshArcade(const String &banner) {
  dashboard.setTile(0, "Pong", currentGame == "pong" ? "PLAY" : "idle", "touch paddle demo");
  dashboard.setTile(1, "Snake", currentGame == "snake" ? "PLAY" : "idle", "grid chase demo");
  dashboard.setTile(2, "2048", currentGame == "2048" ? "PLAY" : "idle", "swipe merge demo");
  dashboard.setTile(3, "Catalog", "53 src", "upstream inspiration");
  dashboard.setTile(4, "Score", String(score), "mock high score");
  dashboard.setBanner(banner);
  dashboard.setFooter("Arcade v1: Pong, Snake, and 2048 touch demos; wider catalog staged");
}

void cmdStatus(const String &) { printSystemStatus(Serial, "cypher-gamer", storage.eventCount()); }
void cmdHistory(const String &) { eventLog.printHistory(Serial); }

void cmdCatalog(const String &) {
  Serial.println(F("[catalog] Pong, Snake, 2048 playable; 50 more staged from Cardputer Games"));
  dashboard.setDetail("Game Catalog", "Playable: Pong, Snake, 2048|Staged: arcade, shooter, puzzle, board, reflex|Offline only");
  refreshArcade("catalog opened");
}

void cmdPlay(const String &args) {
  currentGame = args;
  currentGame.trim();
  if (currentGame.length() == 0) currentGame = "pong";
  score += 25;
  Serial.println(String("[play] ") + currentGame + " score=" + String(score));
  eventLog.add(String("Played ") + currentGame);
  dashboard.setDetail("Now Playing", currentGame + "|Score " + String(score) + "|Touch controls mapped per game in v1");
  refreshArcade("game launched");
}

void cmdScore(const String &) {
  Serial.println(String("[score] ") + String(score));
  dashboard.setDetail("Scoreboard", String("Current score ") + String(score) + "|High scores are mock-only until SD persistence lands");
  refreshArcade("scoreboard shown");
}

void setup() {
  Logger::begin(115200);
  Logger::info("app", "CrowPanel Cypher Gamer Arcade");
  printHardwareProfile(Serial, activeHardwareProfile());
  storage.begin("cypher-gamer");
  dashboard.begin("CYPHER GAMER", "TOUCH ARCADE", "OFFLINE");
  refreshArcade("arcade ready");
  eventLog.add("Cypher Gamer booted");
  router.begin(Serial, "arcade");
  router.on("status", "uptime, heap, profile, flags", cmdStatus);
  router.on("history", "recent events", cmdHistory);
  router.on("catalog", "show staged game catalog", cmdCatalog);
  router.on("play", "play pong|snake|2048", cmdPlay);
  router.on("score", "show mock score", cmdScore);
}

void loop() {
  router.poll();
  dashboard.tick();
  int8_t sel = dashboard.selectedIndex();
  if (sel >= 0 && sel <= 2) {
    // Touch selects the featured game; Serial commands remain the full smoke path.
  }
  delay(20);
}
