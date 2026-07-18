#include "LiteGoTouchView.h"

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
#include <Arduino_GFX_Library.h>
#include <CrowPanelShared.h>
#endif

void LiteGoTouchView::begin(LiteGoGame *game) {
  game_ = game;
  dirty_ = true;
  wasTouched_ = false;
  lastMoveValid_ = false;
  lastMoveX_ = -1;
  lastMoveY_ = -1;
  status_ = "Board ready. Tap an intersection or command pill.";
  statusWarning_ = false;

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  lastTouchMap_ = "none";
  lastTouchActionMs_ = 0;
  if (CrowDisplay::canvas() == nullptr) {
    CrowDisplay::begin(activeHardwareProfile(), "LiteGo Touch Coach");
  }
#endif
}

void LiteGoTouchView::requestRepaint() {
  dirty_ = true;
}

void LiteGoTouchView::clearLastMove() {
  lastMoveValid_ = false;
  lastMoveX_ = -1;
  lastMoveY_ = -1;
  dirty_ = true;
}

void LiteGoTouchView::setLastResult(const LiteGoGame::MoveResult &result, const char *source) {
  if (result.status == LiteGoGame::kMoveOk) {
    lastMoveValid_ = true;
    lastMoveX_ = result.x;
    lastMoveY_ = result.y;
  }
  if (result.status == LiteGoGame::kMovePass || result.status == LiteGoGame::kMoveNoLegalMove) {
    lastMoveValid_ = false;
  }

  String label = source ? source : "move";
  status_ = label + ": ";
  if (game_ != nullptr) {
    status_ += game_->describeMove(result);
  } else {
    status_ += result.status == LiteGoGame::kMoveOk ? "played" : "updated";
  }
  statusWarning_ = result.status != LiteGoGame::kMoveOk &&
                   result.status != LiteGoGame::kMovePass &&
                   result.status != LiteGoGame::kMoveNoLegalMove;
  dirty_ = true;
}

void LiteGoTouchView::setStatus(const String &message, bool warning) {
  status_ = message;
  statusWarning_ = warning;
  dirty_ = true;
}

bool LiteGoTouchView::tick(Action &action) {
  action.type = kActionNone;
  action.x = -1;
  action.y = -1;

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  if (game_ == nullptr || CrowDisplay::canvas() == nullptr) {
    return false;
  }

  if (dirty_) {
    draw();
    dirty_ = false;
  }

  int16_t rx;
  int16_t ry;
  bool touched = CrowDisplay::touchPoint(rx, ry);
  unsigned long now = millis();
  bool tapped = touched && !wasTouched_ && (now - lastTouchActionMs_) > 120;

  if (tapped) {
    struct TouchCandidate {
      int16_t x;
      int16_t y;
      const char *name;
    };
    const TouchCandidate candidates[] = {
        {rx, ry, "raw"},
        {ry, rx, "swap"},
        {(int16_t)(1024 - 1 - rx), ry, "flipX"},
        {rx, (int16_t)(600 - 1 - ry), "flipY"},
        {(int16_t)(1024 - 1 - rx), (int16_t)(600 - 1 - ry), "flipXY"},
        {(int16_t)(1024 - 1 - ry), rx, "swapFlipX"},
        {ry, (int16_t)(600 - 1 - rx), "swapFlipY"},
        {(int16_t)(1024 - 1 - ry), (int16_t)(600 - 1 - rx), "swapFlipXY"},
    };

    for (uint8_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
      int16_t tx = candidates[i].x;
      int16_t ty = candidates[i].y;
      if (tx < 0 || tx >= 1024 || ty < 0 || ty >= 600) {
        continue;
      }

      bool duplicate = false;
      for (uint8_t j = 0; j < i; j++) {
        if (candidates[j].x == tx && candidates[j].y == ty) {
          duplicate = true;
          break;
        }
      }
      if (duplicate) {
        continue;
      }

      if (mapTouch(tx, ty, action)) {
        lastTouchMap_ = candidates[i].name;
        lastTouchActionMs_ = now;
        Logger::info("touch", String("map=") + lastTouchMap_ +
                                  " raw=" + String(rx) + "," + String(ry) +
                                  " mapped=" + String(tx) + "," + String(ty) +
                                  " action=" + String((int)action.type) +
                                  " point=" + String(action.x) + "," + String(action.y));
        wasTouched_ = touched;
        return true;
      }
    }

    lastTouchActionMs_ = now;
    Logger::info("touch", "ignored raw=" + String(rx) + "," + String(ry));
  }

  wasTouched_ = touched;
#else
  (void)action;
#endif
  return false;
}

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
namespace {
constexpr int16_t kScreenW = 1024;
constexpr int16_t kScreenH = 600;
constexpr int16_t kHeaderH = 56;
constexpr int16_t kFooterY = 548;
constexpr int16_t kFooterH = 52;
constexpr int16_t kBoardPanelX = 24;
constexpr int16_t kBoardPanelY = 80;
constexpr int16_t kBoardPanelW = 500;
constexpr int16_t kBoardPanelH = 452;
constexpr int16_t kSideX = 548;
constexpr int16_t kSideY = 80;
constexpr int16_t kSideW = 452;
constexpr int16_t kSideH = 452;
constexpr int16_t kOriginX = 100;
constexpr int16_t kOriginY = 150;
constexpr int16_t kCell = 42;
constexpr int16_t kGridSpan = kCell * (LiteGoGame::kSize - 1);
constexpr int16_t kStoneR = 15;
constexpr int16_t kHitPad = kCell / 2;

constexpr uint16_t kBoard = Widgets::rgb(0xD8, 0xB8, 0x73);
constexpr uint16_t kBoardHi = Widgets::rgb(0xF1, 0xCE, 0x83);
constexpr uint16_t kBoardLine = Widgets::rgb(0x4D, 0x3A, 0x22);
constexpr uint16_t kBlackStone = Widgets::rgb(0x07, 0x0A, 0x11);
constexpr uint16_t kWhiteStone = Widgets::rgb(0xF4, 0xF0, 0xE4);
constexpr uint16_t kWhiteLine = Widgets::rgb(0xB6, 0xAD, 0x98);

struct ButtonDef {
  LiteGoTouchView::ActionType type;
  const char *label;
  int16_t x;
  int16_t y;
  int16_t w;
  int16_t h;
  uint16_t fill;
  uint16_t text;
};

const ButtonDef kButtons[] = {
    {LiteGoTouchView::kActionPass, "PASS", 564, 476, 76, 36, Widgets::kSurfaceHi, Widgets::kTextHi},
    {LiteGoTouchView::kActionCpu, "CPU", 648, 476, 70, 36, Widgets::kAccent, Widgets::kBg},
    {LiteGoTouchView::kActionHint, "HINT", 726, 476, 78, 36, Widgets::kSurfaceHi, Widgets::kTextHi},
    {LiteGoTouchView::kActionScore, "SCORE", 812, 476, 86, 36, Widgets::kSurfaceHi, Widgets::kTextHi},
    {LiteGoTouchView::kActionReset, "RESET", 906, 476, 78, 36, Widgets::kRed, Widgets::kBg},
};

int16_t pointCoord(int8_t index, int16_t origin) {
  return origin + (int16_t)index * kCell;
}

String fitText(Arduino_GFX *g, const String &text, const GFXfont *font, int16_t maxW) {
  if (Widgets::textWidth(g, text.c_str(), font) <= maxW) {
    return text;
  }
  String t = text;
  while (t.length() > 1 && Widgets::textWidth(g, (t + "...").c_str(), font) > maxW) {
    t.remove(t.length() - 1);
  }
  return t + "...";
}

void drawWrapped(Arduino_GFX *g, int16_t x, int16_t y, int16_t maxW, uint8_t maxLines,
                 const String &source, const GFXfont *font, uint16_t color) {
  String text = source;
  text.trim();
  for (uint8_t lineNo = 0; lineNo < maxLines && text.length() > 0; lineNo++) {
    int best = 0;
    int scan = 1;
    while (scan <= (int)text.length()) {
      bool boundary = scan == (int)text.length() || text.charAt(scan) == ' ';
      if (boundary) {
        String part = text.substring(0, scan);
        part.trim();
        if (Widgets::textWidth(g, part.c_str(), font) <= maxW) {
          best = scan;
          scan++;
          continue;
        }
        break;
      }
      scan++;
    }

    if (best <= 0) {
      best = 1;
      while (best < (int)text.length() &&
             Widgets::textWidth(g, text.substring(0, best + 1).c_str(), font) <= maxW) {
        best++;
      }
    }

    String line = text.substring(0, best);
    text = text.substring(best);
    text.trim();
    if (lineNo == maxLines - 1 && text.length() > 0) {
      line = fitText(g, line + " " + text, font, maxW);
      text = "";
    }
    Widgets::text(g, x, y + lineNo * 24, line.c_str(), font, color, Widgets::kLeft);
  }
}

void metricCard(Arduino_GFX *g, int16_t x, int16_t y, int16_t w, int16_t h,
                const char *label, const String &value, const String &sub,
                uint16_t accent) {
  Widgets::panel(g, x, y, w, h, 8, Widgets::kSurface, 1, Widgets::kLine);
  Widgets::text(g, x + 12, y + 10, label, Widgets::fontS(), Widgets::kTextMut, Widgets::kLeft);
  Widgets::text(g, x + 12, y + 34, fitText(g, value, Widgets::fontL(), w - 24).c_str(),
                Widgets::fontL(), accent, Widgets::kLeft);
  if (sub.length() > 0) {
    Widgets::text(g, x + 12, y + h - 24, fitText(g, sub, Widgets::fontS(), w - 24).c_str(),
                  Widgets::fontS(), Widgets::kTextMut, Widgets::kLeft);
  }
}

void drawButton(Arduino_GFX *g, const ButtonDef &button) {
  g->fillRoundRect(button.x, button.y, button.w, button.h, button.h / 2, button.fill);
  Widgets::text(g, button.x + button.w / 2, button.y + 9, button.label,
                Widgets::fontS(), button.text, Widgets::kCenter);
}

void drawStone(Arduino_GFX *g, int16_t cx, int16_t cy, char stone, bool last) {
  if (stone == 'B') {
    g->fillCircle(cx + 2, cy + 2, kStoneR, Widgets::rgb(0x2B, 0x22, 0x17));
    g->fillCircle(cx, cy, kStoneR, kBlackStone);
    g->drawCircle(cx, cy, kStoneR, Widgets::kLine);
  } else if (stone == 'W') {
    g->fillCircle(cx + 2, cy + 2, kStoneR, Widgets::rgb(0x83, 0x6A, 0x42));
    g->fillCircle(cx, cy, kStoneR, kWhiteStone);
    g->drawCircle(cx, cy, kStoneR, kWhiteLine);
  }
  if (last) {
    g->drawCircle(cx, cy, kStoneR + 5, Widgets::kAccent);
    g->drawCircle(cx, cy, kStoneR + 6, Widgets::kAccent);
  }
}
}

bool LiteGoTouchView::hitButton(int16_t tx, int16_t ty, Action &action) const {
  for (uint8_t i = 0; i < sizeof(kButtons) / sizeof(kButtons[0]); i++) {
    const ButtonDef &button = kButtons[i];
    if (tx >= button.x - 8 && tx <= button.x + button.w + 8 &&
        ty >= button.y - 8 && ty <= button.y + button.h + 8) {
      action.type = button.type;
      action.x = -1;
      action.y = -1;
      return true;
    }
  }
  return false;
}

bool LiteGoTouchView::mapTouch(int16_t tx, int16_t ty, Action &action) const {
  if (hitButton(tx, ty, action)) {
    return true;
  }

  if (tx < kOriginX - kHitPad || tx > kOriginX + kGridSpan + kHitPad ||
      ty < kOriginY - kHitPad || ty > kOriginY + kGridSpan + kHitPad) {
    return false;
  }

  int16_t localX = tx - kOriginX;
  int16_t localY = ty - kOriginY;
  int8_t nearestX = (localX >= 0) ? (localX + kHitPad) / kCell : (localX - kHitPad) / kCell;
  int8_t nearestY = (localY >= 0) ? (localY + kHitPad) / kCell : (localY - kHitPad) / kCell;

  if (nearestX < 0 || nearestX >= (int8_t)LiteGoGame::kSize ||
      nearestY < 0 || nearestY >= (int8_t)LiteGoGame::kSize) {
    return false;
  }

  int16_t centerX = pointCoord(nearestX, kOriginX);
  int16_t centerY = pointCoord(nearestY, kOriginY);
  if (abs(tx - centerX) > kHitPad || abs(ty - centerY) > kHitPad) {
    return false;
  }

  action.type = kActionPlay;
  action.x = nearestX;
  action.y = nearestY;
  return true;
}

void LiteGoTouchView::draw() {
  Arduino_GFX *g = CrowDisplay::canvas();
  if (g == nullptr || game_ == nullptr) {
    return;
  }

  LiteGoGame::ScoreEstimate score = game_->estimateScore();
  g->fillScreen(Widgets::kBg);

  g->fillRect(0, 0, kScreenW, kHeaderH, Widgets::kSurface);
  g->fillRect(0, kHeaderH - 2, kScreenW, 2, Widgets::kAccent);
  Widgets::text(g, 20, 10, "LITEGO TOUCH COACH", Widgets::fontL(),
                Widgets::kTextHi, Widgets::kLeft);
  Widgets::text(g, 20, 34, "OFFLINE 9x9 GO REVIEW", Widgets::fontS(),
                Widgets::kTextMut, Widgets::kLeft);

  char moves[20];
  snprintf(moves, sizeof(moves), "MOVES %u", game_->moveCount());
  int16_t x = kScreenW - 16;
  x -= Widgets::pill(g, x - Widgets::textWidth(g, moves, Widgets::fontS()) - 24, 14,
                     moves, Widgets::fontS(), Widgets::kTextHi, Widgets::kSurfaceHi);
  x -= 10;
  x -= Widgets::pill(g, x - Widgets::textWidth(g, "9x9", Widgets::fontS()) - 24, 14,
                     "9x9", Widgets::fontS(), Widgets::kTextHi, Widgets::kSurfaceHi);
  x -= 10;
  Widgets::pill(g, x - Widgets::textWidth(g, "LOCAL", Widgets::fontS()) - 24, 14,
                "LOCAL", Widgets::fontS(), Widgets::kBg, Widgets::kAccent);

  Widgets::panel(g, kBoardPanelX, kBoardPanelY, kBoardPanelW, kBoardPanelH,
                 8, Widgets::kSurface, 1, Widgets::kLine);
  Widgets::text(g, kBoardPanelX + 20, kBoardPanelY + 16, "BOARD",
                Widgets::fontS(), Widgets::kTextMut, Widgets::kLeft);
  Widgets::text(g, kBoardPanelX + kBoardPanelW - 20, kBoardPanelY + 16,
                game_->gameEndedByPasses() ? "TWO PASSES" : "TAP TO PLAY",
                Widgets::fontS(), game_->gameEndedByPasses() ? Widgets::kAmber : Widgets::kAccent,
                Widgets::kRight);

  int16_t boardX = kOriginX - 34;
  int16_t boardY = kOriginY - 34;
  int16_t boardSize = kGridSpan + 68;
  g->fillRoundRect(boardX, boardY, boardSize, boardSize, 8, kBoard);
  g->drawRoundRect(boardX, boardY, boardSize, boardSize, 8, kBoardHi);

  for (uint8_t i = 0; i < LiteGoGame::kSize; i++) {
    int16_t pos = pointCoord(i, 0);
    g->drawFastHLine(kOriginX, kOriginY + pos, kGridSpan, kBoardLine);
    g->drawFastVLine(kOriginX + pos, kOriginY, kGridSpan, kBoardLine);
  }

  const int8_t starPoints[5][2] = {{2, 2}, {6, 2}, {4, 4}, {2, 6}, {6, 6}};
  for (uint8_t i = 0; i < 5; i++) {
    g->fillCircle(pointCoord(starPoints[i][0], kOriginX),
                  pointCoord(starPoints[i][1], kOriginY), 4, kBoardLine);
  }

  for (uint8_t y = 0; y < LiteGoGame::kSize; y++) {
    for (uint8_t x = 0; x < LiteGoGame::kSize; x++) {
      char stone = game_->at(x, y);
      bool last = lastMoveValid_ && lastMoveX_ == (int8_t)x && lastMoveY_ == (int8_t)y;
      if (stone != '.') {
        drawStone(g, pointCoord(x, kOriginX), pointCoord(y, kOriginY), stone, last);
      } else if (last) {
        g->drawCircle(pointCoord(x, kOriginX), pointCoord(y, kOriginY), 10, Widgets::kAccent);
      }
    }
  }

  for (uint8_t i = 0; i < LiteGoGame::kSize; i++) {
    char n[3];
    snprintf(n, sizeof(n), "%u", i);
    Widgets::text(g, pointCoord(i, kOriginX), kOriginY + kGridSpan + 28, n,
                  Widgets::fontS(), kBoardLine, Widgets::kCenter);
    Widgets::text(g, kOriginX - 30, pointCoord(i, kOriginY) - 8, n,
                  Widgets::fontS(), kBoardLine, Widgets::kCenter);
  }

  Widgets::panel(g, kSideX, kSideY, kSideW, kSideH, 8, Widgets::kSurface, 1, Widgets::kLine);
  Widgets::text(g, kSideX + 18, kSideY + 16, "GAME STATE",
                Widgets::fontS(), Widgets::kTextMut, Widgets::kLeft);
  Widgets::statusDot(g, kSideX + kSideW - 26, kSideY + 26, 6,
                     statusWarning_ ? Widgets::kRed : Widgets::kGreen);

  char toMove[2] = {game_->currentPlayer(), '\0'};
  metricCard(g, 564, 116, 132, 92, "TO MOVE", toMove,
             game_->currentPlayer() == 'B' ? "black stones" : "white stones", Widgets::kAccent);
  metricCard(g, 708, 116, 132, 92, "CAPTURES",
             String("B") + String(game_->blackCaptures()) + " W" + String(game_->whiteCaptures()),
             "prisoners", Widgets::kAmber);
  metricCard(g, 852, 116, 132, 92, "AREA",
             String(score.margin >= 0 ? "B+" : "W+") + String(abs(score.margin)),
             String("B") + String(score.blackArea) + " W" + String(score.whiteArea),
             score.margin == 0 ? Widgets::kTextHi : Widgets::kGreen);

  Widgets::panel(g, 564, 224, 420, 78, 8,
                 statusWarning_ ? Widgets::rgb(0x31, 0x18, 0x24) : Widgets::kSurfaceHi,
                 1, statusWarning_ ? Widgets::kRed : Widgets::kLine);
  Widgets::text(g, 582, 238, statusWarning_ ? "ATTENTION" : "LAST ACTION",
                Widgets::fontS(), statusWarning_ ? Widgets::kRed : Widgets::kTextMut, Widgets::kLeft);
  drawWrapped(g, 582, 262, 384, 2, status_, Widgets::fontS(), Widgets::kTextHi);

  Widgets::panel(g, 564, 318, 420, 128, 8, Widgets::kSurface, 1, Widgets::kLine);
  Widgets::text(g, 582, 334, "COACH", Widgets::fontS(), Widgets::kTextMut, Widgets::kLeft);
  drawWrapped(g, 582, 362, 384, 4, game_->lastCoach(), Widgets::fontS(), Widgets::kTextHi);

  for (uint8_t i = 0; i < sizeof(kButtons) / sizeof(kButtons[0]); i++) {
    drawButton(g, kButtons[i]);
  }

  g->fillRect(0, kFooterY, kScreenW, kFooterH, Widgets::kSurface);
  g->fillRect(0, kFooterY, kScreenW, 2, Widgets::kLine);
  Widgets::statusDot(g, 22, kFooterY + 27, 5, Widgets::kAmber);
  Widgets::text(g, 40, kFooterY + 18, "compile-ready until flashed and touch-proven",
                Widgets::fontS(), Widgets::kTextMut, Widgets::kLeft);
  Widgets::text(g, kScreenW / 2, kFooterY + 18,
                String("last touch map: " + String(lastTouchMap_)).c_str(),
                Widgets::fontS(), Widgets::kTextMut, Widgets::kCenter);
  Widgets::text(g, kScreenW - 16, kFooterY + 18, "Serial: selftest",
                Widgets::fontS(), Widgets::kTextMut, Widgets::kRight);
}
#endif
