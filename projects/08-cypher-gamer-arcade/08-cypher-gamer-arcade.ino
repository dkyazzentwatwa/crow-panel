#include "config/ProjectConfig.h"
#include <CrowPanelShared.h>
#include "src/ArcadeEngine.h"

ArcadeEngine arcade;
SerialCommandRouter router;
EventLog eventLog;
StorageManager storage;

void cmdStatus(const String &) {
  printSystemStatus(Serial, "cypher-gamer", storage.eventCount());
  arcade.printFlags(Serial);
}

void cmdHistory(const String &) {
  eventLog.printHistory(Serial);
}

void cmdCatalog(const String &) {
  arcade.showCatalog(Serial);
  eventLog.add("Catalog opened");
}

void cmdPlay(const String &args) {
  arcade.play(args, Serial);
  storage.incrementEventCount();
  eventLog.add(String("Play ") + args);
}

void cmdMove(const String &args) {
  arcade.move(args, Serial);
  eventLog.add(String("Move ") + args);
}

void cmdStep(const String &) {
  arcade.step(Serial);
}

void cmdReset(const String &) {
  arcade.reset(Serial);
  eventLog.add("Reset active game");
}

void cmdScore(const String &) {
  arcade.printScore(Serial);
}

void cmdCal(const String &) {
  arcade.printCalibration(Serial);
}

void setup() {
  Logger::begin(115200);
  Logger::info("app", "CrowPanel Cypher Gamer Arcade");
  printHardwareProfile(Serial, activeHardwareProfile());
  storage.begin("cypher-gamer");
  arcade.begin();
  eventLog.add("Cypher Gamer booted");

  router.begin(Serial, "arcade");
  router.on("status", "uptime, heap, profile, flags", cmdStatus);
  router.on("history", "recent events", cmdHistory);
  router.on("catalog", "show playable game catalog", cmdCatalog);
  router.on("play", "play pong|snake|2048", cmdPlay);
  router.on("move", "move up|down|left|right", cmdMove);
  router.on("step", "advance active game one smoke step", cmdStep);
  router.on("reset", "restart active game or refresh menu", cmdReset);
  router.on("score", "show current and high scores", cmdScore);
  router.on("cal", "show touch calibration and last mapped point", cmdCal);
}

void loop() {
  router.poll();
  arcade.tick();
  delay(5);
}
