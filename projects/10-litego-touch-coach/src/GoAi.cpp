#include "GoAi.h"

#include <math.h>
#include <stdlib.h>

// Same reasoning as GoBoard.cpp: this is the search hot path, so override the
// platform's default -Os with -O3 for throughput on the P4. Guarded to real
// GCC; the host uses clang, which rejects this pragma.
#if !defined(LITEGO_HOST_BUILD) && defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("O3")
#endif

namespace litego {
namespace {

MillisFn gClock = nullptr;

// UCB1 exploration constant. 0.7 is a common tuning for light-playout Go and
// keeps a small budget concentrated on the promising points.
const float kExploration = 0.7f;

// Playouts stop after this many extra plies even if neither side passes. Only
// reachable in pathological positions - the eye guard normally forces a pass.
const uint16_t kPlayoutPlyCap = 2 * kPointCount;

}  // namespace

void setAiClock(MillisFn fn) { gClock = fn; }

AiConfig aiConfigForLevel(Level level) {
  AiConfig c;
  c.level = level;
  switch (level) {
    case kLevelEasy:
      c.heuristicOnly = true;
      c.maxPlayouts = 0;
      c.budgetMs = 0;
      c.atariPercent = 0;
      break;
    // On device the wall-clock budget is what binds; maxPlayouts is only a
    // ceiling so a trivially small endgame position stops early instead of
    // burning the whole budget. The host harness installs no clock, so there
    // these run to exactly maxPlayouts and stay reproducible. Retune budgetMs
    // from the on-device `bench` numbers, not from the host's.
    // budgetMs is what binds on device and maxPlayouts is only a ceiling, so
    // the caps are set high and the wall-clock is what actually decides depth.
    // These times were raised after the first on-panel game showed normal
    // playing near heuristic strength: the P4 is far slower than the host, so a
    // 1.2 s budget bought too few playouts to beat the priors. The opponent now
    // prints its achieved playout count on screen after each move - retune from
    // that number, not from the host's.
    case kLevelNormal:
      c.heuristicOnly = false;
      c.maxPlayouts = 8000;
      c.budgetMs = 2000;
      c.atariPercent = 60;
      break;
    case kLevelHard:
    default:
      c.heuristicOnly = false;
      c.maxPlayouts = 40000;
      c.budgetMs = 4000;
      c.atariPercent = 70;
      break;
  }
  return c;
}

const char *levelName(Level level) {
  switch (level) {
    case kLevelEasy:
      return "easy";
    case kLevelNormal:
      return "normal";
    case kLevelHard:
    default:
      return "hard";
  }
}

void GoAi::begin(const AiConfig &config, uint32_t seed) {
  config_ = config;
  rng_ = seed != 0 ? seed : 0x2545F491u;
  thinking_ = false;
  bestMove_ = kPass;
  playouts_ = 0;
  targetPlayouts_ = 0;
  confidence_ = 50;
  candidateCount_ = 0;
  emptyCount_ = 0;
  sliceStartMs_ = 0;
}

uint32_t GoAi::nextRandom() {
  // xorshift32: cheap, good enough for playouts, and identical on both builds.
  rng_ ^= rng_ << 13;
  rng_ ^= rng_ >> 17;
  rng_ ^= rng_ << 5;
  return rng_;
}

uint8_t GoAi::randomBelow(uint8_t bound) {
  return bound == 0 ? 0 : (uint8_t)(nextRandom() % bound);
}

void GoAi::buildCandidates() {
  candidateCount_ = 0;
  for (int16_t p = 0; p < kPointCount && candidateCount_ < kMaxCandidates; p++) {
    if (root_.at(p) != kEmpty) {
      continue;
    }
    // Playing inside one's own true eye is always self-destructive, so those
    // points never enter the search - this is what stops the bot from killing
    // its own groups in the endgame.
    if (root_.isTrueEye(p, rootPlayer_)) {
      continue;
    }
    if (!root_.isLegal(p, rootPlayer_)) {
      continue;
    }
    candidates_[candidateCount_++] = p;
  }

  // Passing is deliberately NOT a search candidate. With a small playout
  // budget its win rate is pure noise, and a bot that passes early leaves dead
  // stones on the board - which this scorer, having no dead-stone marking,
  // would then count as living. Playing every dame out costs nothing under
  // area scoring and keeps the final score honest. shouldAcceptEnd() handles
  // the one case where passing is right.
  for (uint8_t i = 0; i < candidateCount_; i++) {
    visits_[i] = 0;
    wins_[i] = 0;
  }
}

int32_t GoAi::heuristicScore(int16_t point, bool full) const {
  MoveInfo info;
  bool legal = full ? root_.evaluate(point, rootPlayer_, info)
                    : root_.evaluateFast(point, rootPlayer_, info);
  if (!legal) {
    return kIllegalScore;
  }

  int8_t x = (int8_t)pointX(point);
  int8_t y = (int8_t)pointY(point);
  int32_t score = 8 - (abs(x - 4) + abs(y - 4));
  score += (int32_t)info.captures * 60;
  score += (int32_t)info.liberties * 5;
  if (info.liberties <= 1) {
    score -= 25;
  }
  if (full) {
    score += (int32_t)info.opponentAtariGroups * 18;
    score -= (int32_t)info.ownAtariGroups * 12;
  }
  return score;
}

void GoAi::seedPriors() {
  // On a 400 MHz part the whole budget buys only a few playouts per candidate,
  // which on its own is close to random. Seeding each candidate with virtual
  // wins from the cheap one-ply score gives UCB1 an informed starting point,
  // so a short search still plays sensibly and a long one still converges to
  // what the playouts say.
  int32_t best = kIllegalScore;
  int32_t worst = 0x7FFFFFFF;
  for (uint8_t i = 0; i < candidateCount_; i++) {
    priorScores_[i] = heuristicScore(candidates_[i], false);
    if (priorScores_[i] == kIllegalScore) {
      continue;
    }
    if (priorScores_[i] > best) {
      best = priorScores_[i];
    }
    if (priorScores_[i] < worst) {
      worst = priorScores_[i];
    }
  }

  int32_t span = best - worst;
  if (span <= 0) {
    span = 1;
  }

  totalVisits_ = 0;
  for (uint8_t i = 0; i < candidateCount_; i++) {
    int32_t s = priorScores_[i] == kIllegalScore ? worst : priorScores_[i];
    // Map the score span onto 35%-65%: strong enough to order the candidates,
    // weak enough that real playouts overturn it quickly.
    float rate = 0.35f + 0.30f * ((float)(s - worst) / (float)span);
    visits_[i] = kPriorVisits;
    wins_[i] = (uint32_t)(rate * kPriorVisits + 0.5f);
    priorWins_[i] = wins_[i];
    totalVisits_ += kPriorVisits;
  }
}

void GoAi::start(const GoBoard &board) {
  root_ = board;
  rootPlayer_ = board.toMove();
  root_.save(rootSnapshot_);
  sim_ = board;
  // Playouts only need simple-ko correctness; a narrow window keeps the
  // per-move superko scan off the hot path.
  sim_.setSuperkoWindow(2);

  buildCandidates();
  playouts_ = 0;
  confidence_ = 50;
  bestMove_ = candidateCount_ > 0 ? candidates_[0] : kPass;
  thinking_ = true;
  sliceStartMs_ = gClock != nullptr ? gClock() : 0;
  targetPlayouts_ = 0;

  // Nothing left but our own eyes, or the opponent passed while we are ahead:
  // either way the right move is to pass and let the score stand.
  if (candidateCount_ == 0 || shouldAcceptEnd()) {
    bestMove_ = kPass;
    thinking_ = false;
    return;
  }

  if (config_.heuristicOnly || candidateCount_ == 1) {
    decideHeuristic();
    thinking_ = false;
    return;
  }
  pruneCandidates();
  seedPriors();
  targetPlayouts_ = config_.maxPlayouts;
}

void GoAi::pruneCandidates() {
  if (candidateCount_ <= kMaxSearchCandidates) {
    return;
  }
  int32_t scores[kMaxCandidates];
  for (uint8_t i = 0; i < candidateCount_; i++) {
    scores[i] = heuristicScore(candidates_[i], false);
  }
  // Partial selection sort: move the top-K candidates to the front, then drop
  // the rest. K is small so this is cheap and avoids a full sort.
  for (uint8_t k = 0; k < kMaxSearchCandidates; k++) {
    uint8_t best = k;
    for (uint8_t j = (uint8_t)(k + 1); j < candidateCount_; j++) {
      if (scores[j] > scores[best]) {
        best = j;
      }
    }
    if (best != k) {
      int32_t ts = scores[k];
      scores[k] = scores[best];
      scores[best] = ts;
      int16_t tc = candidates_[k];
      candidates_[k] = candidates_[best];
      candidates_[best] = tc;
    }
  }
  candidateCount_ = kMaxSearchCandidates;
}

bool GoAi::shouldAcceptEnd() const {
  // Only ever considered right after the opponent passed. Accepting while
  // behind would be throwing the game away, so this is gated on the current
  // area score - which is exactly the number the game will be settled on.
  if (root_.consecutivePasses() == 0) {
    return false;
  }
  bool blackAhead = root_.score().marginX2 > 0;
  return blackAhead == (rootPlayer_ == kBlack);
}

uint8_t GoAi::progressPercent() const {
  if (!thinking_ || targetPlayouts_ == 0) {
    return 100;
  }
  uint32_t pct = playouts_ * 100 / targetPlayouts_;
  // With a clock installed the time budget is what actually ends the search,
  // so the bar has to track whichever limit is closer or it would crawl.
  if (gClock != nullptr && config_.budgetMs > 0) {
    uint32_t elapsed = gClock() - sliceStartMs_;
    uint32_t timePct = elapsed * 100 / config_.budgetMs;
    if (timePct > pct) {
      pct = timePct;
    }
  }
  return pct > 100 ? 100 : (uint8_t)pct;
}

uint8_t GoAi::selectCandidate() const {
  // Every candidate carries prior visits from seedPriors(), so UCB1 applies
  // from the first playout - no forced sweep through 60 candidates first.
  float logTotal = logf((float)totalVisits_ + 1.0f);
  uint8_t best = 0;
  float bestScore = -1.0f;
  for (uint8_t i = 0; i < candidateCount_; i++) {
    float mean = (float)wins_[i] / (float)visits_[i];
    float score = mean + kExploration * sqrtf(logTotal / (float)visits_[i]);
    if (score > bestScore) {
      bestScore = score;
      best = i;
    }
  }
  return best;
}

void GoAi::rebuildEmpties() {
  emptyCount_ = 0;
  for (int16_t p = 0; p < kPointCount; p++) {
    if (sim_.at(p) == kEmpty) {
      empties_[emptyCount_++] = p;
    }
  }
}

int16_t GoAi::localAtariReply() {
  int16_t last = sim_.lastMovePoint();
  if (last == kPass) {
    return kPass;
  }

  // Capture the stone that just moved if it left itself on one liberty.
  // Legality is deliberately not pre-checked here: isLegal() costs as much as
  // playing the move, and runPlayout already recovers from a rejection.
  int16_t capture = sim_.soleLiberty(last);
  if (capture != kPass) {
    return capture;
  }

  // Otherwise try to run out of atari with a group next to that move.
  const int8_t *adj = GoBoard::neighbors(last);
  for (uint8_t i = 0; i < 4; i++) {
    int8_t n = adj[i];
    if (n < 0 || sim_.at(n) != sim_.toMove()) {
      continue;
    }
    int16_t escape = sim_.soleLiberty(n);
    if (escape != kPass && !sim_.isTrueEye(escape, sim_.toMove())) {
      return escape;
    }
  }
  return kPass;
}

int16_t GoAi::choosePlayoutMove() {
  if (config_.atariPercent > 0 && randomBelow(100) < config_.atariPercent) {
    int16_t reply = localAtariReply();
    if (reply != kPass) {
      return reply;
    }
  }

  if (emptyCount_ == 0) {
    return kPass;
  }

  // Scan the empty list from a random offset and take the first playable
  // point. Compacting stale entries as we go keeps the list short late in a
  // playout, which is where most of the scanning happens.
  uint8_t start = randomBelow(emptyCount_);
  for (uint8_t k = 0; k < emptyCount_;) {
    uint8_t idx = (uint8_t)((start + k) % emptyCount_);
    int16_t p = empties_[idx];
    if (sim_.at(p) != kEmpty) {
      empties_[idx] = empties_[emptyCount_ - 1];
      emptyCount_--;
      if (emptyCount_ == 0) {
        return kPass;
      }
      start = start % emptyCount_;
      continue;
    }
    if (!sim_.isTrueEye(p, sim_.toMove())) {
      return p;
    }
    k++;
  }
  return kPass;
}

bool GoAi::runPlayout(uint8_t candidate) {
  sim_.restore(rootSnapshot_);
  MoveInfo info;

  if (sim_.play(candidates_[candidate], info, false) != kMoveOk) {
    return false;
  }
  rebuildEmpties();

  for (uint16_t ply = 0; ply < kPlayoutPlyCap && !sim_.finished(); ply++) {
    int16_t move = choosePlayoutMove();
    if (move == kPass) {
      sim_.pass(info);
      continue;
    }
    if (sim_.play(move, info, false) != kMoveOk) {
      // Rejected for ko or suicide: drop it from the list and let the next
      // iteration pick again rather than burning the whole playout.
      for (uint8_t i = 0; i < emptyCount_; i++) {
        if (empties_[i] == move) {
          empties_[i] = empties_[emptyCount_ - 1];
          emptyCount_--;
          break;
        }
      }
      if (emptyCount_ == 0) {
        sim_.pass(info);
      }
      continue;
    }
    if (info.captures > 0) {
      rebuildEmpties();
    } else {
      for (uint8_t i = 0; i < emptyCount_; i++) {
        if (empties_[i] == move) {
          empties_[i] = empties_[emptyCount_ - 1];
          emptyCount_--;
          break;
        }
      }
    }
  }

  bool blackAhead = sim_.score().marginX2 > 0;
  return blackAhead == (rootPlayer_ == kBlack);
}

bool GoAi::step(uint32_t sliceMs) {
  if (!thinking_) {
    return true;
  }

  uint32_t sliceStart = gClock != nullptr ? gClock() : 0;
  uint32_t runThisSlice = 0;

  while (playouts_ < targetPlayouts_) {
    uint8_t candidate = selectCandidate();
    if (runPlayout(candidate)) {
      wins_[candidate]++;
    }
    visits_[candidate]++;
    totalVisits_++;
    playouts_++;
    runThisSlice++;

    if (gClock != nullptr) {
      uint32_t now = gClock();
      if (now - sliceStart >= sliceMs || now - sliceStartMs_ >= config_.budgetMs) {
        break;
      }
    } else if (runThisSlice >= kPlayoutsPerSlice) {
      break;
    }
  }

  bool budgetSpent = playouts_ >= targetPlayouts_;
  if (!budgetSpent && gClock != nullptr && gClock() - sliceStartMs_ >= config_.budgetMs) {
    budgetSpent = true;
  }
  if (!budgetSpent) {
    return false;
  }

  decide();
  thinking_ = false;
  return true;
}

void GoAi::decide() {
  // Most-visited root move: steadier than raw win rate when a few candidates
  // got only a handful of playouts.
  uint8_t best = 0;
  uint32_t bestVisits = 0;
  for (uint8_t i = 0; i < candidateCount_; i++) {
    if (visits_[i] > bestVisits) {
      bestVisits = visits_[i];
      best = i;
    }
  }
  bestMove_ = candidates_[best];

  // Report the win rate from real playouts only; the priors would otherwise
  // drag every number toward the 35-65% band they were seeded in. The seeded
  // wins have to be subtracted from the stored total, which is why seedPriors
  // keeps a copy of them.
  uint32_t realVisits = visits_[best] - kPriorVisits;
  uint32_t realWins = wins_[best] - priorWins_[best];
  confidence_ = realVisits > 0 ? (uint8_t)(realWins * 100 / realVisits) : 50;
}

void GoAi::decideHeuristic() {
  // EASY: one-ply scoring - captures, atari pressure, liberties, and a mild
  // pull to the centre - with a random tiebreak so no two games are identical.
  int32_t bestScore = kIllegalScore;
  int16_t bestPoint = kPass;

  for (uint8_t i = 0; i < candidateCount_; i++) {
    int32_t score = heuristicScore(candidates_[i], true);
    if (score == kIllegalScore) {
      continue;
    }
    score += (int32_t)(nextRandom() % 7);
    if (score > bestScore) {
      bestScore = score;
      bestPoint = candidates_[i];
    }
  }

  bestMove_ = bestPoint;
  confidence_ = 50;
}

}  // namespace litego
