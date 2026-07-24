#ifndef LITEGO_GO_BOARD_H
#define LITEGO_GO_BOARD_H

// Pure C++ 9x9 Go rules core. Deliberately free of Arduino.h, String, and
// Print so the identical translation unit compiles for the ESP32-P4 firmware
// and for the host test harness in ../test (see scripts/test-litego.sh).
// Everything the UI or Serial layer needs in text form lives in LiteGoGame.
#include <stdint.h>

namespace litego {

enum Color : uint8_t {
  kEmpty = 0,
  kBlack = 1,
  kWhite = 2,
};

// Board geometry. 9x9 keeps a position in 81 bytes, which is what makes the
// snapshot undo stack and the Monte-Carlo playouts cheap.
static const uint8_t kSize = 9;
static const uint8_t kPointCount = kSize * kSize;

// Sentinel point index used for "pass" everywhere a move point is expected.
static const int16_t kPass = -1;

enum MoveStatus : uint8_t {
  kMoveOk = 0,
  kMovePass,
  kMoveOutOfBounds,
  kMoveOccupied,
  kMoveSuicide,
  kMoveKo,          // positional superko (includes the simple ko case)
  kMoveGameOver,    // board is frozen after two passes or a resignation
};

enum GameState : uint8_t {
  kPlaying = 0,
  kFinishedByPasses,
  kFinishedByResign,
};

// Everything the caller learns about one applied (or rejected) move.
struct MoveInfo {
  MoveStatus status;
  Color player;
  int16_t point;             // kPass for a pass
  uint8_t captures;          // stones removed by this move
  uint8_t liberties;         // liberties of the played group afterwards
  uint8_t ownAtariGroups;    // mover's groups left on 1 liberty
  uint8_t opponentAtariGroups;
  uint64_t nextHash;         // position hash after the move
  uint8_t capturedPoints[kPointCount];  // valid for [0, captures)
};

// Tromp-Taylor area score. Komi is carried doubled so the whole struct stays
// integer; a 6.5 komi is komiX2 == 13 and margins are reported in half points.
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
  int16_t komiX2;
  int16_t marginX2;  // (blackArea - whiteArea) * 2 - komiX2; >0 means Black leads
};

inline Color opponentOf(Color c) { return c == kBlack ? kWhite : kBlack; }
inline uint8_t pointX(int16_t p) { return (uint8_t)(p % kSize); }
inline uint8_t pointY(int16_t p) { return (uint8_t)(p / kSize); }
inline int16_t pointAt(uint8_t x, uint8_t y) { return (int16_t)(y * kSize + x); }

// A complete, restorable position. Copying one of these is how undo works.
struct Snapshot {
  uint8_t points[kPointCount];
  uint64_t hash;
  Color toMove;
  uint16_t moveCount;
  uint8_t consecutivePasses;
  uint8_t blackCaptures;
  uint8_t whiteCaptures;
  GameState state;
  Color resignedBy;
  uint16_t historyCount;
  int16_t lastMovePoint;
};

class GoBoard {
 public:
  // Positional-superko history, a power-of-two ring so the index masks. 512
  // plies is far beyond any 9x9 game, and wrapping simply ages out the oldest
  // positions instead of failing.
  static const uint16_t kMaxHistory = 512;

  GoBoard();

  void reset();
  void setKomiX2(int16_t komiX2) { komiX2_ = komiX2; }
  int16_t komiX2() const { return komiX2_; }

  // How far back superko looks. The real game uses the full history; Monte-
  // Carlo playouts narrow it to a few plies so the per-move scan stays cheap
  // (a playout only needs simple-ko correctness, not full superko).
  void setSuperkoWindow(uint16_t plies) { superkoWindow_ = plies; }

  // Applies the move when legal. On any non-kMoveOk status the board is left
  // untouched, so callers can probe legality with play() and simply discard a
  // rejection. wantAnalysis=false skips the two atari scans, which is the
  // difference between a cheap playout move and a coached one.
  MoveStatus play(int16_t point, MoveInfo &info, bool wantAnalysis = true);
  MoveStatus pass(MoveInfo &info);
  void resign(Color who);

  // Legality probe that never mutates the board.
  bool isLegal(int16_t point, Color player) const;

  // Full non-mutating evaluation: fills `info` with captures, liberties, and
  // atari counts as if the move were played. Returns true when it is legal.
  bool evaluate(int16_t point, Color player, MoveInfo &info) const;

  // Same, minus the two whole-board atari scans. Captures and liberties are
  // still filled in, at roughly the cost of playing the move.
  bool evaluateFast(int16_t point, Color player, MoveInfo &info) const;

  // The one liberty of the group at `point` when it is in atari, otherwise
  // kPass. Drives the playout policy's capture/escape replies.
  int16_t soleLiberty(int16_t point) const;

  // True eye for `player`: every orthogonal neighbour is own colour or edge,
  // and enough diagonals are own colour (off-board diagonals count as own).
  // Playing into one of these is always self-destructive, so both the AI and
  // the coach use it.
  bool isTrueEye(int16_t point, Color player) const;

  Color at(int16_t point) const { return (Color)points_[point]; }
  Color at(uint8_t x, uint8_t y) const { return (Color)points_[pointAt(x, y)]; }
  Color toMove() const { return toMove_; }
  uint16_t moveCount() const { return moveCount_; }
  uint8_t consecutivePasses() const { return consecutivePasses_; }
  uint8_t blackCaptures() const { return blackCaptures_; }
  uint8_t whiteCaptures() const { return whiteCaptures_; }
  GameState state() const { return state_; }
  Color resignedBy() const { return resignedBy_; }
  int16_t lastMovePoint() const { return lastMovePoint_; }
  bool finished() const { return state_ != kPlaying; }
  uint64_t hash() const { return hash_; }

  ScoreEstimate score() const;

  // Liberty count of the group at `point` (0 when the point is empty).
  uint8_t groupLiberties(int16_t point) const;

  void save(Snapshot &out) const;
  void restore(const Snapshot &in);

  // Seeds a position from 9 rows of 9 chars ('.', 'B', 'W'), recomputes the
  // hash, and restarts the superko history from that position. Used by the
  // rules fixtures to set up ko and scoring shapes.
  void setPosition(const char *const rows[kSize], Color toMove);

  // Neighbour table: neighbors(p)[i] is a point index or -1 off-board.
  static const int8_t *neighbors(int16_t point);
  static const int8_t *diagonals(int16_t point);

 private:
  uint8_t points_[kPointCount];
  uint64_t hash_;
  Color toMove_;
  uint16_t moveCount_;
  uint8_t consecutivePasses_;
  uint8_t blackCaptures_;
  uint8_t whiteCaptures_;
  GameState state_;
  Color resignedBy_;
  int16_t lastMovePoint_;
  int16_t komiX2_;
  uint16_t superkoWindow_;
  uint64_t history_[kMaxHistory];
  uint16_t historyCount_;

  // Scratch buffers reused by the flood fills. Members rather than locals so a
  // playout never touches 81-byte stack arrays per move, which is what made
  // the old countAtariGroups path expensive.
  mutable uint8_t mark_[kPointCount];
  mutable uint8_t libMark_[kPointCount];
  mutable int16_t stack_[kPointCount];
  // Stones of the group most recently walked by collectGroup, so a caller can
  // remove or inspect it without a full-board rescan.
  mutable int16_t groupStones_[kPointCount];
  mutable uint8_t markEpoch_;
  mutable uint8_t libEpoch_;

  void clearMarks() const;
  void clearLibMarks() const;

  // Shared legality/capture resolution for play() and isLegal(). Writes the
  // resulting position into `work` (kPointCount bytes) and never touches the
  // board's own state, so a rejected move leaves everything untouched.
  MoveStatus probe(int16_t point, Color player, uint8_t *work, MoveInfo &info,
                   bool wantAnalysis) const;

  // Flood-fills the group at `point` into mark_, returning its stone count and
  // writing the liberty count to `liberties`.
  uint8_t collectGroup(const uint8_t *board, int16_t point, uint8_t &liberties) const;
  uint8_t countAtariGroups(const uint8_t *board, Color player) const;
  bool recordHistory(uint64_t hash);
  bool seenPosition(uint64_t hash) const;
};

}  // namespace litego

#endif
