#include "LiteGoTouchView.h"

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
#include <Arduino_GFX_Library.h>
#include <CrowPanelShared.h>
#endif

void LiteGoTouchView::setStatus(const String &message, bool warning) {
  status_ = message;
  statusWarning_ = warning;
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  dirtyStatus_ = true;
#endif
}

#if !(USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4))

// Headless build: the game is fully playable over Serial, so the view degrades
// to a no-op rather than disappearing behind #ifdefs at every call site.
void LiteGoTouchView::begin(LiteGoGame *game) { game_ = game; }
bool LiteGoTouchView::ready() const { return false; }
void LiteGoTouchView::requestFullRepaint() {}
void LiteGoTouchView::requestBoardRepaint() {}
void LiteGoTouchView::requestStatusRepaint() {}
void LiteGoTouchView::requestScoreRepaint() {}
void LiteGoTouchView::noteMoveResult(const LiteGoGame::MoveResult &, const char *) {}
void LiteGoTouchView::clearGhost() {}
void LiteGoTouchView::setThinking(bool) {}
void LiteGoTouchView::setHint(int8_t, int8_t) {}
bool LiteGoTouchView::tick(Action &action) {
  action.type = kActionNone;
  action.x = -1;
  action.y = -1;
  return false;
}
void LiteGoTouchView::reportCalibration(Print &out) const {
  out.println(F("[touchcal] display build required (USE_DISPLAY=1)"));
}
String LiteGoTouchView::calibrationSummary() const {
  return "Touch calibration needs a USE_DISPLAY=1 build.";
}

#else

namespace {

constexpr int16_t kScreenW = 1024;
constexpr int16_t kScreenH = 600;
constexpr int16_t kHeaderH = 56;
constexpr int16_t kFooterY = 548;
constexpr int16_t kFooterH = kScreenH - kFooterY;

// Board geometry. 46 px cells with 19 px stones stay finger-sized, and the
// 42 px gutter has to hold both a stone (19 px) and a coordinate label under
// it - at 34 px the labels overlapped the edge stones and clipped on the tile
// border. Tile footprint is unchanged (452 px), so the side panel is untouched.
constexpr int16_t kCell = 46;
constexpr int16_t kGridSpan = kCell * (LiteGoGame::kSize - 1);  // 368
constexpr int16_t kTilePad = 42;
constexpr int16_t kTileX = 22;
constexpr int16_t kTileY = 68;
constexpr int16_t kTileSize = kGridSpan + 2 * kTilePad;  // 452
constexpr int16_t kOriginX = kTileX + kTilePad;          // 64
constexpr int16_t kOriginY = kTileY + kTilePad;          // 110
constexpr int16_t kStoneR = 19;
constexpr int16_t kCellHalf = kCell / 2;                 // 23
constexpr int16_t kHitPad = kCellHalf;
// Coordinate labels sit below/left of the outer line, past the edge stones.
constexpr int16_t kLabelGap = kStoneR + 4;

// Side column
constexpr int16_t kSideX = 496;
constexpr int16_t kSideW = 512;
constexpr int16_t kCardY = 68;
constexpr int16_t kCardH = 96;
constexpr int16_t kCardW = 164;
constexpr int16_t kStatusY = 176;
constexpr int16_t kStatusH = 88;
constexpr int16_t kCoachY = 274;
constexpr int16_t kCoachH = 118;
constexpr int16_t kThinkY = 402;
constexpr int16_t kThinkH = 28;
constexpr int16_t kRow1Y = 438;
constexpr int16_t kRow2Y = 490;
constexpr int16_t kButtonH = 46;

constexpr uint16_t kBoardWood = Widgets::rgb(0xD8, 0xB8, 0x73);
constexpr uint16_t kBoardEdge = Widgets::rgb(0xF1, 0xCE, 0x83);
constexpr uint16_t kBoardLine = Widgets::rgb(0x4D, 0x3A, 0x22);
constexpr uint16_t kBlackStone = Widgets::rgb(0x07, 0x0A, 0x11);
constexpr uint16_t kWhiteStone = Widgets::rgb(0xF4, 0xF0, 0xE4);
constexpr uint16_t kWhiteEdge = Widgets::rgb(0xB6, 0xAD, 0x98);
constexpr uint16_t kStoneShadow = Widgets::rgb(0xA8, 0x8C, 0x52);
constexpr uint16_t kGhostBlack = Widgets::rgb(0x86, 0x74, 0x50);
constexpr uint16_t kGhostWhite = Widgets::rgb(0xE7, 0xDA, 0xB8);
constexpr uint16_t kHintColor = Widgets::rgb(0x2F, 0xD0, 0x6A);  // hint target ring

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
    {LiteGoTouchView::kActionPass, "PASS", 496, kRow1Y, 96, kButtonH, Widgets::kSurfaceHi,
     Widgets::kTextHi},
    {LiteGoTouchView::kActionUndo, "UNDO", 600, kRow1Y, 96, kButtonH, Widgets::kSurfaceHi,
     Widgets::kTextHi},
    {LiteGoTouchView::kActionHint, "HINT", 704, kRow1Y, 96, kButtonH, Widgets::kSurfaceHi,
     Widgets::kTextHi},
    {LiteGoTouchView::kActionScore, "SCORE", 808, kRow1Y, 96, kButtonH, Widgets::kSurfaceHi,
     Widgets::kTextHi},
    {LiteGoTouchView::kActionResign, "RESIGN", 912, kRow1Y, 96, kButtonH, Widgets::kSurfaceHi,
     Widgets::kRed},
    {LiteGoTouchView::kActionNewGame, "NEW GAME", 496, kRow2Y, 164, kButtonH, Widgets::kAccent,
     Widgets::kBg},
    {LiteGoTouchView::kActionLevel, "LEVEL", 670, kRow2Y, 164, kButtonH, Widgets::kSurfaceHi,
     Widgets::kTextHi},
    {LiteGoTouchView::kActionColor, "SWAP SIDES", 844, kRow2Y, 164, kButtonH, Widgets::kSurfaceHi,
     Widgets::kTextHi},
};
constexpr uint8_t kButtonCount = sizeof(kButtons) / sizeof(kButtons[0]);

const int8_t kStarPoints[5][2] = {{2, 2}, {6, 2}, {4, 4}, {2, 6}, {6, 6}};

int16_t pointCoordX(int8_t index) { return kOriginX + (int16_t)index * kCell; }
int16_t pointCoordY(int8_t index) { return kOriginY + (int16_t)index * kCell; }

int16_t clampi(int32_t v, int32_t lo, int32_t hi) {
  if (v < lo) return (int16_t)lo;
  if (v > hi) return (int16_t)hi;
  return (int16_t)v;
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

// Greedy word wrap. Falls back to a character break for a single long word,
// and elides the tail into the final line rather than dropping it silently.
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
    Widgets::text(g, x, y + lineNo * 22, line.c_str(), font, color, Widgets::kLeft);
  }
}

void metricCard(Arduino_GFX *g, int16_t x, int16_t y, int16_t w, int16_t h, const char *label,
                const String &value, const String &sub, uint16_t accent) {
  Widgets::panel(g, x, y, w, h, 8, Widgets::kSurface, 1, Widgets::kLine);
  Widgets::text(g, x + 12, y + 10, label, Widgets::fontS(), Widgets::kTextMut, Widgets::kLeft);
  Widgets::text(g, x + 12, y + 34, fitText(g, value, Widgets::fontL(), w - 24).c_str(),
                Widgets::fontL(), accent, Widgets::kLeft);
  if (sub.length() > 0) {
    Widgets::text(g, x + 12, y + h - 24, fitText(g, sub, Widgets::fontS(), w - 24).c_str(),
                  Widgets::fontS(), Widgets::kTextMut, Widgets::kLeft);
  }
}

// Accumulated dirty row range; render() turns it into a single cache sync.
int16_t gFlushTop = 0x7FFF;
int16_t gFlushBottom = -1;

void markRows(int16_t y, int16_t h) {
  if (y < gFlushTop) {
    gFlushTop = y;
  }
  if (y + h > gFlushBottom) {
    gFlushBottom = y + h;
  }
}

}  // namespace

// --- touch ------------------------------------------------------------------

int16_t LiteGoTouchView::mapX(int16_t rawX, int16_t rawY) const {
  int32_t raw = LITEGO_TOUCH_SWAP_XY ? rawY : rawX;
  int32_t span = (int32_t)LITEGO_TOUCH_MAX_X - LITEGO_TOUCH_MIN_X;
  if (span == 0) {
    span = 1;
  }
  int32_t v = (raw - LITEGO_TOUCH_MIN_X) * (kScreenW - 1) / span;
#if LITEGO_TOUCH_INVERT_X
  v = (kScreenW - 1) - v;
#endif
  return clampi(v, 0, kScreenW - 1);
}

int16_t LiteGoTouchView::mapY(int16_t rawX, int16_t rawY) const {
  int32_t raw = LITEGO_TOUCH_SWAP_XY ? rawX : rawY;
  int32_t span = (int32_t)LITEGO_TOUCH_MAX_Y - LITEGO_TOUCH_MIN_Y;
  if (span == 0) {
    span = 1;
  }
  int32_t v = (raw - LITEGO_TOUCH_MIN_Y) * (kScreenH - 1) / span;
#if LITEGO_TOUCH_INVERT_Y
  v = (kScreenH - 1) - v;
#endif
  return clampi(v, 0, kScreenH - 1);
}

void LiteGoTouchView::sampleTouch() {
  int16_t rx = 0;
  int16_t ry = 0;
  bool down = CrowDisplay::touchPoint(rx, ry);
  if (down) {
    rawX_ = rx;
    rawY_ = ry;
    touchX_ = mapX(rx, ry);
    touchY_ = mapY(rx, ry);
  }
  wasDown_ = down_;
  down_ = down;
}

void LiteGoTouchView::reportCalibration(Print &out) const {
  out.println(String("[touchcal] swap=") + String(LITEGO_TOUCH_SWAP_XY) + " invertX=" +
              String(LITEGO_TOUCH_INVERT_X) + " invertY=" + String(LITEGO_TOUCH_INVERT_Y) +
              " rangeX=" + String(LITEGO_TOUCH_MIN_X) + ".." + String(LITEGO_TOUCH_MAX_X) +
              " rangeY=" + String(LITEGO_TOUCH_MIN_Y) + ".." + String(LITEGO_TOUCH_MAX_Y));
  if (down_) {
    out.println(String("[touchcal] raw=") + String(rawX_) + "," + String(rawY_) + " mapped=" +
                String(touchX_) + "," + String(touchY_));
  } else {
    out.println(F("[touchcal] no finger down; hold the screen and run touchcal again"));
  }
}

String LiteGoTouchView::calibrationSummary() const {
  if (!down_) {
    return String("Touchcal: hold a finger on the screen, then run touchcal again. Mapping "
                  "swap=") +
           String(LITEGO_TOUCH_SWAP_XY) + " invX=" + String(LITEGO_TOUCH_INVERT_X) +
           " invY=" + String(LITEGO_TOUCH_INVERT_Y) + ".";
  }
  int8_t px = 0;
  int8_t py = 0;
  String text = String("Touchcal raw ") + String(rawX_) + "," + String(rawY_) + " -> mapped " +
                String(touchX_) + "," + String(touchY_);
  if (pointAt(touchX_, touchY_, px, py)) {
    text += " = point " + String(px) + "," + String(py);
  } else {
    text += " (off the board)";
  }
  return text;
}

int8_t LiteGoTouchView::buttonAt(int16_t x, int16_t y) const {
  for (uint8_t i = 0; i < kButtonCount; i++) {
    const ButtonDef &b = kButtons[i];
    if (x >= b.x && x <= b.x + b.w && y >= b.y && y <= b.y + b.h) {
      return (int8_t)i;
    }
  }
  return -1;
}

bool LiteGoTouchView::pointAt(int16_t x, int16_t y, int8_t &px, int8_t &py) const {
  if (x < kOriginX - kHitPad || x > kOriginX + kGridSpan + kHitPad || y < kOriginY - kHitPad ||
      y > kOriginY + kGridSpan + kHitPad) {
    return false;
  }
  int16_t nx = (x - kOriginX + kCellHalf) / kCell;
  int16_t ny = (y - kOriginY + kCellHalf) / kCell;
  if (nx < 0 || nx >= (int16_t)LiteGoGame::kSize || ny < 0 || ny >= (int16_t)LiteGoGame::kSize) {
    return false;
  }
  px = (int8_t)nx;
  py = (int8_t)ny;
  return true;
}

bool LiteGoTouchView::tick(Action &action) {
  action.type = kActionNone;
  action.x = -1;
  action.y = -1;

  if (game_ == nullptr || !ready_) {
    return false;
  }

  render();
  sampleTouch();

  uint32_t now = millis();
  bool committed = false;

  if (down_ && !wasDown_) {
    // Press: remember where the finger landed and light up any button under it.
    pressX_ = touchX_;
    pressY_ = touchY_;
    int8_t button = buttonAt(touchX_, touchY_);
    if (button != pressedButton_) {
      pressedButton_ = button;
      dirtyButtons_ = true;
    }
  } else if (!down_ && wasDown_) {
    // Release: the action only fires if the finger is still on the target it
    // pressed, so sliding off cancels - the standard touch contract.
    int8_t releasedButton = buttonAt(pressX_, pressY_);
    int8_t pressedIndex = pressedButton_;
    if (pressedButton_ >= 0) {
      pressedButton_ = -1;
      dirtyButtons_ = true;
    }

    // Contact bounce lasts a few milliseconds. Keep this well under the gap
    // between two deliberate taps, or it swallows the confirm tap that places
    // the stone.
    if (now - lastActionMs_ < 60) {
      return false;
    }

    if (releasedButton >= 0 && releasedButton == pressedIndex) {
      action.type = kButtons[releasedButton].type;
      lastActionMs_ = now;
      committed = true;
      if (action.type != kActionHint && action.type != kActionScore) {
        clearGhost();
      }
    } else {
      int8_t px = 0;
      int8_t py = 0;
      if (pointAt(pressX_, pressY_, px, py)) {
        // Board taps belong to the human's own turn only. Without this a tap
        // during the opponent's search would place a stone of the opponent's
        // colour, because the engine always plays for the side to move.
        if (game_->finished()) {
          setStatus(String("Game over: ") + game_->resultText() + ". Tap NEW GAME to play again.");
          lastActionMs_ = now;
          return false;
        }
        if (!game_->humanToMove()) {
          setStatus("Opponent is thinking - wait for their move.");
          lastActionMs_ = now;
          return false;
        }
        if (game_->at((uint8_t)px, (uint8_t)py) != '.') {
          // No point parking a preview on a stone - say so instead of letting
          // the confirm tap bounce off the rules engine.
          clearGhost();
          char taken[64];
          snprintf(taken, sizeof(taken), "Point %d,%d already has a stone.", px, py);
          setStatus(taken, true);
          lastActionMs_ = now;
          return false;
        }
        if (px == ghostX_ && py == ghostY_) {
          // Second tap on the same intersection commits the stone.
          action.type = kActionPlay;
          action.x = px;
          action.y = py;
          lastActionMs_ = now;
          committed = true;
          clearGhost();
        } else {
          int8_t oldX = ghostX_;
          int8_t oldY = ghostY_;
          ghostX_ = px;
          ghostY_ = py;
          if (oldX >= 0) {
            markPointDirty((uint8_t)oldX, (uint8_t)oldY);
          }
          markPointDirty((uint8_t)px, (uint8_t)py);
          dirtyStatus_ = true;
          char buf[72];
          snprintf(buf, sizeof(buf), "Preview at %d,%d. Tap it again to place the stone.", px, py);
          status_ = buf;
          statusWarning_ = false;
          lastActionMs_ = now;
        }
      }
    }
  }

  return committed;
}

// --- lifecycle and dirty tracking -------------------------------------------

void LiteGoTouchView::begin(LiteGoGame *game) {
  game_ = game;
  status_ = "Tap an intersection to preview, tap it again to place.";
  statusWarning_ = false;
  ghostX_ = -1;
  ghostY_ = -1;
  hintX_ = -1;
  hintY_ = -1;
  koPoint_ = -1;
  territoryValid_ = false;
  pressedButton_ = -1;
  dirtyPointCount_ = 0;

  // manualFlush=true: Arduino_GFX otherwise cache-syncs on every primitive,
  // which for a text-heavy screen means thousands of syncs per repaint.
  ready_ = CrowDisplay::begin(activeHardwareProfile(), "LiteGo Touch Coach", true);
  if (!ready_) {
    Logger::warn("view", "display init failed; Serial play still works");
    return;
  }
  requestFullRepaint();
}

bool LiteGoTouchView::ready() const { return ready_; }

void LiteGoTouchView::requestFullRepaint() {
  dirtyChrome_ = true;
  dirtyBoard_ = true;
  dirtyStatus_ = true;
  dirtyScore_ = true;
  dirtyButtons_ = true;
  thinkingShown_ = 255;
  // Derived overlays get recomputed from the (possibly new) position: a full
  // repaint follows reset, resign, and side-swap.
  hintX_ = -1;
  hintY_ = -1;
  territoryValid_ = false;
  koPoint_ = game_ != nullptr ? game_->board().koPoint() : -1;
}

void LiteGoTouchView::requestBoardRepaint() { dirtyBoard_ = true; }
void LiteGoTouchView::requestStatusRepaint() { dirtyStatus_ = true; }
void LiteGoTouchView::requestScoreRepaint() { dirtyScore_ = true; }

void LiteGoTouchView::markPointDirty(uint8_t x, uint8_t y) {
  if (dirtyBoard_ || dirtyPointCount_ >= kMaxDirtyPoints) {
    return;
  }
  uint8_t index = (uint8_t)(y * LiteGoGame::kSize + x);
  for (uint8_t i = 0; i < dirtyPointCount_; i++) {
    if (dirtyPoints_[i] == index) {
      return;
    }
  }
  dirtyPoints_[dirtyPointCount_++] = index;
}

void LiteGoTouchView::clearGhost() {
  if (ghostX_ >= 0) {
    markPointDirty((uint8_t)ghostX_, (uint8_t)ghostY_);
  }
  ghostX_ = -1;
  ghostY_ = -1;
}

void LiteGoTouchView::setThinking(bool thinking) { thinking_ = thinking; }

void LiteGoTouchView::setHint(int8_t x, int8_t y) {
  if (hintX_ >= 0) {
    markPointDirty((uint8_t)hintX_, (uint8_t)hintY_);
  }
  hintX_ = x;
  hintY_ = y;
  if (hintX_ >= 0) {
    markPointDirty((uint8_t)hintX_, (uint8_t)hintY_);
  }
}

void LiteGoTouchView::noteMoveResult(const LiteGoGame::MoveResult &result, const char *source) {
  if (result.status == LiteGoGame::kMoveOk) {
    // Only the placed stone, the stones it removed, and the cell that used to
    // carry the last-move marker changed - so only those cells are redrawn.
    // The old marker cell has to be dirtied explicitly: the game has already
    // moved its own last-move pointer to the new stone by now.
    if (markerX_ >= 0) {
      markPointDirty((uint8_t)markerX_, (uint8_t)markerY_);
    }
    markPointDirty((uint8_t)result.x, (uint8_t)result.y);
    for (uint8_t i = 0; i < result.captures; i++) {
      uint8_t p = result.capturedPoints[i];
      markPointDirty((uint8_t)(p % LiteGoGame::kSize), (uint8_t)(p / LiteGoGame::kSize));
    }
    markerX_ = result.x;
    markerY_ = result.y;
  } else if (result.status == LiteGoGame::kMovePass) {
    if (markerX_ >= 0) {
      markPointDirty((uint8_t)markerX_, (uint8_t)markerY_);
    }
    markerX_ = -1;
    markerY_ = -1;
  }

  if (result.status == LiteGoGame::kMoveOk || result.status == LiteGoGame::kMovePass) {
    // A stale hint marker would point at the previous position; drop it, and
    // recompute the ko point once here rather than scanning every frame.
    if (hintX_ >= 0) {
      markPointDirty((uint8_t)hintX_, (uint8_t)hintY_);
      hintX_ = -1;
      hintY_ = -1;
    }
    int16_t previousKo = koPoint_;
    koPoint_ = game_ != nullptr ? game_->board().koPoint() : -1;
    if (previousKo != koPoint_) {
      if (previousKo >= 0) {
        markPointDirty((uint8_t)(previousKo % LiteGoGame::kSize),
                       (uint8_t)(previousKo / LiteGoGame::kSize));
      }
      if (koPoint_ >= 0) {
        markPointDirty((uint8_t)(koPoint_ % LiteGoGame::kSize),
                       (uint8_t)(koPoint_ / LiteGoGame::kSize));
      }
    }
    // Territory shading is only valid for the final position.
    territoryValid_ = false;
  }

  String label = source != nullptr ? source : "move";
  status_ = label + ": ";
  status_ += game_ != nullptr ? game_->describeMove(result) : String("updated");
  statusWarning_ = result.status != LiteGoGame::kMoveOk && result.status != LiteGoGame::kMovePass;
  dirtyStatus_ = true;
  dirtyScore_ = true;
  dirtyButtons_ = true;
}

// --- drawing ----------------------------------------------------------------

void LiteGoTouchView::drawChrome() {
  Arduino_GFX *g = CrowDisplay::canvas();
  g->fillScreen(Widgets::kBg);

  g->fillRect(0, 0, kScreenW, kHeaderH, Widgets::kSurface);
  g->fillRect(0, kHeaderH - 2, kScreenW, 2, Widgets::kAccent);
  Widgets::text(g, 20, 8, "LITEGO", Widgets::fontL(), Widgets::kTextHi, Widgets::kLeft);
  Widgets::text(g, 20, 32, "9x9 GO - OFFLINE", Widgets::fontS(), Widgets::kTextMut, Widgets::kLeft);

  // Wood tile and full grid.
  g->fillRoundRect(kTileX, kTileY, kTileSize, kTileSize, 10, kBoardWood);
  g->drawRoundRect(kTileX, kTileY, kTileSize, kTileSize, 10, kBoardEdge);
  for (uint8_t i = 0; i < LiteGoGame::kSize; i++) {
    g->drawFastHLine(kOriginX, pointCoordY((int8_t)i), kGridSpan + 1, kBoardLine);
    g->drawFastVLine(pointCoordX((int8_t)i), kOriginY, kGridSpan + 1, kBoardLine);
  }
  for (uint8_t i = 0; i < 5; i++) {
    g->fillCircle(pointCoordX(kStarPoints[i][0]), pointCoordY(kStarPoints[i][1]), 4, kBoardLine);
  }
  for (uint8_t i = 0; i < LiteGoGame::kSize; i++) {
    char n[3];
    snprintf(n, sizeof(n), "%u", i);
    // Column labels sit just below the last row of cell fills (drawCell paints
    // a kCellHalf-radius square of wood per point). Row labels must clear the
    // column-0 cell fills to their LEFT, or every drawCell in column 0 repaints
    // over them - which is why they were showing only a sliver. kCellHalf + 9
    // puts the whole glyph left of the first column's fill.
    Widgets::text(g, pointCoordX((int8_t)i), kOriginY + kGridSpan + kLabelGap, n, Widgets::fontS(),
                  kBoardLine, Widgets::kCenter);
    Widgets::text(g, kOriginX - kCellHalf - 9, pointCoordY((int8_t)i) - 7, n, Widgets::fontS(),
                  kBoardLine, Widgets::kCenter);
  }

  g->fillRect(0, kFooterY, kScreenW, kFooterH, Widgets::kSurface);
  g->fillRect(0, kFooterY, kScreenW, 2, Widgets::kLine);

  markRows(0, kScreenH);
}

void LiteGoTouchView::drawCell(uint8_t x, uint8_t y) {
  Arduino_GFX *g = CrowDisplay::canvas();
  int16_t cx = pointCoordX((int8_t)x);
  int16_t cy = pointCoordY((int8_t)y);

  // Repaint the cell's own square of wood, then put back everything that
  // belongs inside it. Stones are smaller than the cell, so neighbours are
  // never clipped and no full-board repaint is needed.
  int16_t left = cx - kCellHalf;
  int16_t top = cy - kCellHalf;
  g->fillRect(left, top, kCell, kCell, kBoardWood);

  int16_t hFrom = clampi(left, kOriginX, kOriginX + kGridSpan);
  int16_t hTo = clampi(left + kCell, kOriginX, kOriginX + kGridSpan);
  int16_t vFrom = clampi(top, kOriginY, kOriginY + kGridSpan);
  int16_t vTo = clampi(top + kCell, kOriginY, kOriginY + kGridSpan);
  g->drawFastHLine(hFrom, cy, hTo - hFrom + 1, kBoardLine);
  g->drawFastVLine(cx, vFrom, vTo - vFrom + 1, kBoardLine);

  for (uint8_t i = 0; i < 5; i++) {
    int16_t sx = pointCoordX(kStarPoints[i][0]);
    int16_t sy = pointCoordY(kStarPoints[i][1]);
    if (sx >= left && sx < left + kCell && sy >= top && sy < top + kCell) {
      g->fillCircle(sx, sy, 4, kBoardLine);
    }
  }

  char stone = game_->at(x, y);
  if (stone == 'B' || stone == 'W') {
    g->fillCircle(cx + 2, cy + 2, kStoneR, kStoneShadow);
    g->fillCircle(cx, cy, kStoneR, stone == 'B' ? kBlackStone : kWhiteStone);
    g->drawCircle(cx, cy, kStoneR, stone == 'B' ? Widgets::kLine : kWhiteEdge);
    bool last = game_->hasLastMove() && game_->lastMoveX() == (int8_t)x &&
                game_->lastMoveY() == (int8_t)y;
    if (last) {
      // Marker sits inside the stone so it cannot bleed into the next cell.
      g->fillCircle(cx, cy, 5, Widgets::kAccent);
    }
    markRows(top, kCell);
    return;
  }

  // Empty point. At game end it may be shaded territory; during play it may
  // carry the ghost preview, a hint suggestion, or a ko-forbidden marker.
  int16_t point = (int16_t)(y * LiteGoGame::kSize + x);

  if (game_->finished() && territoryValid_) {
    uint8_t owner = territory_[point];
    if (owner == litego::kBlack) {
      g->fillRect(cx - 7, cy - 7, 14, 14, kBlackStone);
      g->drawRect(cx - 7, cy - 7, 14, 14, kBoardLine);
    } else if (owner == litego::kWhite) {
      g->fillRect(cx - 7, cy - 7, 14, 14, kWhiteStone);
      g->drawRect(cx - 7, cy - 7, 14, 14, kWhiteEdge);
    }
    markRows(top, kCell);
    return;
  }

  if (ghostX_ == (int8_t)x && ghostY_ == (int8_t)y) {
    bool black = game_->currentPlayer() == 'B';
    g->fillCircle(cx, cy, kStoneR - 3, black ? kGhostBlack : kGhostWhite);
    g->drawCircle(cx, cy, kStoneR, Widgets::kAccent);
    g->drawCircle(cx, cy, kStoneR - 1, Widgets::kAccent);
  } else if (hintX_ == (int8_t)x && hintY_ == (int8_t)y) {
    // Suggestion marker: a green target ring, deliberately unlike the blue
    // ghost so a hint never looks like a committed preview.
    g->drawCircle(cx, cy, kStoneR - 2, kHintColor);
    g->drawCircle(cx, cy, kStoneR - 3, kHintColor);
    g->fillCircle(cx, cy, 3, kHintColor);
  }

  if (koPoint_ == point && !game_->finished()) {
    // Standard ko mark: a small hollow square on the forbidden point.
    g->drawRect(cx - 6, cy - 6, 12, 12, Widgets::kAmber);
    g->drawRect(cx - 5, cy - 5, 10, 10, Widgets::kAmber);
  }

  markRows(top, kCell);
}

void LiteGoTouchView::drawBoardAll() {
  for (uint8_t y = 0; y < LiteGoGame::kSize; y++) {
    for (uint8_t x = 0; x < LiteGoGame::kSize; x++) {
      drawCell(x, y);
    }
  }
  dirtyPointCount_ = 0;
  // Resync the tracked marker cell: a full repaint follows an undo or a new
  // game, either of which can move the last-move marker without a move result.
  markerX_ = game_->hasLastMove() ? game_->lastMoveX() : -1;
  markerY_ = game_->hasLastMove() ? game_->lastMoveY() : -1;
}

void LiteGoTouchView::drawScorePanel() {
  Arduino_GFX *g = CrowDisplay::canvas();
  litego::ScoreEstimate s = game_->estimateScore();

  char toMove[2] = {game_->currentPlayer(), '\0'};
  bool yourTurn = game_->humanToMove();
  metricCard(g, kSideX, kCardY, kCardW, kCardH, "TO MOVE", toMove,
             yourTurn ? "your move" : "opponent", yourTurn ? Widgets::kGreen : Widgets::kAmber);
  metricCard(g, kSideX + kCardW + 10, kCardY, kCardW, kCardH, "CAPTURES",
             String("B") + String(game_->blackCaptures()) + " W" + String(game_->whiteCaptures()),
             "prisoners", Widgets::kAmber);
  metricCard(g, kSideX + 2 * (kCardW + 10), kCardY, kCardW, kCardH, "SCORE", game_->resultText(),
             String("B") + String(s.blackArea) + " W" + String(s.whiteArea) + " komi " +
                 game_->komiText(),
             s.marginX2 == 0 ? Widgets::kTextHi : Widgets::kAccent);

  // Footer line: the settings that decide how the game plays.
  g->fillRect(0, kFooterY + 2, kScreenW, kFooterH - 2, Widgets::kSurface);
  Widgets::statusDot(g, 22, kFooterY + 26, 5, game_->finished() ? Widgets::kAmber : Widgets::kGreen);
  String left = String("LEVEL ") + game_->levelName() + "   KOMI " + game_->komiText() +
                "   YOU PLAY " + String(game_->humanColor() == 'B' ? "BLACK" : "WHITE") +
                "   MOVE " + String(game_->moveCount());
  Widgets::text(g, 40, kFooterY + 17, left.c_str(), Widgets::fontS(), Widgets::kTextMut,
                Widgets::kLeft);
  // Named local: a temporary String's c_str() inside a ternary survives only to
  // the end of the full expression, which is a trap waiting for the next edit.
  String right = game_->finished() ? String("GAME OVER  ") + game_->resultText()
                                   : String("Serial: help");
  Widgets::text(g, kScreenW - 16, kFooterY + 17, right.c_str(), Widgets::fontS(),
                game_->finished() ? Widgets::kAmber : Widgets::kTextMut, Widgets::kRight);

  markRows(kCardY, kCardH);
  markRows(kFooterY, kFooterH);
}

void LiteGoTouchView::drawStatusPanel() {
  Arduino_GFX *g = CrowDisplay::canvas();
  uint16_t border = statusWarning_ ? Widgets::kRed : Widgets::kLine;
  uint16_t fill = statusWarning_ ? Widgets::rgb(0x31, 0x18, 0x24) : Widgets::kSurfaceHi;

  Widgets::panel(g, kSideX, kStatusY, kSideW, kStatusH, 8, fill, 1, border);
  Widgets::text(g, kSideX + 18, kStatusY + 12, statusWarning_ ? "ATTENTION" : "LAST ACTION",
                Widgets::fontS(), statusWarning_ ? Widgets::kRed : Widgets::kTextMut,
                Widgets::kLeft);
  drawWrapped(g, kSideX + 18, kStatusY + 36, kSideW - 36, 2, status_, Widgets::fontS(),
              Widgets::kTextHi);

  Widgets::panel(g, kSideX, kCoachY, kSideW, kCoachH, 8, Widgets::kSurface, 1, Widgets::kLine);
  Widgets::text(g, kSideX + 18, kCoachY + 12, "COACH", Widgets::fontS(), Widgets::kTextMut,
                Widgets::kLeft);
  drawWrapped(g, kSideX + 18, kCoachY + 36, kSideW - 36, 4, game_->lastCoach(), Widgets::fontS(),
              Widgets::kTextHi);

  markRows(kStatusY, kStatusH);
  markRows(kCoachY, kCoachH);
}

void LiteGoTouchView::drawButtons() {
  Arduino_GFX *g = CrowDisplay::canvas();
  for (uint8_t i = 0; i < kButtonCount; i++) {
    const ButtonDef &b = kButtons[i];
    bool pressed = pressedButton_ == (int8_t)i;
    bool disabled = (b.type == kActionUndo && !game_->canUndo()) ||
                    (b.type == kActionResign && game_->finished()) ||
                    (b.type == kActionPass && game_->finished());

    uint16_t fill = pressed ? Widgets::kAccent : b.fill;
    uint16_t text = pressed ? Widgets::kBg : b.text;
    if (disabled) {
      fill = Widgets::kSurface;
      text = Widgets::kLine;
    }

    g->fillRoundRect(b.x, b.y, b.w, b.h, 10, fill);
    g->drawRoundRect(b.x, b.y, b.w, b.h, 10, Widgets::kLine);

    // LEVEL and SWAP SIDES show their current value, so the label is dynamic.
    String label = b.label;
    if (b.type == kActionLevel) {
      label = String("LEVEL: ") + game_->levelName();
    } else if (b.type == kActionColor) {
      label = String("YOU: ") + (game_->humanColor() == 'B' ? "BLACK" : "WHITE");
    }
    Widgets::text(g, b.x + b.w / 2, b.y + b.h / 2 - 7,
                  fitText(g, label, Widgets::fontS(), b.w - 12).c_str(), Widgets::fontS(), text,
                  Widgets::kCenter);
  }
  markRows(kRow1Y, kRow2Y + kButtonH - kRow1Y);
}

void LiteGoTouchView::drawEvalStrip() {
  // One strip, two jobs: the opponent's search progress while it thinks, and a
  // live "who's ahead" lead bar the rest of the time.
  Arduino_GFX *g = CrowDisplay::canvas();
  g->fillRect(kSideX, kThinkY, kSideW, kThinkH, Widgets::kBg);

  if (thinking_) {
    uint8_t pct = game_->aiProgressPercent();
    Widgets::text(g, kSideX, kThinkY + 6, "THINKING", Widgets::fontS(), Widgets::kAccent,
                  Widgets::kLeft);
    Widgets::hBar(g, kSideX + 92, kThinkY + 8, kSideW - 92 - 60, 12, pct / 100.0f,
                  Widgets::kAccent);
    char buf[8];
    snprintf(buf, sizeof(buf), "%u%%", pct);
    Widgets::text(g, kSideX + kSideW, kThinkY + 6, buf, Widgets::fontS(), Widgets::kTextMut,
                  Widgets::kRight);
    thinkingShown_ = pct;
    markRows(kThinkY, kThinkH);
    return;
  }
  thinkingShown_ = 255;

  // Lead bar: black's share of total area (komi counted for White) as a
  // black-fills-from-left bar with a centre reference tick. Early on it sits
  // near the middle because most of the board is still neutral - that is
  // honest, not a bug.
  litego::ScoreEstimate s = game_->estimateScore();
  float komi = s.komiX2 / 2.0f;
  float total = (float)s.blackArea + (float)s.whiteArea + komi;
  float blackFrac = total > 0.0f ? (float)s.blackArea / total : 0.5f;

  const int16_t barX = kSideX + 52;
  const int16_t barW = kSideW - 52 - 92;
  const int16_t barY = kThinkY + 8;
  const int16_t barH = 12;
  int16_t blackW = (int16_t)(blackFrac * barW + 0.5f);

  Widgets::text(g, kSideX, kThinkY + 6, "LEAD", Widgets::fontS(), Widgets::kTextMut,
                Widgets::kLeft);
  g->fillRoundRect(barX, barY, barW, barH, barH / 2, kWhiteStone);
  if (blackW > 0) {
    g->fillRect(barX, barY, blackW, barH, kBlackStone);
  }
  g->drawRoundRect(barX, barY, barW, barH, barH / 2, Widgets::kLine);
  g->drawFastVLine(barX + barW / 2, barY - 3, barH + 6, Widgets::kAccent);
  Widgets::text(g, kSideX + kSideW, kThinkY + 6, game_->resultText().c_str(), Widgets::fontS(),
                s.marginX2 == 0 ? Widgets::kTextMut : Widgets::kTextHi, Widgets::kRight);
  markRows(kThinkY, kThinkH);
}

void LiteGoTouchView::drawGameOver() {
  Arduino_GFX *g = CrowDisplay::canvas();
  const int16_t w = 300;
  const int16_t h = 108;
  const int16_t x = kTileX + (kTileSize - w) / 2;
  const int16_t y = kTileY + (kTileSize - h) / 2;

  Widgets::panel(g, x, y, w, h, 10, Widgets::kSurface, 2, Widgets::kAccent);
  Widgets::text(g, x + w / 2, y + 14, "GAME OVER", Widgets::fontS(), Widgets::kTextMut,
                Widgets::kCenter);
  Widgets::text(g, x + w / 2, y + 40, game_->resultText().c_str(), Widgets::fontL(),
                Widgets::kAccent, Widgets::kCenter);
  Widgets::text(g, x + w / 2, y + 74, "Tap NEW GAME to play again", Widgets::fontS(),
                Widgets::kTextMut, Widgets::kCenter);
  markRows(y, h);
}

void LiteGoTouchView::render() {
  Arduino_GFX *g = CrowDisplay::canvas();
  if (g == nullptr || game_ == nullptr) {
    return;
  }

  gFlushTop = 0x7FFF;
  gFlushBottom = -1;

  // The lead bar tracks the score, so remember whether it changed before the
  // flag is cleared below.
  bool scoreWasDirty = dirtyScore_;

  // Compute the endgame territory shading once, the first frame after the game
  // ends, and repaint the whole board to show it.
  if (game_->finished() && !territoryValid_) {
    game_->board().areaOwnership(territory_);
    territoryValid_ = true;
    dirtyBoard_ = true;
  }

  if (dirtyChrome_) {
    drawChrome();
    dirtyChrome_ = false;
    dirtyBoard_ = true;
  }

  bool boardRedrawn = dirtyBoard_;
  if (dirtyBoard_) {
    drawBoardAll();
    dirtyBoard_ = false;
  } else {
    boardRedrawn = dirtyPointCount_ > 0;
    for (uint8_t i = 0; i < dirtyPointCount_; i++) {
      drawCell((uint8_t)(dirtyPoints_[i] % LiteGoGame::kSize),
               (uint8_t)(dirtyPoints_[i] / LiteGoGame::kSize));
    }
    dirtyPointCount_ = 0;
  }

  if (dirtyScore_) {
    drawScorePanel();
    dirtyScore_ = false;
  }
  if (dirtyStatus_) {
    drawStatusPanel();
    dirtyStatus_ = false;
  }
  if (dirtyButtons_) {
    drawButtons();
    dirtyButtons_ = false;
  }
  // Eval strip: progress bar while searching (redraw when the percent moves),
  // otherwise the lead bar (redraw when the score changed or the search just
  // ended, marked by thinkingShown_ still holding a percent).
  if (thinking_) {
    if (game_->aiProgressPercent() != thinkingShown_) {
      drawEvalStrip();
    }
  } else if (scoreWasDirty || thinkingShown_ != 255) {
    drawEvalStrip();
  }

  // The overlay is only repainted when it first appears or when something
  // underneath it was redrawn; drawing it every pass would flush its rows on
  // every loop() for the rest of the game.
  if (game_->finished()) {
    if (!gameOverShown_ || boardRedrawn) {
      drawGameOver();
      gameOverShown_ = true;
    }
  } else {
    gameOverShown_ = false;
  }

  // One cache sync for every row that changed, instead of one per primitive.
  if (gFlushBottom > gFlushTop) {
    CrowDisplay::flush(0, gFlushTop, kScreenW, gFlushBottom - gFlushTop);
  }
}

#endif  // USE_DISPLAY && CONFIG_IDF_TARGET_ESP32P4
