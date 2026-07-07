#include "config/ProjectConfig.h"
#include <CrowPanelShared.h>
#include "src/LiteGoGame.h"
#include "src/LiteGoTouchView.h"

OpsDashboard dashboard;
SerialCommandRouter router;
EventLog eventLog;
StorageManager storage;
LiteGoGame game;
LiteGoTouchView touchView;

bool parsePoint(const String &args, int8_t &x, int8_t &y) {
  int xi;
  int yi;
  if (sscanf(args.c_str(), "%d %d", &xi, &yi) != 2) {
    return false;
  }
  x = (int8_t)xi;
  y = (int8_t)yi;
  return true;
}

void printBoard() {
  Serial.println(F("[board] x 012345678"));
  for (uint8_t y = 0; y < LiteGoGame::kSize; y++) {
    Serial.print(F("[board] "));
    Serial.print(y);
    Serial.print(F(" "));
    Serial.println(game.boardRow(y));
  }
  Serial.println(String("[board] next=") + String(game.currentPlayer()) +
                 " moves=" + String(game.moveCount()) +
                 " " + game.scoreSummary());
}

void refreshGo(const String &banner) {
  LiteGoGame::ScoreEstimate score = game.estimateScore();
  dashboard.setTile(0, "Board", "9x9", game.gameEndedByPasses() ? "two passes" : "touch or serial");
  dashboard.setTile(1, "To Move", String(game.currentPlayer()), "Black starts");
  dashboard.setTile(2, "Moves", String(game.moveCount()), "plays + passes");
  dashboard.setTile(3, "Caps", String("B") + String(game.blackCaptures()) + " W" + String(game.whiteCaptures()), "prisoners");
  dashboard.setTile(4, "Score", String(score.margin), "rough area");
  dashboard.setTile(5, "Coach", score.neutralPoints > 0 ? "open" : "settled", "liberties + atari");
  dashboard.setBanner(banner);
  dashboard.setFooter("LiteGo v1 is local/offline Go logic; compile-ready is not field-proven");
  touchView.requestRepaint();
}

void reportMove(const LiteGoGame::MoveResult &result, const char *source) {
  Serial.print(F("[go] "));
  Serial.print(source);
  Serial.print(F(" "));
  Serial.println(game.describeMove(result));
  Serial.print(F("[coach] "));
  Serial.println(game.lastCoach());

  if (result.status == LiteGoGame::kMoveOk || result.status == LiteGoGame::kMovePass ||
      result.status == LiteGoGame::kMoveNoLegalMove) {
    eventLog.add(String(source) + " " + game.describeMove(result));
  }

  printBoard();
  dashboard.setDetail("Coach", game.lastCoach() + "|" + game.scoreSummary());
  refreshGo(game.describeMove(result));
}

void cmdStatus(const String &) {
  printSystemStatus(Serial, "litego-coach", storage.eventCount());
  Serial.println(String("[go] next=") + String(game.currentPlayer()) +
                 " moves=" + String(game.moveCount()) +
                 " passes=" + String(game.consecutivePasses()));
}

void cmdHistory(const String &) {
  eventLog.printHistory(Serial);
}

void cmdBoard(const String &) {
  printBoard();
}

void cmdHint(const String &) {
  Serial.print(F("[coach] "));
  Serial.println(game.lastCoach());
  dashboard.setDetail("Coach", game.lastCoach() + "|" + game.scoreSummary());
  refreshGo("coach hint");
}

void cmdPlay(const String &args) {
  int8_t x;
  int8_t y;
  if (!parsePoint(args, x, y)) {
    Serial.println(F("[go] usage: play <x> <y> with coordinates 0-8"));
    dashboard.setDetail("Illegal Move", "Use play <x> <y>|Coordinates are 0-8");
    refreshGo("bad play command");
    return;
  }

  LiteGoGame::MoveResult result = game.play(x, y);
  reportMove(result, "serial");
}

void cmdCpu(const String &) {
  LiteGoGame::MoveResult result = game.cpuMove();
  reportMove(result, "cpu");
}

void cmdPass(const String &) {
  LiteGoGame::MoveResult result = game.pass();
  reportMove(result, "pass");
}

void cmdReset(const String &) {
  game.reset();
  eventLog.add("Board reset");
  Serial.println(F("[go] board reset; Black to move"));
  printBoard();
  dashboard.setDetail("New Game", "9x9 board reset|Black to move|Use play x y or touch");
  refreshGo("new game");
}

void cmdScore(const String &) {
  LiteGoGame::ScoreEstimate score = game.estimateScore();
  Serial.println(String("[score] black_area=") + String(score.blackArea) +
                 " white_area=" + String(score.whiteArea) +
                 " margin=" + String(score.margin));
  Serial.println(String("[score] stones B") + String(score.blackStones) +
                 " W" + String(score.whiteStones) +
                 " territory B" + String(score.blackTerritory) +
                 " W" + String(score.whiteTerritory) +
                 " neutral=" + String(score.neutralPoints));
  dashboard.setDetail("Score Estimate", game.scoreSummary() +
                                      "|Area = stones + enclosed empty points|No komi or seki adjudication");
  refreshGo("score estimate");
}

void setup() {
  Logger::begin(115200);
  Logger::info("app", "CrowPanel LiteGo Touch Coach");
  printHardwareProfile(Serial, activeHardwareProfile());
  storage.begin("litego");
  game.reset();
  dashboard.begin("LITEGO", "TOUCH COACH", "LOCAL");
  touchView.begin(&game);
  refreshGo("coach board ready");
  eventLog.add("LiteGo Touch Coach booted");
  router.begin(Serial, "litego");
  router.on("status", "uptime, heap, profile, flags", cmdStatus);
  router.on("history", "recent events", cmdHistory);
  router.on("board", "print 9x9 board", cmdBoard);
  router.on("hint", "show liberty/atari coach text", cmdHint);
  router.on("play", "play <x> <y>", cmdPlay);
  router.on("cpu", "make simple CPU move", cmdCpu);
  router.on("pass", "pass turn", cmdPass);
  router.on("reset", "reset board", cmdReset);
  router.on("score", "rough area score estimate", cmdScore);
  printBoard();
}

void loop() {
  router.poll();
  dashboard.tick();

  int8_t touchX;
  int8_t touchY;
  if (touchView.tick(touchX, touchY)) {
    LiteGoGame::MoveResult result = game.play(touchX, touchY);
    reportMove(result, "touch");
  }

  delay(20);
}
