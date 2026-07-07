#include "LiteGoTouchView.h"

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
#include <Arduino_GFX_Library.h>
#include <CrowPanelShared.h>
#endif

void LiteGoTouchView::begin(LiteGoGame *game) {
  game_ = game;
  dirty_ = true;
  wasTouched_ = false;
}

void LiteGoTouchView::requestRepaint() {
  dirty_ = true;
}

bool LiteGoTouchView::tick(int8_t &x, int8_t &y) {
  x = -1;
  y = -1;

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  if (game_ == nullptr || CrowDisplay::canvas() == nullptr) {
    return false;
  }

  if (dirty_) {
    draw();
    dirty_ = false;
  }

  int16_t tx;
  int16_t ty;
  bool touched = CrowDisplay::touchPoint(tx, ty);
  bool tapped = touched && !wasTouched_;
  wasTouched_ = touched;

  if (!tapped) {
    return false;
  }
  return mapTouch(tx, ty, x, y);
#else
  (void)x;
  (void)y;
  return false;
#endif
}

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
namespace {
constexpr int16_t kPanelX = 372;
constexpr int16_t kPanelY = 152;
constexpr int16_t kPanelW = 636;
constexpr int16_t kPanelH = 368;
constexpr int16_t kOriginX = 430;
constexpr int16_t kOriginY = 214;
constexpr int16_t kCell = 34;
constexpr int16_t kGridSpan = kCell * (LiteGoGame::kSize - 1);
constexpr int16_t kStoneR = 12;
constexpr int16_t kHitPad = kCell / 2;
constexpr int16_t kInfoX = 742;

constexpr uint16_t kBoard = Widgets::rgb(0xD6, 0xB4, 0x73);
constexpr uint16_t kBoardLine = Widgets::rgb(0x4E, 0x3C, 0x24);
constexpr uint16_t kBlackStone = Widgets::rgb(0x0A, 0x0D, 0x13);
constexpr uint16_t kWhiteStone = Widgets::rgb(0xF2, 0xEE, 0xDF);
constexpr uint16_t kWhiteLine = Widgets::rgb(0xBE, 0xB6, 0xA0);

int16_t pointCoord(int8_t index, int16_t origin) {
  return origin + (int16_t)index * kCell;
}

void writeLabel(Arduino_GFX *g, int16_t x, int16_t y, const String &text) {
  Widgets::text(g, x, y, text.c_str(), Widgets::fontS(), Widgets::kTextMut, Widgets::kLeft);
}
}

bool LiteGoTouchView::mapTouch(int16_t tx, int16_t ty, int8_t &x, int8_t &y) const {
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

  x = nearestX;
  y = nearestY;
  return true;
}

void LiteGoTouchView::draw() {
  Arduino_GFX *g = CrowDisplay::canvas();
  if (g == nullptr || game_ == nullptr) {
    return;
  }

  g->fillRect(kPanelX, kPanelY, kPanelW, kPanelH, Widgets::kBg);
  Widgets::panel(g, kPanelX, kPanelY, kPanelW, kPanelH, 8, Widgets::kSurface, 1, Widgets::kLine);
  Widgets::text(g, kPanelX + 24, kPanelY + 24, "9x9 Touch Board",
                Widgets::fontL(), Widgets::kTextHi, Widgets::kLeft);

  g->fillRoundRect(kOriginX - 26, kOriginY - 26, kGridSpan + 52, kGridSpan + 52, 8, kBoard);

  for (uint8_t i = 0; i < LiteGoGame::kSize; i++) {
    int16_t pos = pointCoord(i, 0);
    g->drawFastHLine(kOriginX, kOriginY + pos, kGridSpan, kBoardLine);
    g->drawFastVLine(kOriginX + pos, kOriginY, kGridSpan, kBoardLine);
  }

  for (uint8_t y = 0; y < LiteGoGame::kSize; y++) {
    for (uint8_t x = 0; x < LiteGoGame::kSize; x++) {
      char stone = game_->at(x, y);
      if (stone == '.') {
        continue;
      }
      int16_t cx = pointCoord(x, kOriginX);
      int16_t cy = pointCoord(y, kOriginY);
      if (stone == 'B') {
        g->fillCircle(cx, cy, kStoneR, kBlackStone);
        g->drawCircle(cx, cy, kStoneR, Widgets::kLine);
      } else {
        g->fillCircle(cx, cy, kStoneR, kWhiteStone);
        g->drawCircle(cx, cy, kStoneR, kWhiteLine);
      }
    }
  }

  for (uint8_t i = 0; i < LiteGoGame::kSize; i++) {
    String n = String(i);
    Widgets::text(g, pointCoord(i, kOriginX), kOriginY + kGridSpan + 22, n.c_str(),
                  Widgets::fontS(), kBoardLine, Widgets::kCenter);
    Widgets::text(g, kOriginX - 24, pointCoord(i, kOriginY) - 8, n.c_str(),
                  Widgets::fontS(), kBoardLine, Widgets::kCenter);
  }

  LiteGoGame::ScoreEstimate score = game_->estimateScore();
  char line[64];

  Widgets::text(g, kInfoX, kPanelY + 32, "TO MOVE", Widgets::fontS(), Widgets::kTextMut, Widgets::kLeft);
  snprintf(line, sizeof(line), "%c", game_->currentPlayer());
  Widgets::text(g, kInfoX, kPanelY + 58, line, Widgets::fontXL(), Widgets::kAccent, Widgets::kLeft);

  snprintf(line, sizeof(line), "Moves %u", game_->moveCount());
  writeLabel(g, kInfoX, kPanelY + 122, line);
  snprintf(line, sizeof(line), "Captures B%u W%u", game_->blackCaptures(), game_->whiteCaptures());
  writeLabel(g, kInfoX, kPanelY + 150, line);
  snprintf(line, sizeof(line), "Area B%d W%d", score.blackArea, score.whiteArea);
  writeLabel(g, kInfoX, kPanelY + 178, line);
  snprintf(line, sizeof(line), "Territory B%u W%u N%u",
           score.blackTerritory, score.whiteTerritory, score.neutralPoints);
  writeLabel(g, kInfoX, kPanelY + 206, line);

  String coach = game_->lastCoach();
  if (coach.length() > 40) {
    coach = coach.substring(0, 40) + "...";
  }
  Widgets::text(g, kInfoX, kPanelY + 250, "COACH", Widgets::fontS(), Widgets::kTextMut, Widgets::kLeft);
  Widgets::text(g, kInfoX, kPanelY + 278, coach.c_str(), Widgets::fontS(), Widgets::kTextHi, Widgets::kLeft);
  Widgets::text(g, kInfoX, kPanelY + 322, "Tap an empty point", Widgets::fontS(), Widgets::kTextMut, Widgets::kLeft);
}
#endif
