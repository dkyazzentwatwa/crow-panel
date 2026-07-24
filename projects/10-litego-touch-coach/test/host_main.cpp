// Host-side test harness for the LiteGo rules core and Monte-Carlo AI.
//
// Lives outside src/ on purpose: arduino-cli only compiles the sketch root and
// src/, so nothing in this folder ever reaches the firmware. Build and run it
// with scripts/test-litego.sh.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <chrono>

#include "../src/GoAi.h"
#include "../src/GoBoard.h"
#include "../src/GoFixtures.h"

using namespace litego;

namespace {

void emitStdout(void *, const char *line) { printf("%s\n", line); }

uint16_t gFailures = 0;

void expect(const char *name, bool ok) {
  printf("[host] %-44s %s\n", name, ok ? "PASS" : "FAIL");
  if (!ok) {
    gFailures++;
  }
}

double nowMs() {
  using namespace std::chrono;
  return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}

struct GameOutcome {
  bool illegalMove;
  bool eyeFill;
  bool exceededMoveCap;
  int16_t marginX2;  // >0 Black ahead
  uint16_t plies;
};

// Plays one complete game between two AI configurations and reports whether
// either side ever produced an illegal move or filled one of its own eyes.
GameOutcome playGame(const AiConfig &blackCfg, const AiConfig &whiteCfg, uint32_t seed) {
  GameOutcome out = {false, false, false, 0, 0};
  GoBoard board;
  GoAi black;
  GoAi white;
  black.begin(blackCfg, seed);
  white.begin(whiteCfg, seed ^ 0x5DEECE66u);

  const uint16_t kMoveCap = 4 * kPointCount;
  MoveInfo info;

  while (!board.finished() && out.plies < kMoveCap) {
    GoAi &ai = board.toMove() == kBlack ? black : white;
    ai.start(board);
    while (!ai.step(1000)) {
    }
    int16_t move = ai.bestMove();

    if (move == kPass) {
      board.pass(info);
    } else {
      if (board.isTrueEye(move, board.toMove())) {
        out.eyeFill = true;
      }
      if (board.play(move, info, false) != kMoveOk) {
        out.illegalMove = true;
        break;
      }
    }
    out.plies++;
  }

  if (out.plies >= kMoveCap) {
    out.exceededMoveCap = true;
  }
  out.marginX2 = board.score().marginX2;
  return out;
}

// The host build installs no clock, so budgetMs is inert and every search runs
// exactly maxPlayouts - which is what makes these runs reproducible.
AiConfig withPlayouts(Level level, uint32_t playouts) {
  AiConfig cfg = aiConfigForLevel(level);
  cfg.maxPlayouts = playouts;
  return cfg;
}

void benchPlayouts() {
  const uint32_t kPlayouts = 4000;
  GoBoard board;
  GoAi ai;
  ai.begin(withPlayouts(kLevelHard, kPlayouts), 12345);
  ai.start(board);
  double start = nowMs();
  while (!ai.step(1000)) {
  }
  double elapsed = nowMs() - start;
  uint32_t playouts = ai.playouts();
  printf("[host] bench (empty board): %u playouts in %.1f ms = %.0f playouts/sec\n",
         (unsigned)playouts, elapsed, playouts / (elapsed / 1000.0));

  // Mid-game positions are the ones that actually gate the on-device budget,
  // so measure one of those too.
  GoBoard mid;
  MoveInfo info;
  GoAi filler;
  filler.begin(withPlayouts(kLevelEasy, 0), 999);
  for (uint8_t i = 0; i < 30 && !mid.finished(); i++) {
    filler.start(mid);
    while (!filler.step(1000)) {
    }
    int16_t m = filler.bestMove();
    if (m == kPass) {
      mid.pass(info);
    } else {
      mid.play(m, info, false);
    }
  }
  ai.begin(withPlayouts(kLevelHard, kPlayouts), 4242);
  ai.start(mid);
  start = nowMs();
  while (!ai.step(1000)) {
  }
  elapsed = nowMs() - start;
  printf("[host] bench (30 plies in): %u playouts in %.1f ms = %.0f playouts/sec\n",
         (unsigned)ai.playouts(), elapsed, ai.playouts() / (elapsed / 1000.0));
}

void testAiHygiene() {
  // Every level must produce only legal, non-eye-filling moves and terminate.
  const Level levels[] = {kLevelEasy, kLevelNormal, kLevelHard};
  const char *names[] = {"easy", "normal", "hard"};
  for (uint8_t i = 0; i < 3; i++) {
    AiConfig cfg = withPlayouts(levels[i], 300);
    GameOutcome g = playGame(cfg, cfg, 0xC0FFEE00u + i);
    char name[80];
    snprintf(name, sizeof(name), "%s self-play: no illegal move", names[i]);
    expect(name, !g.illegalMove);
    snprintf(name, sizeof(name), "%s self-play: never fills own eye", names[i]);
    expect(name, !g.eyeFill);
    snprintf(name, sizeof(name), "%s self-play: game terminates", names[i]);
    expect(name, !g.exceededMoveCap);
    printf("[host]   (%s finished in %u plies, margin %+.1f)\n", names[i], g.plies,
           g.marginX2 / 2.0);
  }
}

void testStrengthOrdering(uint8_t games) {
  // Hard should beat Easy clearly. Colours alternate so komi and the first-move
  // advantage cannot decide the result on their own.
  AiConfig hard = withPlayouts(kLevelHard, 1200);
  AiConfig easy = withPlayouts(kLevelEasy, 0);

  uint8_t hardWins = 0;
  for (uint8_t i = 0; i < games; i++) {
    bool hardIsBlack = (i % 2) == 0;
    GameOutcome g = hardIsBlack ? playGame(hard, easy, 0x1234u + i * 7919u)
                                : playGame(easy, hard, 0x1234u + i * 7919u);
    bool blackWon = g.marginX2 > 0;
    if (blackWon == hardIsBlack) {
      hardWins++;
    }
    printf("[host]   game %u: hard played %s, margin %+.1f -> %s\n", i + 1,
           hardIsBlack ? "black" : "white", g.marginX2 / 2.0,
           (blackWon == hardIsBlack) ? "hard wins" : "easy wins");
  }

  char name[80];
  snprintf(name, sizeof(name), "hard beats easy in at least %u of %u games", (games * 7) / 10,
           games);
  expect(name, hardWins >= (games * 7) / 10);
}

}  // namespace

int main(int argc, char **argv) {
  setvbuf(stdout, nullptr, _IOLBF, 0);  // line-buffered so a hang still shows progress
  bool quick = argc > 1 && strcmp(argv[1], "--quick") == 0;

  TestReport report;
  report.emit = emitStdout;
  report.context = nullptr;
  report.passed = 0;
  report.failed = 0;
  runRulesFixtures(report);
  gFailures += report.failed;

  printf("\n");
  testAiHygiene();
  printf("\n");
  benchPlayouts();
  printf("\n");
  if (!quick) {
    testStrengthOrdering(10);
  } else {
    printf("[host] --quick: skipping the strength tournament\n");
  }

  printf("\n[host] %s (%u failures)\n", gFailures == 0 ? "ALL PASS" : "FAILURES", gFailures);
  return gFailures == 0 ? 0 : 1;
}
