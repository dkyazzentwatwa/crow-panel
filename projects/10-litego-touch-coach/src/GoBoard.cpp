#include "GoBoard.h"

#include <string.h>

namespace litego {
namespace {

// --- Geometry and Zobrist tables -------------------------------------------
// Both are pure functions of the board size, so they are built once by a
// namespace-scope constructor. Point indices max out at 80, which fits int8_t.

struct Tables {
  int8_t neighbors[kPointCount][4];
  int8_t diagonals[kPointCount][4];
  uint64_t zobrist[3][kPointCount];  // index 0 (kEmpty) unused

  Tables() {
    const int8_t nx[4] = {-1, 1, 0, 0};
    const int8_t ny[4] = {0, 0, -1, 1};
    const int8_t dx[4] = {-1, 1, -1, 1};
    const int8_t dy[4] = {-1, -1, 1, 1};

    for (int16_t p = 0; p < kPointCount; p++) {
      int8_t x = (int8_t)pointX(p);
      int8_t y = (int8_t)pointY(p);
      for (uint8_t i = 0; i < 4; i++) {
        int8_t ax = x + nx[i];
        int8_t ay = y + ny[i];
        neighbors[p][i] = (ax >= 0 && ax < (int8_t)kSize && ay >= 0 && ay < (int8_t)kSize)
                              ? (int8_t)(ay * kSize + ax)
                              : (int8_t)-1;
        int8_t bx = x + dx[i];
        int8_t by = y + dy[i];
        diagonals[p][i] = (bx >= 0 && bx < (int8_t)kSize && by >= 0 && by < (int8_t)kSize)
                              ? (int8_t)(by * kSize + bx)
                              : (int8_t)-1;
      }
    }

    // splitmix64 from a fixed seed: the firmware and the host harness must
    // agree on these constants or the shared fixtures would diverge.
    uint64_t seed = 0x9E3779B97F4A7C15ULL;
    for (uint8_t c = 1; c <= 2; c++) {
      for (int16_t p = 0; p < kPointCount; p++) {
        seed += 0x9E3779B97F4A7C15ULL;
        uint64_t z = seed;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        zobrist[c][p] = z ^ (z >> 31);
      }
    }
  }
};

const Tables &tables() {
  static const Tables t;
  return t;
}

}  // namespace

const int8_t *GoBoard::neighbors(int16_t point) { return tables().neighbors[point]; }
const int8_t *GoBoard::diagonals(int16_t point) { return tables().diagonals[point]; }

GoBoard::GoBoard() : komiX2_(13) {
  tables();  // force table construction before any play() call
  memset(mark_, 0, sizeof(mark_));
  memset(libMark_, 0, sizeof(libMark_));
  markEpoch_ = 0;
  libEpoch_ = 0;
  superkoWindow_ = kMaxHistory;
  reset();
}

void GoBoard::reset() {
  memset(points_, kEmpty, sizeof(points_));
  hash_ = 0;
  toMove_ = kBlack;
  moveCount_ = 0;
  consecutivePasses_ = 0;
  blackCaptures_ = 0;
  whiteCaptures_ = 0;
  state_ = kPlaying;
  resignedBy_ = kEmpty;
  lastMovePoint_ = kPass;
  historyCount_ = 0;
  recordHistory(hash_);  // the empty board is itself a superko position
}

void GoBoard::clearMarks() const {
  if (++markEpoch_ == 0) {
    memset(mark_, 0, sizeof(mark_));
    markEpoch_ = 1;
  }
}

void GoBoard::clearLibMarks() const {
  if (++libEpoch_ == 0) {
    memset(libMark_, 0, sizeof(libMark_));
    libEpoch_ = 1;
  }
}

uint8_t GoBoard::collectGroup(const uint8_t *board, int16_t point, uint8_t &liberties) const {
  liberties = 0;
  if (board[point] == kEmpty) {
    return 0;
  }

  const uint8_t color = board[point];
  clearLibMarks();

  uint8_t stones = 0;
  uint8_t top = 0;
  stack_[top++] = point;
  mark_[point] = markEpoch_;

  while (top > 0) {
    int16_t current = stack_[--top];
    // Record the stones as we walk them so callers can remove a captured
    // group without rescanning all 81 points - captures are common enough in
    // playouts for that scan to show up in the throughput.
    groupStones_[stones] = current;
    stones++;
    const int8_t *adj = tables().neighbors[current];
    for (uint8_t i = 0; i < 4; i++) {
      int8_t n = adj[i];
      if (n < 0) {
        continue;
      }
      if (board[n] == kEmpty) {
        if (libMark_[n] != libEpoch_) {
          libMark_[n] = libEpoch_;
          liberties++;
        }
      } else if (board[n] == color && mark_[n] != markEpoch_) {
        mark_[n] = markEpoch_;
        stack_[top++] = n;
      }
    }
  }

  return stones;
}

uint8_t GoBoard::groupLiberties(int16_t point) const {
  if (point < 0 || point >= kPointCount || points_[point] == kEmpty) {
    return 0;
  }
  uint8_t liberties = 0;
  clearMarks();
  collectGroup(points_, point, liberties);
  return liberties;
}

uint8_t GoBoard::countAtariGroups(const uint8_t *board, Color player) const {
  uint8_t atari = 0;
  clearMarks();
  for (int16_t p = 0; p < kPointCount; p++) {
    if (board[p] != player || mark_[p] == markEpoch_) {
      continue;
    }
    uint8_t liberties = 0;
    // collectGroup marks every stone of the group with the current epoch, so
    // the outer scan naturally skips the rest of it.
    collectGroup(board, p, liberties);
    if (liberties == 1) {
      atari++;
    }
  }
  return atari;
}

bool GoBoard::seenPosition(uint64_t hash) const {
  uint16_t depth = historyCount_;
  if (depth > kMaxHistory) {
    depth = kMaxHistory;
  }
  if (depth > superkoWindow_) {
    depth = superkoWindow_;
  }
  for (uint16_t i = 1; i <= depth; i++) {
    if (history_[(uint16_t)(historyCount_ - i) & (kMaxHistory - 1)] == hash) {
      return true;
    }
  }
  return false;
}

bool GoBoard::recordHistory(uint64_t hash) {
  history_[historyCount_ & (kMaxHistory - 1)] = hash;
  historyCount_++;
  return true;
}

bool GoBoard::isTrueEye(int16_t point, Color player) const {
  if (point < 0 || point >= kPointCount || points_[point] != kEmpty) {
    return false;
  }

  const int8_t *adj = tables().neighbors[point];
  uint8_t orthogonal = 0;
  for (uint8_t i = 0; i < 4; i++) {
    if (adj[i] < 0) {
      continue;
    }
    orthogonal++;
    if (points_[adj[i]] != player) {
      return false;
    }
  }

  // Off-board diagonals count as friendly, so an interior point needs 3 of 4
  // friendly diagonals while an edge or corner point needs all of them.
  const int8_t *diag = tables().diagonals[point];
  uint8_t friendly = 0;
  for (uint8_t i = 0; i < 4; i++) {
    if (diag[i] < 0 || points_[diag[i]] == player) {
      friendly++;
    }
  }

  return orthogonal == 4 ? friendly >= 3 : friendly == 4;
}

MoveStatus GoBoard::probe(int16_t point, Color player, uint8_t *work, MoveInfo &info,
                          bool wantAnalysis) const {
  info.status = kMoveOk;
  info.player = player;
  info.point = point;
  info.captures = 0;
  info.liberties = 0;
  info.ownAtariGroups = 0;
  info.opponentAtariGroups = 0;

  if (state_ != kPlaying) {
    return info.status = kMoveGameOver;
  }
  if (point < 0 || point >= kPointCount) {
    return info.status = kMoveOutOfBounds;
  }
  if (points_[point] != kEmpty) {
    return info.status = kMoveOccupied;
  }

  memcpy(work, points_, kPointCount);
  work[point] = player;
  uint64_t nextHash = hash_ ^ tables().zobrist[player][point];

  const Color other = opponentOf(player);
  const int8_t *adj = tables().neighbors[point];
  for (uint8_t i = 0; i < 4; i++) {
    int8_t n = adj[i];
    if (n < 0 || work[n] != other) {
      continue;
    }
    uint8_t liberties = 0;
    clearMarks();
    uint8_t stones = collectGroup(work, n, liberties);
    if (liberties != 0) {
      continue;
    }
    for (uint8_t s = 0; s < stones; s++) {
      int16_t p = groupStones_[s];
      work[p] = kEmpty;
      nextHash ^= tables().zobrist[other][p];
      info.capturedPoints[info.captures++] = (uint8_t)p;
    }
  }

  clearMarks();
  collectGroup(work, point, info.liberties);
  if (info.liberties == 0) {
    return info.status = kMoveSuicide;
  }

  if (seenPosition(nextHash)) {
    return info.status = kMoveKo;
  }

  info.nextHash = nextHash;
  if (wantAnalysis) {
    info.ownAtariGroups = countAtariGroups(work, player);
    info.opponentAtariGroups = countAtariGroups(work, other);
  }
  return kMoveOk;
}

bool GoBoard::isLegal(int16_t point, Color player) const {
  uint8_t work[kPointCount];
  MoveInfo info;
  return probe(point, player, work, info, false) == kMoveOk;
}

bool GoBoard::evaluate(int16_t point, Color player, MoveInfo &info) const {
  uint8_t work[kPointCount];
  return probe(point, player, work, info, true) == kMoveOk;
}

bool GoBoard::evaluateFast(int16_t point, Color player, MoveInfo &info) const {
  uint8_t work[kPointCount];
  return probe(point, player, work, info, false) == kMoveOk;
}

int16_t GoBoard::soleLiberty(int16_t point) const {
  if (point < 0 || point >= kPointCount || points_[point] == kEmpty) {
    return kPass;
  }
  uint8_t liberties = 0;
  clearMarks();
  collectGroup(points_, point, liberties);
  if (liberties != 1) {
    return kPass;
  }
  // collectGroup tagged the group in mark_ and its liberties in libMark_.
  for (int16_t p = 0; p < kPointCount; p++) {
    if (points_[p] == kEmpty && libMark_[p] == libEpoch_) {
      return p;
    }
  }
  return kPass;
}

MoveStatus GoBoard::play(int16_t point, MoveInfo &info, bool wantAnalysis) {
  uint8_t work[kPointCount];
  MoveStatus status = probe(point, toMove_, work, info, wantAnalysis);
  if (status != kMoveOk) {
    return status;
  }

  memcpy(points_, work, kPointCount);
  hash_ = info.nextHash;
  if (toMove_ == kBlack) {
    blackCaptures_ += info.captures;
  } else {
    whiteCaptures_ += info.captures;
  }
  moveCount_++;
  consecutivePasses_ = 0;
  lastMovePoint_ = point;
  toMove_ = opponentOf(toMove_);
  recordHistory(hash_);
  return kMoveOk;
}

MoveStatus GoBoard::pass(MoveInfo &info) {
  info.status = kMovePass;
  info.player = toMove_;
  info.point = kPass;
  info.captures = 0;
  info.liberties = 0;
  info.nextHash = hash_;
  info.ownAtariGroups = 0;
  info.opponentAtariGroups = 0;

  if (state_ != kPlaying) {
    return info.status = kMoveGameOver;
  }

  info.ownAtariGroups = countAtariGroups(points_, toMove_);
  info.opponentAtariGroups = countAtariGroups(points_, opponentOf(toMove_));

  moveCount_++;
  consecutivePasses_++;
  lastMovePoint_ = kPass;
  toMove_ = opponentOf(toMove_);
  if (consecutivePasses_ >= 2) {
    state_ = kFinishedByPasses;
  }
  return kMovePass;
}

void GoBoard::resign(Color who) {
  if (state_ != kPlaying) {
    return;
  }
  state_ = kFinishedByResign;
  resignedBy_ = who;
}

ScoreEstimate GoBoard::score() const {
  ScoreEstimate s;
  s.blackStones = 0;
  s.whiteStones = 0;
  s.blackTerritory = 0;
  s.whiteTerritory = 0;
  s.neutralPoints = 0;
  s.blackCaptures = blackCaptures_;
  s.whiteCaptures = whiteCaptures_;
  s.komiX2 = komiX2_;

  for (int16_t p = 0; p < kPointCount; p++) {
    if (points_[p] == kBlack) {
      s.blackStones++;
    } else if (points_[p] == kWhite) {
      s.whiteStones++;
    }
  }

  // Flood-fill each empty region; a region touching exactly one colour is that
  // colour's territory, anything else is neutral (dame or a contested gap).
  clearMarks();
  for (int16_t start = 0; start < kPointCount; start++) {
    if (points_[start] != kEmpty || mark_[start] == markEpoch_) {
      continue;
    }

    uint8_t top = 0;
    uint8_t region = 0;
    bool touchesBlack = false;
    bool touchesWhite = false;
    stack_[top++] = start;
    mark_[start] = markEpoch_;

    while (top > 0) {
      int16_t current = stack_[--top];
      region++;
      const int8_t *adj = tables().neighbors[current];
      for (uint8_t i = 0; i < 4; i++) {
        int8_t n = adj[i];
        if (n < 0) {
          continue;
        }
        if (points_[n] == kBlack) {
          touchesBlack = true;
        } else if (points_[n] == kWhite) {
          touchesWhite = true;
        } else if (mark_[n] != markEpoch_) {
          mark_[n] = markEpoch_;
          stack_[top++] = n;
        }
      }
    }

    if (touchesBlack && !touchesWhite) {
      s.blackTerritory += region;
    } else if (touchesWhite && !touchesBlack) {
      s.whiteTerritory += region;
    } else {
      s.neutralPoints += region;
    }
  }

  s.blackArea = (int16_t)s.blackStones + (int16_t)s.blackTerritory;
  s.whiteArea = (int16_t)s.whiteStones + (int16_t)s.whiteTerritory;
  s.marginX2 = (int16_t)((s.blackArea - s.whiteArea) * 2 - komiX2_);
  return s;
}

void GoBoard::setPosition(const char *const rows[kSize], Color toMove) {
  reset();
  for (uint8_t y = 0; y < kSize; y++) {
    for (uint8_t x = 0; x < kSize; x++) {
      char c = rows[y][x];
      Color color = c == 'B' ? kBlack : (c == 'W' ? kWhite : kEmpty);
      int16_t p = pointAt(x, y);
      points_[p] = color;
      if (color != kEmpty) {
        hash_ ^= tables().zobrist[color][p];
      }
    }
  }
  toMove_ = toMove;
  historyCount_ = 0;
  recordHistory(hash_);
}

void GoBoard::save(Snapshot &out) const {
  memcpy(out.points, points_, kPointCount);
  out.hash = hash_;
  out.toMove = toMove_;
  out.moveCount = moveCount_;
  out.consecutivePasses = consecutivePasses_;
  out.blackCaptures = blackCaptures_;
  out.whiteCaptures = whiteCaptures_;
  out.state = state_;
  out.resignedBy = resignedBy_;
  out.historyCount = historyCount_;
  out.lastMovePoint = lastMovePoint_;
}

void GoBoard::restore(const Snapshot &in) {
  memcpy(points_, in.points, kPointCount);
  hash_ = in.hash;
  toMove_ = in.toMove;
  moveCount_ = in.moveCount;
  consecutivePasses_ = in.consecutivePasses;
  blackCaptures_ = in.blackCaptures;
  whiteCaptures_ = in.whiteCaptures;
  state_ = in.state;
  resignedBy_ = in.resignedBy;
  lastMovePoint_ = in.lastMovePoint;
  // The superko ring is append-only, so rewinding the count is enough: the
  // slots above it hold stale hashes that the next play() overwrites and
  // seenPosition() never reads.
  historyCount_ = in.historyCount;
}

}  // namespace litego
