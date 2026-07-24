#ifndef LITEGO_GO_AI_H
#define LITEGO_GO_AI_H

// Monte-Carlo opponent for the 9x9 board. Pure C++ alongside GoBoard so the
// host harness exercises the exact code the firmware runs.
//
// The search is cooperative: start() snapshots the root position, then step()
// is called repeatedly from loop() until it returns true. That keeps the touch
// UI live and the thinking bar moving without a second task or any locking,
// and it makes the search reproducible for the host tests.
#include <stdint.h>

#include "GoBoard.h"

namespace litego {

enum Level : uint8_t {
  kLevelEasy = 0,
  kLevelNormal,
  kLevelHard,
};

struct AiConfig {
  Level level;
  bool heuristicOnly;    // EASY: one-ply scoring, no playouts, instant
  uint32_t maxPlayouts;  // hard cap on total playouts for one move
  uint32_t budgetMs;     // wall-clock cap; ignored unless a clock is installed
  uint8_t atariPercent;  // chance a playout answers a local atari
};

AiConfig aiConfigForLevel(Level level);
const char *levelName(Level level);

// Installs the millisecond clock used for budgetMs. The firmware passes
// millis(); the host harness leaves it unset so budgeting falls back to
// maxPlayouts and every run is byte-for-byte reproducible.
typedef uint32_t (*MillisFn)();
void setAiClock(MillisFn fn);

class GoAi {
 public:
  void begin(const AiConfig &config, uint32_t seed);
  void setConfig(const AiConfig &config) { config_ = config; }
  const AiConfig &config() const { return config_; }

  // Snapshots `board` as the root and builds the candidate list.
  void start(const GoBoard &board);

  // Runs one slice of search. Returns true once the move is decided.
  bool step(uint32_t sliceMs);

  bool thinking() const { return thinking_; }
  int16_t bestMove() const { return bestMove_; }
  uint32_t playouts() const { return playouts_; }
  uint32_t targetPlayouts() const { return targetPlayouts_; }
  uint8_t progressPercent() const;
  // Root win rate in percent for the chosen move; 50 when nothing was searched.
  uint8_t confidencePercent() const { return confidence_; }
  uint8_t candidateCount() const { return candidateCount_; }

 private:
  // Root candidates are every legal non-eye point plus a pass, so the array
  // never needs more than one slot per point plus one.
  static const uint8_t kMaxCandidates = kPointCount + 1;
  static const uint32_t kPlayoutsPerSlice = 96;
  // Virtual playouts each candidate starts with, carrying the one-ply score.
  static const uint32_t kPriorVisits = 8;
  static const int32_t kIllegalScore = -0x7FFFFFFF;

  AiConfig config_;
  GoBoard root_;
  GoBoard sim_;
  Snapshot rootSnapshot_;
  Color rootPlayer_;

  int16_t candidates_[kMaxCandidates];
  uint32_t visits_[kMaxCandidates];
  uint32_t wins_[kMaxCandidates];
  int32_t priorScores_[kMaxCandidates];
  uint32_t priorWins_[kMaxCandidates];  // seeded portion of wins_, for confidence
  uint8_t candidateCount_;
  uint32_t totalVisits_;  // real playouts plus every candidate's priors

  int16_t empties_[kPointCount];
  uint8_t emptyCount_;

  bool thinking_;
  int16_t bestMove_;
  uint32_t playouts_;
  uint32_t targetPlayouts_;
  uint8_t confidence_;
  uint32_t rng_;
  uint32_t sliceStartMs_;

  uint32_t nextRandom();
  uint8_t randomBelow(uint8_t bound);

  void buildCandidates();
  bool shouldAcceptEnd() const;
  // One-ply score for a candidate. `full` adds the two atari scans, which EASY
  // wants and the prior seeding cannot afford once per candidate per move.
  int32_t heuristicScore(int16_t point, bool full) const;
  void seedPriors();
  uint8_t selectCandidate() const;
  // Runs one playout from the root through `candidate`; returns true when the
  // root player wins it.
  bool runPlayout(uint8_t candidate);
  void rebuildEmpties();
  int16_t choosePlayoutMove();
  int16_t localAtariReply();
  void decide();
  void decideHeuristic();
};

}  // namespace litego

#endif
