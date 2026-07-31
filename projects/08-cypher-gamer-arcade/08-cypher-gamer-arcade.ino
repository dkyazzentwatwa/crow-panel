#include "config/ProjectConfig.h"
#include <CrowPanelShared.h>
#include "src/ArcadeEngine.h"

ArcadeEngine arcade;
SerialCommandRouter router;
EventLog eventLog;
StorageManager storage;

void cmdStatus(const String &) {
  printSystemStatus(Serial, "cypher-gamer", storage.eventCount(), &router);
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

// Serial mirror of the touch SCORES navigation (catalog SCORES tab / pause
// overlay SCORES button -> kEvShowScores). Keeps touch and serial in parity for
// the high-score screen the same way `catalog` mirrors quit-to-home.
void cmdScores(const String &) {
  arcade.showScores();
  Serial.println(F("[scores] high-score screen (values match `score`)"));
  eventLog.add("Scores opened");
}

void cmdCal(const String &) {
  arcade.printCalibration(Serial);
}

void cmdTouch(const String &) {
  arcade.printTouchDiag(Serial);
}

void cmdSelfTest(const String &) {
  bool ok = arcade.runSelfTest(Serial);
  eventLog.add(ok ? "Selftest PASS" : "Selftest FAIL");
}

// Execute the typed UI event a touch produced this frame through the SAME engine
// methods the serial commands call, so touch and serial stay in parity.
void handleUiEvent(ArcadeEngine::UiEvent ev) {
  switch (ev) {
    case ArcadeEngine::kEvLaunchPong:
      arcade.play("pong", Serial);
      storage.incrementEventCount();
      eventLog.add("Play pong (touch)");
      break;
    case ArcadeEngine::kEvLaunchSnake:
      arcade.play("snake", Serial);
      storage.incrementEventCount();
      eventLog.add("Play snake (touch)");
      break;
    case ArcadeEngine::kEvLaunch2048:
      arcade.play("2048", Serial);
      storage.incrementEventCount();
      eventLog.add("Play 2048 (touch)");
      break;
    case ArcadeEngine::kEvRestart:
      arcade.reset(Serial);
      eventLog.add("Restart (touch)");
      break;
    case ArcadeEngine::kEvQuitToCatalog:
      arcade.showCatalog(Serial);
      eventLog.add("Quit to catalog (touch)");
      break;
    case ArcadeEngine::kEvShowScores:
      arcade.showScores();
      eventLog.add("Scores (touch)");
      break;
    case ArcadeEngine::kEvNone:
    default:
      break;
  }
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
  router.on("reset", "restart active game or refresh catalog", cmdReset);
  router.on("score", "show current and high scores", cmdScore);
  router.on("scores", "open the high-score screen (touch parity)", cmdScores);
  router.on("cal", "show touch calibration and last mapped point", cmdCal);
  router.on("touch", "print raw + mapped touch, tap count, current screen", cmdTouch);
  router.on("selftest", "drive the mock flow headlessly with PASS/FAIL", cmdSelfTest);
}

void loop() {
  router.poll();
  ArcadeEngine::UiEvent ev = arcade.tick();
  handleUiEvent(ev);
  delay(5);
}
