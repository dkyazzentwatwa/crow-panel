#include "config/ProjectConfig.h"
#include <CrowPanelShared.h>
#include "src/LiteGoGame.h"
#include "src/LiteGoTouchView.h"

SerialCommandRouter router;
EventLog eventLog;
StorageManager storage;
LiteGoGame game;
LiteGoTouchView touchView;

// True while the opponent's search is running so loop() keeps slicing it and
// the sketch does not accept a second move on top of it.
bool aiTurnActive = false;

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
                 " moves=" + String(game.moveCount()) + " " + game.scoreSummary());
}

void reportMove(const LiteGoGame::MoveResult &result, const char *source) {
  Serial.print(F("[go] "));
  Serial.print(source);
  Serial.print(F(" "));
  Serial.println(game.describeMove(result));
  Serial.print(F("[coach] "));
  Serial.println(game.lastCoach());

  if (result.status == LiteGoGame::kMoveOk || result.status == LiteGoGame::kMovePass) {
    eventLog.add(String(source) + " " + game.describeMove(result));
  }

  printBoard();
  touchView.noteMoveResult(result, source);
}

// Starts the opponent's search when it is their turn and the game is live.
void maybeStartAiTurn() {
  if (aiTurnActive || game.finished() || game.humanToMove()) {
    return;
  }
  game.startAiTurn();
  aiTurnActive = true;
  touchView.setThinking(true);
}

// Abandons a search in progress. Any command that changes the board has to
// call this first, or loop() would apply the stale result on top of the new
// position as soon as the slice finished.
void cancelAiTurn() {
  if (!aiTurnActive) {
    return;
  }
  aiTurnActive = false;
  touchView.setThinking(false);
}

void cmdStatus(const String &) {
  printSystemStatus(Serial, "litego-coach", storage.eventCount());
  Serial.println(String("[go] next=") + String(game.currentPlayer()) +
                 " moves=" + String(game.moveCount()) +
                 " passes=" + String(game.consecutivePasses()) + " level=" + game.levelName() +
                 " komi=" + game.komiText() + " you=" + String(game.humanColor()) +
                 (game.finished() ? String(" result=") + game.resultText() : String()));
}

void cmdHistory(const String &) { eventLog.printHistory(Serial); }

void cmdBoard(const String &) { printBoard(); }

void cmdHint(const String &) {
  if (game.finished()) {
    Serial.println(F("[hint] game over"));
    touchView.setStatus(String("Game over. ") + game.resultText() + ".");
    return;
  }
  if (!game.humanToMove()) {
    Serial.println(F("[hint] not your turn"));
    touchView.setStatus("Wait for your turn to ask for a hint.");
    return;
  }
  // Brief blocking search for the human's side; the ~0.8 s pause is fine for a
  // button press and the suggestion lands as a green marker on the board.
  cancelAiTurn();
  int16_t suggestion = game.suggestMove();
  if (suggestion < 0) {
    touchView.setHint(-1, -1);
    Serial.println(F("[hint] passing is reasonable here"));
    touchView.setStatus("Hint: passing is reasonable here.");
    return;
  }
  int8_t hx = (int8_t)(suggestion % LiteGoGame::kSize);
  int8_t hy = (int8_t)(suggestion / LiteGoGame::kSize);
  touchView.setHint(hx, hy);
  Serial.println(String("[hint] try ") + String(hx) + "," + String(hy));
  touchView.setStatus(String("Hint: try ") + String(hx) + "," + String(hy) +
                      " (green marker). Tap it twice to play there.");
}

void cmdPlay(const String &args) {
  cancelAiTurn();
  int8_t x;
  int8_t y;
  if (!parsePoint(args, x, y)) {
    Serial.println(F("[go] usage: play <x> <y> with coordinates 0-8"));
    touchView.setStatus("Use play <x> <y> with coordinates 0-8.", true);
    return;
  }
  reportMove(game.play(x, y), "serial");
  maybeStartAiTurn();
}

void cmdCpu(const String &) {
  cancelAiTurn();
  // Blocking on purpose: a Serial user asked for one move and wants it now.
  reportMove(game.cpuMoveBlocking(), "cpu");
}

void cmdPass(const String &) {
  cancelAiTurn();
  reportMove(game.pass(), "pass");
  maybeStartAiTurn();
}

void cmdReset(const String &) {
  cancelAiTurn();
  game.reset();
  touchView.clearGhost();
  touchView.requestFullRepaint();
  eventLog.add("New game");
  Serial.println(F("[go] new game; Black to move"));
  printBoard();
  touchView.setStatus("New game. Tap an intersection to preview, tap again to place.");
  maybeStartAiTurn();
}

void cmdUndo(const String &) {
  cancelAiTurn();
  if (!game.undo()) {
    Serial.println(F("[go] nothing to undo"));
    touchView.setStatus("Nothing to undo yet.", true);
    return;
  }
  touchView.clearGhost();
  touchView.requestBoardRepaint();
  touchView.requestScoreRepaint();
  touchView.setStatus(game.lastCoach());
  Serial.println(String("[go] undo -> move ") + String(game.moveCount()) + " " +
                 String(game.currentPlayer()) + " to play");
  printBoard();
  // Normally undo lands on the human's turn and this is a no-op, but undoing a
  // resignation can leave the opponent to move - without this the game would
  // sit there with nobody searching.
  maybeStartAiTurn();
}

void cmdResign(const String &) {
  cancelAiTurn();
  game.resign();
  touchView.requestFullRepaint();
  eventLog.add(String("Resigned: ") + game.resultText());
  Serial.println(String("[go] resigned; result ") + game.resultText());
  touchView.setStatus(String("Resigned. Result ") + game.resultText() + ".");
}

void cmdScore(const String &) {
  litego::ScoreEstimate s = game.estimateScore();
  Serial.println(String("[score] black_area=") + String(s.blackArea) +
                 " white_area=" + String(s.whiteArea) + " komi=" + game.komiText() +
                 " result=" + game.resultText());
  Serial.println(String("[score] stones B") + String(s.blackStones) + " W" +
                 String(s.whiteStones) + " territory B" + String(s.blackTerritory) + " W" +
                 String(s.whiteTerritory) + " neutral=" + String(s.neutralPoints));
  touchView.setStatus(String("Score: ") + game.scoreSummary() +
                      ". Area scoring, no dead-stone marking - play the dame out.");
  touchView.requestScoreRepaint();
}

void cmdLevel(const String &args) {
  String value = args;
  value.trim();
  value.toLowerCase();
  if (value == "easy") {
    game.setLevel(litego::kLevelEasy);
  } else if (value == "normal") {
    game.setLevel(litego::kLevelNormal);
  } else if (value == "hard") {
    game.setLevel(litego::kLevelHard);
  } else if (value.length() > 0) {
    Serial.println(F("[go] usage: level <easy|normal|hard>"));
    return;
  }
  Serial.println(String("[go] level=") + game.levelName());
  touchView.setStatus(String("Opponent level: ") + game.levelName() + ".");
  touchView.requestScoreRepaint();
}

void cmdKomi(const String &args) {
  String value = args;
  value.trim();
  if (value.length() > 0) {
    // Parsed as halves so the engine's integer scoring stays exact.
    float komi = value.toFloat();
    game.setKomiX2((int16_t)lroundf(komi * 2.0f));
  }
  Serial.println(String("[go] komi=") + game.komiText());
  touchView.setStatus(String("Komi set to ") + game.komiText() + ".");
  touchView.requestScoreRepaint();
}

void cmdColor(const String &args) {
  cancelAiTurn();
  String value = args;
  value.trim();
  if (value.length() > 0) {
    game.setHumanColor(value.charAt(0) == 'w' || value.charAt(0) == 'W' ? 'W' : 'B');
  }
  Serial.println(String("[go] you play ") + String(game.humanColor()));
  touchView.setStatus(String("You play ") + (game.humanColor() == 'B' ? "Black." : "White."));
  touchView.requestScoreRepaint();
  touchView.requestFullRepaint();
  maybeStartAiTurn();
}

void cmdSelfTest(const String &) {
  bool ok = LiteGoGame::runSelfTest(Serial);
  eventLog.add(ok ? "Selftest PASS" : "Selftest FAIL");
  touchView.setStatus(ok ? "Selftest PASS: rules fixtures and AI hygiene all green."
                         : "Selftest FAIL. Check Serial output for the failing check.",
                      !ok);
}

void cmdBench(const String &args) {
  cancelAiTurn();  // benchmark borrows the session's searcher
  String value = args;
  value.trim();
  uint32_t ms = value.length() > 0 ? (uint32_t)value.toInt() : 1000;
  if (ms < 100) {
    ms = 100;
  }
  uint32_t rate = game.benchmark(Serial, ms);
  touchView.setStatus(String("Bench: ") + String(rate) +
                      " playouts/sec. Tune level budgets from this.");
}

void cmdTouchCal(const String &) {
  touchView.reportCalibration(Serial);
  // Also on screen: USB-CDC serial on this board drops once the app runs, so
  // the panel has to be able to show the calibration on its own.
  touchView.setStatus(touchView.calibrationSummary());
}

void cmdAutoplay(const String &args) {
  cancelAiTurn();
  String value = args;
  value.trim();
  int moves = value.length() > 0 ? value.toInt() : 10;
  if (moves < 1) {
    moves = 1;
  }
  for (int i = 0; i < moves && !game.finished(); i++) {
    LiteGoGame::MoveResult result = game.cpuMoveBlocking();
    Serial.print(F("[autoplay] "));
    Serial.println(game.describeMove(result));
  }
  printBoard();
  touchView.requestFullRepaint();
}

void handleAction(const LiteGoTouchView::Action &action) {
  switch (action.type) {
    case LiteGoTouchView::kActionPlay:
      reportMove(game.play(action.x, action.y), "touch");
      maybeStartAiTurn();
      break;
    case LiteGoTouchView::kActionPass:
      reportMove(game.pass(), "touch-pass");
      maybeStartAiTurn();
      break;
    case LiteGoTouchView::kActionUndo:
      cmdUndo(String());
      break;
    case LiteGoTouchView::kActionResign:
      cmdResign(String());
      break;
    case LiteGoTouchView::kActionScore:
      cmdScore(String());
      break;
    case LiteGoTouchView::kActionHint:
      cmdHint(String());
      break;
    case LiteGoTouchView::kActionNewGame:
      cmdReset(String());
      break;
    case LiteGoTouchView::kActionLevel: {
      // Cycles easy -> normal -> hard -> easy.
      litego::Level next = game.level() == litego::kLevelEasy     ? litego::kLevelNormal
                           : game.level() == litego::kLevelNormal ? litego::kLevelHard
                                                                  : litego::kLevelEasy;
      game.setLevel(next);
      Serial.println(String("[go] level=") + game.levelName());
      touchView.setStatus(String("Opponent level: ") + game.levelName() + ".");
      touchView.requestScoreRepaint();
      break;
    }
    case LiteGoTouchView::kActionColor:
      cancelAiTurn();
      game.setHumanColor(game.humanColor() == 'B' ? 'W' : 'B');
      Serial.println(String("[go] you play ") + String(game.humanColor()));
      touchView.setStatus(String("You play ") + (game.humanColor() == 'B' ? "Black." : "White."));
      touchView.requestScoreRepaint();
      maybeStartAiTurn();
      break;
    case LiteGoTouchView::kActionNone:
      break;
  }
}

void setup() {
  Logger::begin(115200);
  Logger::info("app", "CrowPanel LiteGo Touch Coach");
  printHardwareProfile(Serial, activeHardwareProfile());
  storage.begin("litego");

  game.begin();
  game.setKomiX2(LITEGO_KOMI_X2);
  game.setLevel((litego::Level)LITEGO_DEFAULT_LEVEL);
  game.setHumanColor(LITEGO_HUMAN_COLOR);

  touchView.begin(&game);
  touchView.setStatus("Tap an intersection to preview, tap it again to place.");
  eventLog.add("LiteGo Touch Coach booted");

  router.begin(Serial, "litego");
  router.on("status", "uptime, heap, profile, game settings", cmdStatus);
  router.on("history", "recent events", cmdHistory);
  router.on("board", "print 9x9 board", cmdBoard);
  router.on("hint", "show liberty/atari coach text", cmdHint);
  router.on("play", "play <x> <y>", cmdPlay);
  router.on("cpu", "make one opponent move now", cmdCpu);
  router.on("pass", "pass turn", cmdPass);
  router.on("undo", "take back your last move", cmdUndo);
  router.on("resign", "resign the game", cmdResign);
  router.on("new", "start a new game", cmdReset);
  router.on("reset", "start a new game", cmdReset);
  router.on("score", "area score with komi", cmdScore);
  router.on("level", "level <easy|normal|hard>", cmdLevel);
  router.on("komi", "komi <points>", cmdKomi);
  router.on("color", "color <b|w> - the side you play", cmdColor);
  router.on("selftest", "run rules fixtures and AI hygiene", cmdSelfTest);
  router.on("bench", "bench <ms> - measure playouts/sec", cmdBench);
  router.on("touchcal", "print raw and mapped touch coordinates", cmdTouchCal);
  router.on("autoplay", "autoplay <n> - opponent plays n moves", cmdAutoplay);

  printBoard();
  maybeStartAiTurn();
}

void loop() {
  router.poll();

  // Slice the opponent's search so touch stays live while it thinks.
  if (aiTurnActive) {
    if (game.tickAi(LITEGO_AI_SLICE_MS)) {
      aiTurnActive = false;
      touchView.setThinking(false);
      // Capture the search stats before takeAiMove() applies the move.
      uint32_t playouts = game.aiPlayouts();
      uint8_t confidence = game.aiConfidencePercent();
      reportMove(game.takeAiMove(), "cpu");
      Serial.println(String("[cpu] ") + game.levelName() + " searched " + String(playouts) +
                     " playouts, " + String(confidence) + "% confident");
      // On-screen, because USB-CDC serial is gone by now: this is the real
      // on-device search depth the level budgets should be tuned against.
      // Skipped for easy, which does a single heuristic ply (0 playouts).
      if (playouts > 0) {
        touchView.setStatus(String("Opponent (") + game.levelName() + ") searched " +
                            String(playouts) + " playouts, " + String(confidence) +
                            "% confident.");
      }
    }
  }

  LiteGoTouchView::Action action;
  if (touchView.tick(action)) {
    handleAction(action);
  }

  delay(2);
}
