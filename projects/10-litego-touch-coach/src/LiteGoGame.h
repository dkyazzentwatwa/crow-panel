#ifndef LITEGO_GAME_H
#define LITEGO_GAME_H

#include <Arduino.h>

class LiteGoGame {
 public:
  static const uint8_t kSize = 9;
  static const uint8_t kPointCount = kSize * kSize;

  enum MoveStatus {
    kMoveOk,
    kMovePass,
    kMoveOutOfBounds,
    kMoveOccupied,
    kMoveSuicide,
    kMoveKo,
    kMoveNoLegalMove
  };

  struct MoveResult {
    MoveStatus status;
    char player;
    int8_t x;
    int8_t y;
    uint8_t captures;
    uint8_t liberties;
    uint8_t ownAtariGroups;
    uint8_t opponentAtariGroups;
  };

  struct ScoreEstimate {
    uint8_t blackStones;
    uint8_t whiteStones;
    uint8_t blackTerritory;
    uint8_t whiteTerritory;
    uint8_t neutralPoints;
    uint8_t blackCaptures;
    uint8_t whiteCaptures;
    int16_t blackArea;
    int16_t whiteArea;
    int16_t margin;
  };

  LiteGoGame();

  static bool runSelfTest(Print &out);

  void reset();
  MoveResult play(int8_t x, int8_t y);
  MoveResult cpuMove();
  MoveResult pass();

  char currentPlayer() const;
  char at(uint8_t x, uint8_t y) const;
  uint16_t moveCount() const;
  uint8_t consecutivePasses() const;
  uint8_t blackCaptures() const;
  uint8_t whiteCaptures() const;
  bool gameEndedByPasses() const;

  String boardRow(uint8_t y) const;
  String lastCoach() const;
  String describeMove(const MoveResult &result) const;
  String describeStatus(MoveStatus status) const;
  String scoreSummary() const;
  ScoreEstimate estimateScore() const;

 private:
  struct GroupInfo {
    uint8_t stones;
    uint8_t liberties;
  };

  char board_[kSize][kSize];
  char previousBoard_[kSize][kSize];
  char toMove_;
  uint16_t moveCount_;
  uint8_t consecutivePasses_;
  uint8_t blackCaptures_;
  uint8_t whiteCaptures_;
  bool hasPreviousBoard_;
  String lastCoach_;

  bool inBounds(int8_t x, int8_t y) const;
  char opponent(char player) const;
  void copyBoard(const char source[kSize][kSize], char target[kSize][kSize]) const;
  bool sameBoard(const char left[kSize][kSize], const char right[kSize][kSize]) const;
  void clearMarks(bool marks[kSize][kSize]) const;
  GroupInfo markGroup(const char board[kSize][kSize], int8_t x, int8_t y,
                      bool marks[kSize][kSize]) const;
  uint8_t removeMarked(char board[kSize][kSize], const bool marks[kSize][kSize]) const;
  uint8_t countAtariGroups(const char board[kSize][kSize], char player) const;
  MoveResult evaluateMove(char player, int8_t x, int8_t y, char nextBoard[kSize][kSize]) const;
  String buildCoach(const MoveResult &result) const;
};

#endif
