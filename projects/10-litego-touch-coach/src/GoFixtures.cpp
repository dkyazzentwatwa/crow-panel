#include "GoFixtures.h"

#include <stdio.h>

#include "GoBoard.h"

namespace litego {
namespace {

void check(TestReport &report, const char *name, bool ok) {
  char buffer[96];
  snprintf(buffer, sizeof(buffer), "[selftest] %-34s %s", name, ok ? "PASS" : "FAIL");
  report.line(buffer);
  if (ok) {
    report.passed++;
  } else {
    report.failed++;
  }
}

// --- capture, suicide, and the capturing-suicide exception -----------------

void fixtureCapture(TestReport &report) {
  GoBoard b;
  MoveInfo info;
  bool ok = b.play(pointAt(1, 0), info) == kMoveOk;   // B
  ok = ok && b.play(pointAt(0, 0), info) == kMoveOk;  // W into the corner
  ok = ok && b.play(pointAt(0, 1), info) == kMoveOk;  // B takes the last liberty
  check(report, "corner capture legal", ok);
  check(report, "corner capture removes stone",
        info.captures == 1 && b.blackCaptures() == 1 && b.at(0, 0) == kEmpty &&
            info.capturedPoints[0] == pointAt(0, 0));
}

void fixtureSuicide(TestReport &report) {
  // Black surrounds the corner, then White plays into it with no capture.
  const char *rows[kSize] = {
      ".B.......",
      "B........",
      ".........",
      ".........",
      ".........",
      ".........",
      ".........",
      ".........",
      ".........",
  };
  GoBoard b;
  b.setPosition(rows, kWhite);
  MoveInfo info;
  MoveStatus status = b.play(pointAt(0, 0), info);
  check(report, "suicide rejected", status == kMoveSuicide);
  check(report, "suicide leaves board untouched",
        b.at(0, 0) == kEmpty && b.toMove() == kWhite && b.moveCount() == 0);
}

void fixtureCapturingSuicideIsLegal(TestReport &report) {
  // Black's stone at (1,1) is surrounded on all four sides, so it has no
  // liberty of its own - but it first removes the white corner group, which
  // frees two. This is the rule case a naive "no zero-liberty move" check
  // gets wrong.
  const char *rows[kSize] = {
      "WWB......",
      "W.W......",
      "BW.......",
      ".........",
      ".........",
      ".........",
      ".........",
      ".........",
      ".........",
  };
  GoBoard b;
  b.setPosition(rows, kBlack);
  MoveInfo info;
  MoveStatus status = b.play(pointAt(1, 1), info);
  check(report, "capturing move with no own liberty legal",
        status == kMoveOk && info.captures == 3 && b.at(0, 0) == kEmpty &&
            b.at(1, 1) == kBlack);
}

// --- ko and positional superko ---------------------------------------------

// Three ko shapes stacked so a full six-ply triple-ko cycle returns to the
// start position. Only positional superko catches the closing move.
const char *const kTripleKo[kSize] = {
    ".BW......",
    "BW.W.....",
    ".BW......",
    ".WB......",
    "WB.B.....",
    ".WB......",
    ".BW......",
    "BW.W.....",
    ".BW......",
};

void fixtureSimpleKo(TestReport &report) {
  GoBoard b;
  b.setPosition(kTripleKo, kBlack);
  MoveInfo info;
  MoveStatus capture = b.play(pointAt(2, 1), info);
  check(report, "ko capture legal", capture == kMoveOk && info.captures == 1);
  MoveStatus recapture = b.play(pointAt(1, 1), info);
  check(report, "immediate ko recapture rejected", recapture == kMoveKo);
}

void fixtureKoLegalAfterTenuki(TestReport &report) {
  GoBoard b;
  b.setPosition(kTripleKo, kBlack);
  MoveInfo info;
  bool ok = b.play(pointAt(2, 1), info) == kMoveOk;    // B takes the ko
  ok = ok && b.play(pointAt(7, 0), info) == kMoveOk;   // W tenuki
  ok = ok && b.play(pointAt(7, 2), info) == kMoveOk;   // B tenuki
  MoveStatus recapture = b.play(pointAt(1, 1), info);  // W recaptures
  check(report, "ko recapture legal after tenuki",
        ok && recapture == kMoveOk && info.captures == 1);
}

void fixtureSuperko(TestReport &report) {
  GoBoard b;
  b.setPosition(kTripleKo, kBlack);
  MoveInfo info;
  bool ok = true;
  ok = ok && b.play(pointAt(2, 1), info) == kMoveOk && info.captures == 1;  // B takes ko A
  ok = ok && b.play(pointAt(2, 4), info) == kMoveOk && info.captures == 1;  // W takes ko B
  ok = ok && b.play(pointAt(2, 7), info) == kMoveOk && info.captures == 1;  // B takes ko C
  ok = ok && b.play(pointAt(1, 1), info) == kMoveOk && info.captures == 1;  // W retakes A
  ok = ok && b.play(pointAt(1, 4), info) == kMoveOk && info.captures == 1;  // B retakes B
  check(report, "triple ko cycle runs five plies", ok);
  // The sixth ply would recreate the seeded position: only superko sees it.
  MoveStatus closing = b.play(pointAt(1, 7), info);
  check(report, "superko rejects position repeat", closing == kMoveKo);
}

// --- eyes -------------------------------------------------------------------

void fixtureTrueEye(TestReport &report) {
  // (0,0) corner eye, (4,4) centre eye with 3 friendly diagonals, (0,7) edge
  // eye whose two off-board diagonals count as friendly.
  const char *rows[kSize] = {
      ".B.......",
      "BB.......",
      ".........",
      "...BBB...",
      "...B.B...",
      "...BB....",
      "BB.......",
      ".B.......",
      "BB.......",
  };
  GoBoard b;
  b.setPosition(rows, kBlack);
  check(report, "corner true eye", b.isTrueEye(pointAt(0, 0), kBlack));
  check(report, "centre true eye", b.isTrueEye(pointAt(4, 4), kBlack));
  check(report, "edge true eye", b.isTrueEye(pointAt(0, 7), kBlack));
  check(report, "isolated empty point is not an eye", !b.isTrueEye(pointAt(8, 4), kBlack));
  check(report, "opponent point is not our eye", !b.isTrueEye(pointAt(0, 0), kWhite));
  check(report, "occupied point is not an eye", !b.isTrueEye(pointAt(1, 0), kBlack));
}

void fixtureFalseEye(TestReport &report) {
  // (1,1) has all four orthogonals black but two black-free diagonals, so it
  // is a false eye and must not be treated as one.
  const char *rows[kSize] = {
      ".B.......",
      "B.B......",
      ".B.......",
      ".........",
      ".........",
      ".........",
      ".........",
      ".........",
      ".........",
  };
  GoBoard b;
  b.setPosition(rows, kBlack);
  check(report, "false eye rejected", !b.isTrueEye(pointAt(1, 1), kBlack));
}

// --- scoring and game end ---------------------------------------------------

void fixtureScoreAndKomi(TestReport &report) {
  // Black owns the top-left 3x3 block, White the bottom-right 3x3 block, and
  // the rest is neutral. Areas are equal, so komi alone decides.
  const char *rows[kSize] = {
      "BBB.....W",
      "B.B.....W",
      "BBB......",
      ".........",
      ".........",
      ".........",
      "......WWW",
      "......W.W",
      "......WWW",
  };
  GoBoard b;
  b.setPosition(rows, kBlack);
  b.setKomiX2(13);  // 6.5
  ScoreEstimate s = b.score();
  check(report, "black area counts stones plus eye", s.blackArea == 9);
  check(report, "white area counts stones plus eye", s.whiteArea == 11);
  check(report, "komi applied to margin", s.marginX2 == (9 - 11) * 2 - 13);
  b.setKomiX2(0);
  check(report, "zero komi margin", b.score().marginX2 == (9 - 11) * 2);
}

void fixtureGameEnd(TestReport &report) {
  GoBoard b;
  MoveInfo info;
  bool ok = b.play(pointAt(4, 4), info) == kMoveOk;
  ok = ok && b.pass(info) == kMovePass && b.state() == kPlaying;
  ok = ok && b.pass(info) == kMovePass;
  check(report, "two passes finish the game", ok && b.state() == kFinishedByPasses);
  check(report, "moves rejected after game over",
        b.play(pointAt(0, 0), info) == kMoveGameOver && b.at(0, 0) == kEmpty);

  GoBoard r;
  r.resign(kBlack);
  check(report, "resignation finishes the game",
        r.state() == kFinishedByResign && r.resignedBy() == kBlack);
}

void fixtureUndo(TestReport &report) {
  GoBoard b;
  MoveInfo info;
  b.play(pointAt(1, 0), info);
  b.play(pointAt(0, 0), info);

  Snapshot before;
  b.save(before);
  uint64_t hashBefore = b.hash();

  b.play(pointAt(0, 1), info);  // capture
  bool changed = b.blackCaptures() == 1 && b.hash() != hashBefore;

  b.restore(before);
  check(report, "undo restores position and counters",
        changed && b.hash() == hashBefore && b.blackCaptures() == 0 &&
            b.at(0, 0) == kWhite && b.toMove() == kBlack && b.moveCount() == 2);

  // The ko history must rewind too, or the replayed move would look like a
  // superko violation.
  check(report, "undo rewinds ko history", b.play(pointAt(0, 1), info) == kMoveOk);
}

void fixtureEmptyBoardLegality(TestReport &report) {
  GoBoard b;
  uint8_t legal = 0;
  for (int16_t p = 0; p < kPointCount; p++) {
    if (b.isLegal(p, kBlack)) {
      legal++;
    }
  }
  check(report, "every point legal on an empty board", legal == kPointCount);
  check(report, "off-board point rejected", !b.isLegal(kPointCount, kBlack));
}

}  // namespace

bool runRulesFixtures(TestReport &report) {
  report.passed = 0;
  report.failed = 0;

  fixtureCapture(report);
  fixtureSuicide(report);
  fixtureCapturingSuicideIsLegal(report);
  fixtureSimpleKo(report);
  fixtureKoLegalAfterTenuki(report);
  fixtureSuperko(report);
  fixtureTrueEye(report);
  fixtureFalseEye(report);
  fixtureScoreAndKomi(report);
  fixtureGameEnd(report);
  fixtureUndo(report);
  fixtureEmptyBoardLegality(report);

  char buffer[96];
  snprintf(buffer, sizeof(buffer), "[selftest] rules %u passed, %u failed", report.passed,
           report.failed);
  report.line(buffer);
  return report.failed == 0;
}

}  // namespace litego
