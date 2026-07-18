#include "LiteGoGame.h"

namespace {
const int8_t kDirs[4][2] = {
    {-1, 0},
    {1, 0},
    {0, -1},
    {0, 1},
};

bool selftestCheck(Print &out, const char *name, bool ok) {
  out.print(F("[selftest] "));
  out.print(name);
  out.println(ok ? F(" PASS") : F(" FAIL"));
  return ok;
}
}

LiteGoGame::LiteGoGame() {
  reset();
}

bool LiteGoGame::runSelfTest(Print &out) {
  bool allOk = true;
  LiteGoGame g;

  g.reset();
  allOk &= selftestCheck(out, "capture setup B 1,0", g.play(1, 0).status == kMoveOk);
  allOk &= selftestCheck(out, "capture setup W 0,0", g.play(0, 0).status == kMoveOk);
  LiteGoGame::MoveResult capture = g.play(0, 1);
  allOk &= selftestCheck(out, "capture move legal", capture.status == kMoveOk);
  allOk &= selftestCheck(out, "capture removes stone",
                         capture.captures == 1 && g.blackCaptures() == 1 && g.at(0, 0) == '.');

  g.reset();
  allOk &= selftestCheck(out, "suicide setup B 0,1", g.play(0, 1).status == kMoveOk);
  allOk &= selftestCheck(out, "suicide setup W 4,4", g.play(4, 4).status == kMoveOk);
  allOk &= selftestCheck(out, "suicide setup B 1,0", g.play(1, 0).status == kMoveOk);
  allOk &= selftestCheck(out, "suicide setup W 5,5", g.play(5, 5).status == kMoveOk);
  allOk &= selftestCheck(out, "suicide setup B 1,1", g.play(1, 1).status == kMoveOk);
  LiteGoGame::MoveResult suicide = g.play(0, 0);
  allOk &= selftestCheck(out, "suicide rejected",
                         suicide.status == kMoveSuicide && g.currentPlayer() == 'W' &&
                             g.moveCount() == 5);

  g.reset();
  allOk &= selftestCheck(out, "cpu move legal", g.cpuMove().status == kMoveOk && g.moveCount() == 1);
  LiteGoGame::ScoreEstimate cpuScore = g.estimateScore();
  allOk &= selftestCheck(out, "score has stones",
                         cpuScore.blackStones + cpuScore.whiteStones > 0 &&
                             cpuScore.neutralPoints < kPointCount);

  LiteGoGame::MoveResult passOne = g.pass();
  LiteGoGame::MoveResult passTwo = g.pass();
  allOk &= selftestCheck(out, "two passes end review",
                         passOne.status == kMovePass && passTwo.status == kMovePass &&
                             g.gameEndedByPasses());
  g.reset();
  allOk &= selftestCheck(out, "reset clears board",
                         g.moveCount() == 0 && g.currentPlayer() == 'B' && g.at(4, 4) == '.');

  const char *koRows[kSize] = {
      ".BW......",
      "BW.W.....",
      ".BW......",
      ".........",
      ".........",
      ".........",
      ".........",
      ".........",
      ".........",
  };
  for (uint8_t y = 0; y < kSize; y++) {
    for (uint8_t x = 0; x < kSize; x++) {
      g.board_[y][x] = koRows[y][x];
      g.previousBoard_[y][x] = '.';
    }
  }
  g.toMove_ = 'B';
  g.moveCount_ = 8;
  g.consecutivePasses_ = 0;
  g.blackCaptures_ = 0;
  g.whiteCaptures_ = 0;
  g.hasPreviousBoard_ = false;
  g.lastCoach_ = "Ko fixture ready.";
  LiteGoGame::MoveResult koCapture = g.play(2, 1);
  LiteGoGame::MoveResult koRecapture = g.play(1, 1);
  allOk &= selftestCheck(out, "ko capture legal",
                         koCapture.status == kMoveOk && koCapture.captures == 1 &&
                             g.blackCaptures() == 1);
  allOk &= selftestCheck(out, "immediate ko recapture rejected", koRecapture.status == kMoveKo);

  out.println(allOk ? F("[selftest] overall PASS") : F("[selftest] overall FAIL"));
  return allOk;
}

void LiteGoGame::reset() {
  for (uint8_t y = 0; y < kSize; y++) {
    for (uint8_t x = 0; x < kSize; x++) {
      board_[y][x] = '.';
      previousBoard_[y][x] = '.';
    }
  }
  toMove_ = 'B';
  moveCount_ = 0;
  consecutivePasses_ = 0;
  blackCaptures_ = 0;
  whiteCaptures_ = 0;
  hasPreviousBoard_ = false;
  lastCoach_ = "Black to move. Corners and sides are efficient on 9x9.";
}

LiteGoGame::MoveResult LiteGoGame::play(int8_t x, int8_t y) {
  char nextBoard[kSize][kSize];
  MoveResult result = evaluateMove(toMove_, x, y, nextBoard);
  if (result.status != kMoveOk) {
    lastCoach_ = buildCoach(result);
    return result;
  }

  copyBoard(board_, previousBoard_);
  hasPreviousBoard_ = true;
  copyBoard(nextBoard, board_);

  if (result.player == 'B') {
    blackCaptures_ += result.captures;
  } else {
    whiteCaptures_ += result.captures;
  }

  moveCount_++;
  consecutivePasses_ = 0;
  toMove_ = opponent(toMove_);
  lastCoach_ = buildCoach(result);
  return result;
}

LiteGoGame::MoveResult LiteGoGame::cpuMove() {
  int8_t bestX = -1;
  int8_t bestY = -1;
  int16_t bestScore = -32768;

  for (int8_t y = 0; y < (int8_t)kSize; y++) {
    for (int8_t x = 0; x < (int8_t)kSize; x++) {
      char nextBoard[kSize][kSize];
      MoveResult candidate = evaluateMove(toMove_, x, y, nextBoard);
      if (candidate.status != kMoveOk) {
        continue;
      }

      int16_t centerBias = 8 - (abs(x - 4) + abs(y - 4));
      int16_t score = centerBias;
      score += (int16_t)candidate.captures * 60;
      score += (int16_t)candidate.opponentAtariGroups * 18;
      score += (int16_t)candidate.liberties * 5;
      score -= (int16_t)candidate.ownAtariGroups * 12;
      if (candidate.liberties <= 1) {
        score -= 25;
      }

      if (score > bestScore) {
        bestScore = score;
        bestX = x;
        bestY = y;
      }
    }
  }

  if (bestX < 0 || bestY < 0) {
    MoveResult result = pass();
    result.status = kMoveNoLegalMove;
    lastCoach_ = "CPU found no legal board point and passed.";
    return result;
  }

  return play(bestX, bestY);
}

LiteGoGame::MoveResult LiteGoGame::pass() {
  MoveResult result;
  result.status = kMovePass;
  result.player = toMove_;
  result.x = -1;
  result.y = -1;
  result.captures = 0;
  result.liberties = 0;
  result.ownAtariGroups = countAtariGroups(board_, toMove_);
  result.opponentAtariGroups = countAtariGroups(board_, opponent(toMove_));

  copyBoard(board_, previousBoard_);
  hasPreviousBoard_ = true;
  moveCount_++;
  consecutivePasses_++;
  toMove_ = opponent(toMove_);
  lastCoach_ = buildCoach(result);
  return result;
}

char LiteGoGame::currentPlayer() const {
  return toMove_;
}

char LiteGoGame::at(uint8_t x, uint8_t y) const {
  if (x >= kSize || y >= kSize) {
    return '.';
  }
  return board_[y][x];
}

uint16_t LiteGoGame::moveCount() const {
  return moveCount_;
}

uint8_t LiteGoGame::consecutivePasses() const {
  return consecutivePasses_;
}

uint8_t LiteGoGame::blackCaptures() const {
  return blackCaptures_;
}

uint8_t LiteGoGame::whiteCaptures() const {
  return whiteCaptures_;
}

bool LiteGoGame::gameEndedByPasses() const {
  return consecutivePasses_ >= 2;
}

String LiteGoGame::boardRow(uint8_t y) const {
  String row;
  if (y >= kSize) {
    return row;
  }
  row.reserve(kSize);
  for (uint8_t x = 0; x < kSize; x++) {
    row += board_[y][x];
  }
  return row;
}

String LiteGoGame::lastCoach() const {
  return lastCoach_;
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
  if (result.status == kMoveNoLegalMove) {
    return String(result.player) + " passed: no legal moves";
  }
  return String("illegal: ") + describeStatus(result.status);
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
      return "simple ko recapture";
    case kMoveNoLegalMove:
      return "no legal moves";
  }
  return "unknown";
}

String LiteGoGame::scoreSummary() const {
  ScoreEstimate score = estimateScore();
  String text = "B ";
  text += String(score.blackArea);
  text += " W ";
  text += String(score.whiteArea);
  text += " margin ";
  text += String(score.margin);
  return text;
}

LiteGoGame::ScoreEstimate LiteGoGame::estimateScore() const {
  ScoreEstimate score;
  score.blackStones = 0;
  score.whiteStones = 0;
  score.blackTerritory = 0;
  score.whiteTerritory = 0;
  score.neutralPoints = 0;
  score.blackCaptures = blackCaptures_;
  score.whiteCaptures = whiteCaptures_;
  score.blackArea = 0;
  score.whiteArea = 0;
  score.margin = 0;

  bool visited[kSize][kSize];
  clearMarks(visited);

  for (uint8_t y = 0; y < kSize; y++) {
    for (uint8_t x = 0; x < kSize; x++) {
      if (board_[y][x] == 'B') {
        score.blackStones++;
      } else if (board_[y][x] == 'W') {
        score.whiteStones++;
      }
    }
  }

  for (uint8_t startY = 0; startY < kSize; startY++) {
    for (uint8_t startX = 0; startX < kSize; startX++) {
      if (board_[startY][startX] != '.' || visited[startY][startX]) {
        continue;
      }

      int8_t stackX[kPointCount];
      int8_t stackY[kPointCount];
      uint8_t top = 0;
      uint8_t region = 0;
      bool touchesBlack = false;
      bool touchesWhite = false;

      stackX[top] = startX;
      stackY[top] = startY;
      top++;
      visited[startY][startX] = true;

      while (top > 0) {
        top--;
        int8_t x = stackX[top];
        int8_t y = stackY[top];
        region++;

        for (uint8_t i = 0; i < 4; i++) {
          int8_t nx = x + kDirs[i][0];
          int8_t ny = y + kDirs[i][1];
          if (!inBounds(nx, ny)) {
            continue;
          }
          char neighbor = board_[ny][nx];
          if (neighbor == 'B') {
            touchesBlack = true;
          } else if (neighbor == 'W') {
            touchesWhite = true;
          } else if (!visited[ny][nx]) {
            visited[ny][nx] = true;
            stackX[top] = nx;
            stackY[top] = ny;
            top++;
          }
        }
      }

      if (touchesBlack && !touchesWhite) {
        score.blackTerritory += region;
      } else if (touchesWhite && !touchesBlack) {
        score.whiteTerritory += region;
      } else {
        score.neutralPoints += region;
      }
    }
  }

  score.blackArea = score.blackStones + score.blackTerritory;
  score.whiteArea = score.whiteStones + score.whiteTerritory;
  score.margin = score.blackArea - score.whiteArea;
  return score;
}

bool LiteGoGame::inBounds(int8_t x, int8_t y) const {
  return x >= 0 && x < (int8_t)kSize && y >= 0 && y < (int8_t)kSize;
}

char LiteGoGame::opponent(char player) const {
  return player == 'B' ? 'W' : 'B';
}

void LiteGoGame::copyBoard(const char source[kSize][kSize], char target[kSize][kSize]) const {
  for (uint8_t y = 0; y < kSize; y++) {
    for (uint8_t x = 0; x < kSize; x++) {
      target[y][x] = source[y][x];
    }
  }
}

bool LiteGoGame::sameBoard(const char left[kSize][kSize], const char right[kSize][kSize]) const {
  for (uint8_t y = 0; y < kSize; y++) {
    for (uint8_t x = 0; x < kSize; x++) {
      if (left[y][x] != right[y][x]) {
        return false;
      }
    }
  }
  return true;
}

void LiteGoGame::clearMarks(bool marks[kSize][kSize]) const {
  for (uint8_t y = 0; y < kSize; y++) {
    for (uint8_t x = 0; x < kSize; x++) {
      marks[y][x] = false;
    }
  }
}

LiteGoGame::GroupInfo LiteGoGame::markGroup(const char board[kSize][kSize], int8_t x,
                                            int8_t y, bool marks[kSize][kSize]) const {
  GroupInfo info;
  info.stones = 0;
  info.liberties = 0;

  if (!inBounds(x, y) || board[y][x] == '.') {
    return info;
  }

  bool libertyMarks[kSize][kSize];
  clearMarks(libertyMarks);

  char color = board[y][x];
  int8_t stackX[kPointCount];
  int8_t stackY[kPointCount];
  uint8_t top = 0;

  stackX[top] = x;
  stackY[top] = y;
  top++;
  marks[y][x] = true;

  while (top > 0) {
    top--;
    int8_t cx = stackX[top];
    int8_t cy = stackY[top];
    info.stones++;

    for (uint8_t i = 0; i < 4; i++) {
      int8_t nx = cx + kDirs[i][0];
      int8_t ny = cy + kDirs[i][1];
      if (!inBounds(nx, ny)) {
        continue;
      }
      if (board[ny][nx] == '.') {
        if (!libertyMarks[ny][nx]) {
          libertyMarks[ny][nx] = true;
          info.liberties++;
        }
      } else if (board[ny][nx] == color && !marks[ny][nx]) {
        marks[ny][nx] = true;
        stackX[top] = nx;
        stackY[top] = ny;
        top++;
      }
    }
  }

  return info;
}

uint8_t LiteGoGame::removeMarked(char board[kSize][kSize], const bool marks[kSize][kSize]) const {
  uint8_t removed = 0;
  for (uint8_t y = 0; y < kSize; y++) {
    for (uint8_t x = 0; x < kSize; x++) {
      if (marks[y][x]) {
        board[y][x] = '.';
        removed++;
      }
    }
  }
  return removed;
}

uint8_t LiteGoGame::countAtariGroups(const char board[kSize][kSize], char player) const {
  bool visited[kSize][kSize];
  clearMarks(visited);
  uint8_t atariGroups = 0;

  for (uint8_t y = 0; y < kSize; y++) {
    for (uint8_t x = 0; x < kSize; x++) {
      if (board[y][x] != player || visited[y][x]) {
        continue;
      }
      bool groupMarks[kSize][kSize];
      clearMarks(groupMarks);
      GroupInfo info = markGroup(board, x, y, groupMarks);
      for (uint8_t gy = 0; gy < kSize; gy++) {
        for (uint8_t gx = 0; gx < kSize; gx++) {
          if (groupMarks[gy][gx]) {
            visited[gy][gx] = true;
          }
        }
      }
      if (info.liberties == 1) {
        atariGroups++;
      }
    }
  }

  return atariGroups;
}

LiteGoGame::MoveResult LiteGoGame::evaluateMove(char player, int8_t x, int8_t y,
                                                char nextBoard[kSize][kSize]) const {
  MoveResult result;
  result.status = kMoveOk;
  result.player = player;
  result.x = x;
  result.y = y;
  result.captures = 0;
  result.liberties = 0;
  result.ownAtariGroups = 0;
  result.opponentAtariGroups = 0;

  copyBoard(board_, nextBoard);

  if (!inBounds(x, y)) {
    result.status = kMoveOutOfBounds;
    return result;
  }
  if (board_[y][x] != '.') {
    result.status = kMoveOccupied;
    return result;
  }

  nextBoard[y][x] = player;
  char other = opponent(player);

  for (uint8_t i = 0; i < 4; i++) {
    int8_t nx = x + kDirs[i][0];
    int8_t ny = y + kDirs[i][1];
    if (!inBounds(nx, ny) || nextBoard[ny][nx] != other) {
      continue;
    }

    bool opponentMarks[kSize][kSize];
    clearMarks(opponentMarks);
    GroupInfo opponentInfo = markGroup(nextBoard, nx, ny, opponentMarks);
    if (opponentInfo.liberties == 0) {
      result.captures += removeMarked(nextBoard, opponentMarks);
    }
  }

  bool ownMarks[kSize][kSize];
  clearMarks(ownMarks);
  GroupInfo ownInfo = markGroup(nextBoard, x, y, ownMarks);
  result.liberties = ownInfo.liberties;
  if (ownInfo.liberties == 0) {
    result.status = kMoveSuicide;
    return result;
  }

  if (hasPreviousBoard_ && sameBoard(nextBoard, previousBoard_)) {
    result.status = kMoveKo;
    return result;
  }

  result.ownAtariGroups = countAtariGroups(nextBoard, player);
  result.opponentAtariGroups = countAtariGroups(nextBoard, other);
  return result;
}

String LiteGoGame::buildCoach(const MoveResult &result) const {
  if (result.status == kMovePass) {
    String text = String(result.player) + " passed. ";
    text += consecutivePasses_ >= 2 ? "Two consecutive passes have ended the review." : "Next pass ends the review.";
    return text;
  }
  if (result.status == kMoveNoLegalMove) {
    return "No legal board point found. Passing is the correct fallback.";
  }
  if (result.status != kMoveOk) {
    return String("Rejected: ") + describeStatus(result.status) + ". Try an empty point with liberties or a capture.";
  }

  String text = String(result.player) + " played " + String(result.x) + "," + String(result.y);
  text += ". Liberties: " + String(result.liberties) + ".";
  if (result.captures > 0) {
    text += " Captured " + String(result.captures) + ".";
  }
  if (result.liberties == 1 || result.ownAtariGroups > 0) {
    text += " Your stones are in atari; connect, extend, or capture.";
  } else if (result.opponentAtariGroups > 0) {
    text += " Opponent has " + String(result.opponentAtariGroups) + " atari group";
    if (result.opponentAtariGroups != 1) {
      text += "s";
    }
    text += ".";
  } else {
    text += " Shape is stable for now.";
  }
  return text;
}
