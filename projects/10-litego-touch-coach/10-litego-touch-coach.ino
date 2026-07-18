#include "config/ProjectConfig.h"
#include <CrowPanelShared.h>
#include "src/LiteGoGame.h"
#include "src/LiteGoTouchView.h"

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
  touchView.setStatus(banner);
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
  touchView.setLastResult(result, source);
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
  refreshGo(String("Hint: ") + game.lastCoach());
}

void cmdPlay(const String &args) {
  int8_t x;
  int8_t y;
  if (!parsePoint(args, x, y)) {
    Serial.println(F("[go] usage: play <x> <y> with coordinates 0-8"));
    touchView.setStatus("Use play <x> <y> with coordinates 0-8.", true);
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
  touchView.clearLastMove();
  eventLog.add("Board reset");
  Serial.println(F("[go] board reset; Black to move"));
  printBoard();
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
  refreshGo(String("Score: ") + game.scoreSummary() +
            ". Territory B" + String(score.blackTerritory) +
            " W" + String(score.whiteTerritory) +
            " neutral " + String(score.neutralPoints) +
            ". No komi or seki adjudication.");
}

void cmdSelfTest(const String &) {
  bool ok = LiteGoGame::runSelfTest(Serial);
  eventLog.add(ok ? "Selftest PASS" : "Selftest FAIL");
  touchView.setStatus(ok ? "Selftest PASS. Serial scenarios covered capture, suicide, pass, score, CPU, and ko."
                         : "Selftest FAIL. Check Serial output for the failing scenario.",
                      !ok);
}

void setup() {
  Logger::begin(115200);
  Logger::info("app", "CrowPanel LiteGo Touch Coach");
  printHardwareProfile(Serial, activeHardwareProfile());
  storage.begin("litego");
  game.reset();
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
  router.on("selftest", "run rules smoke scenarios", cmdSelfTest);
  printBoard();
}

void loop() {
  router.poll();

  LiteGoTouchView::Action action;
  if (touchView.tick(action)) {
    switch (action.type) {
      case LiteGoTouchView::kActionPlay:
        reportMove(game.play(action.x, action.y), "touch");
        break;
      case LiteGoTouchView::kActionPass:
        reportMove(game.pass(), "touch-pass");
        break;
      case LiteGoTouchView::kActionCpu:
        reportMove(game.cpuMove(), "touch-cpu");
        break;
      case LiteGoTouchView::kActionReset:
        cmdReset(String());
        break;
      case LiteGoTouchView::kActionScore:
        cmdScore(String());
        break;
      case LiteGoTouchView::kActionHint:
        cmdHint(String());
        break;
      case LiteGoTouchView::kActionNone:
        break;
    }
  }

  delay(20);
}
