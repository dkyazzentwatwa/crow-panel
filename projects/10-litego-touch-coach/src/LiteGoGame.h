#ifndef LITEGO_GAME_H
#define LITEGO_GAME_H

#include <Arduino.h>

#include "GoAi.h"
#include "GoBoard.h"

// Arduino-facing session layer over the pure C++ engine. Owns the undo stack,
// the AI driver, and every String the UI and Serial commands need - the core
// itself never allocates one.
class LiteGoGame {
 public:
  static const uint8_t kSize = litego::kSize;
  static const uint8_t kPointCount = litego::kPointCount;
  // 200 plies is well past the end of any 9x9 game, and a snapshot is ~100 B,
  // so the whole stack costs about 20 KB of static RAM.
  static const uint16_t kMaxUndo = 200;

  // Mirrors of the engine enums so the sketch and view do not need to reach
  // into the litego namespace for the common cases.
  enum MoveStatus {
    kMoveOk = litego::kMoveOk,
    kMovePass = litego::kMovePass,
    kMoveOutOfBounds = litego::kMoveOutOfBounds,
    kMoveOccupied = litego::kMoveOccupied,
    kMoveSuicide = litego::kMoveSuicide,
    kMoveKo = litego::kMoveKo,
    kMoveGameOver = litego::kMoveGameOver,
  };

  struct MoveResult {
    MoveStatus status;
    char player;   // 'B' or 'W'
    int8_t x;
    int8_t y;
    uint8_t captures;
    uint8_t liberties;
    uint8_t ownAtariGroups;
    uint8_t opponentAtariGroups;
    uint8_t capturedPoints[kPointCount];
  };

  LiteGoGame();

  void begin();

  // --- game flow ------------------------------------------------------------
  void reset();
  MoveResult play(int8_t x, int8_t y);
  MoveResult pass();
  void resign();               // the side to move resigns
  bool undo();                 // rolls back to the human's previous turn
  bool canUndo() const { return undoDepth_ > 0; }

  // --- configuration --------------------------------------------------------
  void setKomiX2(int16_t komiX2);
  int16_t komiX2() const { return board_.komiX2(); }
  void setLevel(litego::Level level);
  litego::Level level() const { return level_; }
  void setHumanColor(char color);
  char humanColor() const { return humanColor_; }
  bool humanToMove() const { return currentPlayer() == humanColor_; }

  // --- AI driver ------------------------------------------------------------
  // Kicks off a search for the side to move. tickAi() must then be called each
  // loop until it returns true, at which point takeAiMove() applies the move.
  void startAiTurn();
  bool aiThinking() const { return aiThinking_; }
  bool tickAi(uint32_t sliceMs);
  MoveResult takeAiMove();
  uint8_t aiProgressPercent() const { return ai_.progressPercent(); }
  uint8_t aiConfidencePercent() const { return ai_.confidencePercent(); }
  uint32_t aiPlayouts() const { return ai_.playouts(); }
  // Blocking search used by the Serial `cpu` command and `autoplay`.
  MoveResult cpuMoveBlocking();

  // --- queries --------------------------------------------------------------
  char currentPlayer() const;
  char at(uint8_t x, uint8_t y) const;
  uint16_t moveCount() const { return board_.moveCount(); }
  uint8_t consecutivePasses() const { return board_.consecutivePasses(); }
  uint8_t blackCaptures() const { return board_.blackCaptures(); }
  uint8_t whiteCaptures() const { return board_.whiteCaptures(); }
  bool finished() const { return board_.finished(); }
  bool gameEndedByPasses() const { return board_.state() == litego::kFinishedByPasses; }
  int8_t lastMoveX() const { return lastMoveX_; }
  int8_t lastMoveY() const { return lastMoveY_; }
  bool hasLastMove() const { return lastMoveX_ >= 0; }
  const litego::GoBoard &board() const { return board_; }
  litego::ScoreEstimate estimateScore() const { return board_.score(); }

  // --- text -----------------------------------------------------------------
  String boardRow(uint8_t y) const;
  String lastCoach() const { return lastCoach_; }
  String describeMove(const MoveResult &result) const;
  String describeStatus(MoveStatus status) const;
  String scoreSummary() const;
  String resultText() const;   // "B+7.5", "W+3.5", "W+R"
  String komiText() const;     // "6.5"
  const char *levelName() const { return litego::levelName(level_); }

  // Runs the shared rules fixtures plus a short AI hygiene check on Serial.
  static bool runSelfTest(Print &out);
  // Measures playouts/sec on this board, which is what the level budgets are
  // tuned against. Returns playouts per second.
  uint32_t benchmark(Print &out, uint32_t milliseconds);

 private:
  litego::GoBoard board_;
  litego::GoAi ai_;
  litego::Level level_;
  char humanColor_;
  bool aiThinking_;
  int8_t lastMoveX_;
  int8_t lastMoveY_;
  String lastCoach_;

  litego::Snapshot undoStack_[kMaxUndo];
  uint16_t undoDepth_;

  void pushUndo(const litego::Snapshot &snapshot);
  MoveResult toResult(const litego::MoveInfo &info) const;
  void noteMove(const MoveResult &result);
  String buildCoach(const MoveResult &result) const;
};

#endif
