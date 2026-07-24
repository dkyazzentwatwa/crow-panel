#include "LiteGoGame.h"

#include "GoFixtures.h"

using litego::kBlack;
using litego::kEmpty;
using litego::kPass;
using litego::kWhite;
using litego::MoveInfo;
using litego::ScoreEstimate;

namespace {

uint32_t arduinoMillis() { return (uint32_t)millis(); }

char colorChar(litego::Color c) { return c == kBlack ? 'B' : (c == kWhite ? 'W' : '.'); }

// Renders a doubled half-point value ("13" -> "6.5") without floating point.
String halfPoints(int16_t valueX2) {
  int16_t whole = valueX2 / 2;
  bool half = (valueX2 % 2) != 0;
  String text = String(whole);
  text += half ? ".5" : ".0";
  return text;
}

void emitToPrint(void *context, const char *line) {
  Print *out = (Print *)context;
  if (out != nullptr) {
    out->println(line);
  }
}

}  // namespace

LiteGoGame::LiteGoGame()
    : level_(litego::kLevelNormal),
      humanColor_('B'),
      aiThinking_(false),
      lastMoveX_(-1),
      lastMoveY_(-1),
      undoDepth_(0) {}

void LiteGoGame::begin() {
  // Give the engine the Arduino clock so the level time budgets apply. Without
  // this the search would run to maxPlayouts and block the UI for seconds.
  litego::setAiClock(arduinoMillis);
  ai_.begin(litego::aiConfigForLevel(level_), (uint32_t)micros() ^ 0xA5A5F00Du);
  reset();
}

void LiteGoGame::reset() {
  int16_t komi = board_.komiX2();
  board_.reset();
  board_.setKomiX2(komi);
  undoDepth_ = 0;
  aiThinking_ = false;
  lastMoveX_ = -1;
  lastMoveY_ = -1;
  lastCoach_ = "Black moves first. Corners and sides are efficient on 9x9.";
}

void LiteGoGame::setKomiX2(int16_t komiX2) { board_.setKomiX2(komiX2); }

void LiteGoGame::setLevel(litego::Level level) {
  level_ = level;
  ai_.setConfig(litego::aiConfigForLevel(level));
}

void LiteGoGame::setHumanColor(char color) {
  humanColor_ = (color == 'W' || color == 'w') ? 'W' : 'B';
}

void LiteGoGame::pushUndo(const litego::Snapshot &snapshot) {
  if (undoDepth_ >= kMaxUndo) {
    // Past the cap the oldest snapshot is dropped rather than refusing the
    // move; a 9x9 game never gets here in practice.
    for (uint16_t i = 1; i < kMaxUndo; i++) {
      undoStack_[i - 1] = undoStack_[i];
    }
    undoDepth_ = kMaxUndo - 1;
  }
  undoStack_[undoDepth_++] = snapshot;
}

LiteGoGame::MoveResult LiteGoGame::toResult(const MoveInfo &info) const {
  MoveResult r;
  r.status = (MoveStatus)info.status;
  r.player = colorChar(info.player);
  r.x = info.point == kPass ? -1 : (int8_t)litego::pointX(info.point);
  r.y = info.point == kPass ? -1 : (int8_t)litego::pointY(info.point);
  r.captures = info.captures;
  r.liberties = info.liberties;
  r.ownAtariGroups = info.ownAtariGroups;
  r.opponentAtariGroups = info.opponentAtariGroups;
  for (uint8_t i = 0; i < info.captures && i < kPointCount; i++) {
    r.capturedPoints[i] = info.capturedPoints[i];
  }
  return r;
}

void LiteGoGame::noteMove(const MoveResult &result) {
  if (result.status == kMoveOk) {
    lastMoveX_ = result.x;
    lastMoveY_ = result.y;
  } else if (result.status == kMovePass) {
    lastMoveX_ = -1;
    lastMoveY_ = -1;
  }
  lastCoach_ = buildCoach(result);
}

LiteGoGame::MoveResult LiteGoGame::play(int8_t x, int8_t y) {
  MoveInfo info;
  int16_t point = (x < 0 || y < 0 || x >= (int8_t)kSize || y >= (int8_t)kSize)
                      ? (int16_t)kPointCount
                      : litego::pointAt((uint8_t)x, (uint8_t)y);

  litego::Snapshot before;
  board_.save(before);
  if (board_.play(point, info) == litego::kMoveOk) {
    pushUndo(before);
  }

  MoveResult result = toResult(info);
  // The engine reports out-of-range points as occupied-or-out-of-bounds off a
  // clamped index; restore the caller's coordinates so the message reads right.
  if (result.status == kMoveOutOfBounds) {
    result.x = x;
    result.y = y;
  }
  noteMove(result);
  return result;
}

LiteGoGame::MoveResult LiteGoGame::pass() {
  litego::Snapshot before;
  board_.save(before);
  MoveInfo info;
  if (board_.pass(info) == litego::kMovePass) {
    pushUndo(before);
  }
  MoveResult result = toResult(info);
  noteMove(result);
  return result;
}

void LiteGoGame::resign() {
  // Always the human's colour, never "whoever is to move": RESIGN is pressed by
  // the person holding the panel, and during the opponent's turn the side to
  // move is the opponent - resigning them would hand the human a win.
  litego::Color who = humanColor_ == 'W' ? kWhite : kBlack;
  litego::Snapshot before;
  board_.save(before);
  board_.resign(who);
  if (board_.finished()) {
    pushUndo(before);  // so undo can take back a mis-tap on RESIGN
  }
  lastCoach_ = String(colorChar(who)) + " resigned. " + resultText() + ".";
}

bool LiteGoGame::undo() {
  if (undoDepth_ == 0) {
    return false;
  }
  aiThinking_ = false;
  board_.restore(undoStack_[--undoDepth_]);

  // If that landed on the AI's turn, roll back one more so the human gets
  // their own move back rather than handing the AI a free replay.
  if (undoDepth_ > 0 && currentPlayer() != humanColor_) {
    board_.restore(undoStack_[--undoDepth_]);
  }

  int16_t last = board_.lastMovePoint();
  lastMoveX_ = last == kPass ? -1 : (int8_t)litego::pointX(last);
  lastMoveY_ = last == kPass ? -1 : (int8_t)litego::pointY(last);
  lastCoach_ = String("Took back to move ") + String(board_.moveCount()) + ". " +
               String(currentPlayer()) + " to play.";
  return true;
}

void LiteGoGame::startAiTurn() {
  if (board_.finished()) {
    aiThinking_ = false;
    return;
  }
  ai_.start(board_);
  aiThinking_ = true;
}

bool LiteGoGame::tickAi(uint32_t sliceMs) {
  if (!aiThinking_) {
    return true;
  }
  if (!ai_.step(sliceMs)) {
    return false;
  }
  aiThinking_ = false;
  return true;
}

LiteGoGame::MoveResult LiteGoGame::takeAiMove() {
  int16_t move = ai_.bestMove();
  if (move == kPass) {
    return pass();
  }
  return play((int8_t)litego::pointX(move), (int8_t)litego::pointY(move));
}

LiteGoGame::MoveResult LiteGoGame::cpuMoveBlocking() {
  startAiTurn();
  while (!tickAi(50)) {
  }
  return takeAiMove();
}

char LiteGoGame::currentPlayer() const { return colorChar(board_.toMove()); }

char LiteGoGame::at(uint8_t x, uint8_t y) const {
  if (x >= kSize || y >= kSize) {
    return '.';
  }
  return colorChar(board_.at(x, y));
}

String LiteGoGame::boardRow(uint8_t y) const {
  String row;
  if (y >= kSize) {
    return row;
  }
  row.reserve(kSize);
  for (uint8_t x = 0; x < kSize; x++) {
    row += at(x, y);
  }
  return row;
}

String LiteGoGame::describeStatus(MoveStatus status) const {
  switch (status) {
    case kMoveOk:
      return "ok";
    case kMovePass:
      return "pass";
    case kMoveOutOfBounds:
      return "point out of bounds";
    case kMoveOccupied:
      return "point occupied";
    case kMoveSuicide:
      return "suicide is not legal";
    case kMoveKo:
      return "ko: that repeats a previous position";
    case kMoveGameOver:
      return "game is over";
  }
  return "unknown";
}

String LiteGoGame::describeMove(const MoveResult &result) const {
  if (result.status == kMoveOk) {
    String text = String(result.player) + " at " + String(result.x) + "," + String(result.y);
    text += " captures=" + String(result.captures);
    text += " libs=" + String(result.liberties);
    return text;
  }
  if (result.status == kMovePass) {
    return String(result.player) + " passed";
  }
  return String("illegal: ") + describeStatus(result.status);
}

String LiteGoGame::scoreSummary() const {
  ScoreEstimate s = estimateScore();
  String text = "B ";
  text += String(s.blackArea);
  text += " W ";
  text += String(s.whiteArea);
  text += " komi ";
  text += halfPoints(s.komiX2);
  text += " -> ";
  text += resultText();
  return text;
}

String LiteGoGame::komiText() const { return halfPoints(board_.komiX2()); }

String LiteGoGame::resultText() const {
  if (board_.state() == litego::kFinishedByResign) {
    // The resigning side loses, so the winner is the other colour.
    return String(board_.resignedBy() == kBlack ? 'W' : 'B') + "+R";
  }
  int16_t marginX2 = estimateScore().marginX2;
  if (marginX2 == 0) {
    return "draw";
  }
  return String(marginX2 > 0 ? 'B' : 'W') + "+" + halfPoints(marginX2 > 0 ? marginX2 : -marginX2);
}

String LiteGoGame::buildCoach(const MoveResult &result) const {
  if (result.status == kMovePass) {
    String text = String(result.player) + " passed. ";
    text += board_.finished() ? "Both players passed, so the game is scored: " + resultText() + "."
                              : "Another pass ends the game.";
    return text;
  }
  if (result.status == kMoveGameOver) {
    return "The game is over. Tap NEW GAME to play again.";
  }
  if (result.status != kMoveOk) {
    return String("Rejected: ") + describeStatus(result.status) +
           ". Pick an empty point that keeps a liberty, or capture.";
  }

  String text = String(result.player) + " played " + String(result.x) + "," + String(result.y);
  text += ". Liberties: " + String(result.liberties) + ".";
  if (result.captures > 0) {
    text += " Captured " + String(result.captures) + ".";
  }
  if (result.liberties == 1 || result.ownAtariGroups > 0) {
    text += " Your stones are in atari; connect, extend, or capture.";
  } else if (result.opponentAtariGroups > 0) {
    text += " Opponent has " + String(result.opponentAtariGroups) + " group";
    if (result.opponentAtariGroups != 1) {
      text += "s";
    }
    text += " in atari.";
  } else {
    text += " Shape is stable for now.";
  }
  return text;
}

bool LiteGoGame::runSelfTest(Print &out) {
  litego::TestReport report;
  report.emit = emitToPrint;
  report.context = &out;
  report.passed = 0;
  report.failed = 0;
  bool rulesOk = litego::runRulesFixtures(report);

  // AI hygiene: a short game must stay legal, must never fill its own eye, and
  // must terminate. This is the on-device half of the host harness's check.
  //
  // Heap, not stack: a GoAi carries two boards and their superko rings, so the
  // pair below is about 16 KB - twice the Arduino loop task's stack.
  litego::GoBoard *boardPtr = new litego::GoBoard();
  litego::GoAi *aiPtr = new litego::GoAi();
  if (boardPtr == nullptr || aiPtr == nullptr) {
    delete boardPtr;
    delete aiPtr;
    out.println(F("[selftest] ai hygiene SKIPPED (out of memory)"));
    out.println(rulesOk ? F("[selftest] overall PASS") : F("[selftest] overall FAIL"));
    return rulesOk;
  }
  litego::GoBoard &board = *boardPtr;
  litego::GoAi &ai = *aiPtr;

  litego::AiConfig cfg = litego::aiConfigForLevel(litego::kLevelNormal);
  cfg.maxPlayouts = 120;
  cfg.budgetMs = 40;
  ai.begin(cfg, 0x1234ABCDu);

  bool legal = true;
  bool noEyeFill = true;
  uint16_t plies = 0;
  const uint16_t kCap = 4 * litego::kPointCount;
  MoveInfo info;
  while (!board.finished() && plies < kCap) {
    ai.start(board);
    while (!ai.step(20)) {
    }
    int16_t move = ai.bestMove();
    if (move == kPass) {
      board.pass(info);
    } else {
      if (board.isTrueEye(move, board.toMove())) {
        noEyeFill = false;
      }
      if (board.play(move, info, false) != litego::kMoveOk) {
        legal = false;
        break;
      }
    }
    plies++;
  }
  bool terminated = plies < kCap;
  delete boardPtr;
  delete aiPtr;

  out.println(legal ? F("[selftest] ai plays only legal moves       PASS")
                    : F("[selftest] ai plays only legal moves       FAIL"));
  out.println(noEyeFill ? F("[selftest] ai never fills its own eye      PASS")
                        : F("[selftest] ai never fills its own eye      FAIL"));
  out.println(terminated ? F("[selftest] ai self-play terminates         PASS")
                         : F("[selftest] ai self-play terminates         FAIL"));

  bool allOk = rulesOk && legal && noEyeFill && terminated;
  out.println(allOk ? F("[selftest] overall PASS") : F("[selftest] overall FAIL"));
  return allOk;
}

uint32_t LiteGoGame::benchmark(Print &out, uint32_t milliseconds) {
  // Reuses the session's own searcher rather than putting an 11 KB GoAi on the
  // stack. The caller must have abandoned any in-flight turn first; the level
  // config is restored on the way out.
  aiThinking_ = false;
  litego::AiConfig cfg = litego::aiConfigForLevel(litego::kLevelHard);
  cfg.maxPlayouts = 0xFFFFFFFFu;
  cfg.budgetMs = milliseconds;
  litego::GoAi &ai = ai_;
  ai.setConfig(cfg);

  uint32_t start = millis();
  ai.start(board_);
  while (!ai.step(milliseconds)) {
  }
  uint32_t elapsed = millis() - start;
  ai_.setConfig(litego::aiConfigForLevel(level_));
  if (elapsed == 0) {
    elapsed = 1;
  }
  uint32_t rate = ai.playouts() * 1000UL / elapsed;

  if (ai.playouts() == 0) {
    // The searcher short-circuits when the game is over or the position has no
    // legal non-eye points, so there is nothing to measure from here.
    out.println(F("[bench] no playouts: the position has no searchable moves. "
                  "Run bench during a live game."));
    return 0;
  }

  out.println(String("[bench] ") + String(ai.playouts()) + " playouts in " + String(elapsed) +
              " ms = " + String(rate) + " playouts/sec");
  out.println(String("[bench] candidates=") + String(ai.candidateCount()) + " move=" +
              String(board_.moveCount()) + " level budgets: normal=" +
              String(litego::aiConfigForLevel(litego::kLevelNormal).budgetMs) + "ms hard=" +
              String(litego::aiConfigForLevel(litego::kLevelHard).budgetMs) + "ms");
  return rate;
}
