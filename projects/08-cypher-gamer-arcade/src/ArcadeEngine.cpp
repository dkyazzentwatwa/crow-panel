#include "ArcadeEngine.h"

#include <CrowPanelShared.h>
#include <math.h>
#include <stdio.h>

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
#include <Arduino_GFX_Library.h>
#include "esp_heap_caps.h"
using namespace Widgets;
#endif

#if USE_SD_HIGHSCORES
#include <FS.h>
#include <SD_MMC.h>
#endif

namespace {
// --- Layout geometry (plain ints, visible to both the guarded render/touch
// paths and the unguarded game physics). ---
constexpr int16_t kScreenW = 1024;
constexpr int16_t kScreenH = 600;

// Pong playfield: composited into the offscreen canvas, blitted at (X,Y).
// 448x288 (~252 KB) stays inside the internal-SRAM envelope proven by the
// project 14 radar scope, so the fast internal path is likely to win.
constexpr int16_t kPongFieldW = 448;   // even => uint32 fast-copy blit path
constexpr int16_t kPongFieldH = 288;
constexpr int16_t kPongFieldX = 52;
constexpr int16_t kPongFieldY = 128;
constexpr int16_t kPongPaddleW = 12;
constexpr int16_t kPongPaddleH = 64;
constexpr int16_t kPongBall = 12;
constexpr int16_t kPongPadInset = 16;  // paddle x from the field edge

// Snake board (drawn directly; it changes only on each ~175 ms step).
constexpr int16_t kSnakeCell = 22;
constexpr int16_t kSnakeBoardX = 40;
constexpr int16_t kSnakeBoardY = 96;

// 2048 board (drawn directly; changes only on a move).
constexpr int16_t kTwentyCell = 88;
constexpr int16_t kTwentyGap = 12;
constexpr int16_t kTwentyFrameX = 48;
constexpr int16_t kTwentyFrameY = 104;
constexpr int16_t kTwentyBoardX = kTwentyFrameX + kTwentyGap;
constexpr int16_t kTwentyBoardY = kTwentyFrameY + kTwentyGap;

constexpr int16_t kSwipePx = 40;

// Header PAUSE button (top-right of the play chrome).
constexpr int16_t kPauseBtnW = 132;
constexpr int16_t kPauseBtnH = 44;
constexpr int16_t kPauseBtnX = kScreenW - 24 - kPauseBtnW;
constexpr int16_t kPauseBtnY = 14;

// Pause overlay card + its 2x2 button grid.
constexpr int16_t kPauseCardX = 232;
constexpr int16_t kPauseCardY = 140;
constexpr int16_t kPauseCardW = 560;
constexpr int16_t kPauseCardH = 340;
constexpr int16_t kPauseOptW = 228;
constexpr int16_t kPauseOptH = 64;

// Catalog / scores cards.
constexpr int16_t kCardW = 308;
constexpr int16_t kCardY = 96;
constexpr int16_t kCardH = 300;

int16_t cardX(uint8_t i) { return (int16_t)(24 + i * 334); }

void pauseOptRect(uint8_t i, int16_t &x, int16_t &y, int16_t &w, int16_t &h) {
  x = (i & 1) ? 524 : 272;
  y = (i >> 1) ? 372 : 284;
  w = kPauseOptW;
  h = kPauseOptH;
}

int16_t clampInt(int16_t value, int16_t low, int16_t high) {
  if (value < low) return low;
  if (value > high) return high;
  return value;
}

bool inRect(int16_t px, int16_t py, int16_t x, int16_t y, int16_t w, int16_t h) {
  return px >= x && px < (int16_t)(x + w) && py >= y && py < (int16_t)(y + h);
}

String normalized(String value) {
  value.trim();
  value.toLowerCase();
  return value;
}

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
// Offscreen Pong buffer. We inject the framebuffer before any draw so
// Arduino_Canvas never runs its own allocation, and we allocate in INTERNAL
// SRAM first (~10x faster than PSRAM for the per-frame recompose + blit) with a
// PSRAM fallback so a full internal heap never crashes the game. The animated
// region is deliberately small (448x288 ~= 252 KB) rather than a full-screen
// 1024x600 PSRAM buffer, which is the known-wrong approach for this panel.
class PongCanvas : public Arduino_Canvas {
 public:
  PongCanvas(int16_t w, int16_t h) : Arduino_Canvas(w, h, nullptr) {}
  bool alloc() {
    if (_framebuffer) return true;
    size_t sz = (size_t)WIDTH * HEIGHT * 2;
    _framebuffer = (uint16_t *)heap_caps_malloc(sz, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    internal_ = (_framebuffer != nullptr);
    if (!_framebuffer) _framebuffer = (uint16_t *)heap_caps_malloc(sz, MALLOC_CAP_SPIRAM);
    return _framebuffer != nullptr;
  }
  bool internal() const { return internal_; }

 private:
  bool internal_ = false;
};

// 2048 tile fill on the dark ops palette: a cool -> warm ramp as tiles grow.
uint16_t tileColor(uint16_t value) {
  switch (value) {
    case 2: return kSurfaceHi;
    case 4: return rgb(0x25, 0x4A, 0x5A);
    case 8: return rgb(0x1E, 0x6E, 0x74);
    case 16: return rgb(0x16, 0x9E, 0x9E);
    case 32: return rgb(0x2E, 0xB8, 0x7A);
    case 64: return kGreen;
    case 128: return rgb(0xB8, 0xC8, 0x3A);
    case 256: return kAmber;
    case 512: return rgb(0xF7, 0x9A, 0x33);
    case 1024: return rgb(0xF7, 0x70, 0x40);
    case 2048: return kRed;
    default: return value > 2048 ? rgb(0xE0, 0x4C, 0xC8) : kSurfaceHi;
  }
}

// Shared play-screen chrome: Widgets header + subtitle + a tappable PAUSE
// button. The button is hit-tested in handlePlayTouch().
void drawPlayHeader(Arduino_GFX *g, const char *title, const char *subtitle) {
  headerBar(g, title, subtitle, nullptr);
  touchButton(g, kPauseBtnX, kPauseBtnY, kPauseBtnW, kPauseBtnH, "PAUSE", false, kAccent);
}
#endif  // USE_DISPLAY && P4
}  // namespace

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void ArcadeEngine::begin() {
  randomSeed((uint32_t)micros());
  loadHighScores();
  screen_ = kScreenCatalog;
  activeGame_ = kMenu;
  paused_ = false;

#if USE_DISPLAY
  displayReady_ = CrowDisplay::begin(activeHardwareProfile(), "CYPHER GAMER", /*manualFlush=*/true);
  displayReady_ = displayReady_ && (CrowDisplay::canvas() != nullptr);
  Logger::info("ui", displayReady_ ? "arcade display ready" : "display flag set, no canvas");
#else
  displayReady_ = false;
  Logger::info("ui", "arcade display disabled");
#endif
  markDirty();
}

ArcadeEngine::UiEvent ArcadeEngine::tick() {
  touch_.tick();
  UiEvent ev = handleTouch();

  // Advance the active game's physics while it is on-screen and not paused.
  if (screen_ == kScreenPlay && !paused_) {
    if (activeGame_ == kPong) {
      updatePong(false);
    } else if (activeGame_ == kSnake) {
      updateSnake(false);
    }
  }

  render();
  return ev;
}

void ArcadeEngine::markDirty() { dirty_ = true; }

const char *ArcadeEngine::screenName() const {
  if (screen_ == kScreenPlay) return paused_ ? "play(paused)" : "play";
  if (screen_ == kScreenScores) return "scores";
  return "catalog";
}

void ArcadeEngine::startGame(GameId game) {
  releasePongCanvas();
  activeGame_ = game;
  screen_ = kScreenPlay;
  paused_ = false;
  if (game == kPong) {
    ensurePongCanvas();
    resetPong();
  } else if (game == kSnake) {
    resetSnake();
  } else if (game == kTwenty48) {
    resetTwenty48();
  } else {
    screen_ = kScreenCatalog;
    activeGame_ = kMenu;
  }
  Logger::info("arcade", String("start ") + gameName(activeGame_));
  markDirty();
}

// ---------------------------------------------------------------------------
// Serial command surface
// ---------------------------------------------------------------------------

void ArcadeEngine::showCatalog(Print &out) {
  releasePongCanvas();
  screen_ = kScreenCatalog;
  activeGame_ = kMenu;
  paused_ = false;
  markDirty();
  out.println(F("[catalog] Pong, Snake, and 2048 are playable"));
  out.println(F("[catalog] touch: tap a game card; drag the Pong paddle; swipe Snake/2048"));
  out.println(F("[catalog] in game: PAUSE opens resume/restart/scores/quit"));
  out.println(F("[catalog] serial: play <pong|snake|2048>, move <up|down|left|right>, step, reset, score"));
}

void ArcadeEngine::showScores() {
  releasePongCanvas();
  screen_ = kScreenScores;
  paused_ = false;
  markDirty();
}

void ArcadeEngine::play(const String &name, Print &out) {
  GameId game = parseGame(name);
  if (game == kMenu) {
    out.println(F("[play] unknown game; use pong, snake, or 2048"));
    return;
  }
  startGame(game);
  out.print(F("[play] "));
  out.println(gameName(game));
}

void ArcadeEngine::applyMove(Direction dir) {
  if (activeGame_ == kPong) {
    int16_t delta = (dir == kDirUp || dir == kDirLeft) ? -32 : 32;
    pong_.playerY = clampInt(pong_.playerY + delta, 0, kPongFieldH - kPongPaddleH);
  } else if (activeGame_ == kSnake) {
    setSnakeDirection(dir);
  } else if (activeGame_ == kTwenty48) {
    moveTwenty48(dir);
  }
}

void ArcadeEngine::move(const String &direction, Print &out) {
  Direction dir = parseDirection(direction);
  if (dir == kDirNone) {
    out.println(F("[move] use up, down, left, or right"));
    return;
  }
  if (screen_ != kScreenPlay || activeGame_ == kMenu) {
    out.println(F("[move] start a game first with play pong|snake|2048"));
    return;
  }
  applyMove(dir);
  out.print(F("[move] "));
  out.print(gameName(activeGame_));
  out.print(F(" score="));
  out.println(currentScore());
}

void ArcadeEngine::step(Print &out) {
  if (screen_ != kScreenPlay || activeGame_ == kMenu) {
    out.println(F("[step] start a game first with play pong|snake|2048"));
    return;
  }
  if (activeGame_ == kPong) {
    updatePong(true);
  } else if (activeGame_ == kSnake) {
    updateSnake(true);
  } else if (activeGame_ == kTwenty48) {
    out.println(F("[step] 2048 advances only on move/swipe"));
  }
  out.print(F("[step] "));
  out.print(gameName(activeGame_));
  out.print(F(" score="));
  out.println(currentScore());
}

void ArcadeEngine::reset(Print &out) {
  if (screen_ != kScreenPlay || activeGame_ == kMenu) {
    markDirty();
    out.println(F("[reset] catalog refreshed"));
    return;
  }
  startGame(activeGame_);
  out.print(F("[reset] "));
  out.println(gameName(activeGame_));
}

void ArcadeEngine::printScore(Print &out) const {
  out.print(F("[score] current "));
  out.print(gameName(activeGame_));
  out.print(F("="));
  out.println(currentScore());
  out.print(F("[score] highs pong="));
  out.print(highScores_[0]);
  out.print(F(" snake="));
  out.print(highScores_[1]);
  out.print(F(" 2048="));
  out.println(highScores_[2]);
}

void ArcadeEngine::printCalibration(Print &out) const {
  out.print(F("[cal] raw_x="));
  out.print(CROW_TOUCH_MIN_X);
  out.print(F(".."));
  out.print(CROW_TOUCH_MAX_X);
  out.print(F(" raw_y="));
  out.print(CROW_TOUCH_MIN_Y);
  out.print(F(".."));
  out.println(CROW_TOUCH_MAX_Y);
  out.print(F("[cal] swap_xy="));
  out.print(CROW_TOUCH_SWAP_XY);
  out.print(F(" invert_x="));
  out.print(CROW_TOUCH_INVERT_X);
  out.print(F(" invert_y="));
  out.println(CROW_TOUCH_INVERT_Y);
  out.print(F("[cal] last raw="));
  out.print(touch_.rawX());
  out.print(F(","));
  out.print(touch_.rawY());
  out.print(F(" mapped="));
  out.print(touch_.x());
  out.print(F(","));
  out.println(touch_.y());
}

void ArcadeEngine::printTouchDiag(Print &out) const {
  out.print(F("[touch] raw="));
  out.print(touch_.rawX());
  out.print(F(","));
  out.print(touch_.rawY());
  out.print(F(" mapped="));
  out.print(touch_.x());
  out.print(F(","));
  out.print(touch_.y());
  out.print(F(" down="));
  out.print(touch_.down() ? 1 : 0);
  out.print(F(" taps="));
  out.print(touch_.count());
  out.print(F(" screen="));
  out.print(screenName());
  if (screen_ == kScreenPlay) {
    out.print(F(" game="));
    out.print(gameName(activeGame_));
  }
  out.println();
}

void ArcadeEngine::printFlags(Print &out) const {
  out.print(F("[status] arcade USE_SD_HIGHSCORES="));
  out.print(USE_SD_HIGHSCORES);
  out.print(F(" sd_ready="));
  out.print(highScoreStoreReady_ ? 1 : 0);
  out.print(F(" display_ready="));
  out.print(displayReady_ ? 1 : 0);
  out.print(F(" pong_fb="));
  out.println(pongCanvas_ ? (pongCanvasInternal_ ? "internal" : "psram") : "none");
}

// ---------------------------------------------------------------------------
// Self test - drives the mock flow headlessly with explicit PASS/FAIL lines.
// ---------------------------------------------------------------------------

bool ArcadeEngine::runSelfTest(Print &out) {
  uint16_t passed = 0;
  uint16_t failed = 0;
  auto check = [&](const char *name, bool ok) {
    char line[80];
    snprintf(line, sizeof(line), "[selftest] %-32s %s", name, ok ? "PASS" : "FAIL");
    out.println(line);
    if (ok) {
      passed++;
    } else {
      failed++;
    }
  };

  // Command parsing.
  check("parse game pong", parseGame("pong") == kPong);
  check("parse game snake", parseGame("SNAKE ") == kSnake);
  check("parse game 2048", parseGame("2048") == kTwenty48);
  check("parse game rejects junk", parseGame("tetris") == kMenu);
  check("parse dir up/down/left/right",
        parseDirection("up") == kDirUp && parseDirection("down") == kDirDown &&
            parseDirection("left") == kDirLeft && parseDirection("right") == kDirRight);

  // Navigation.
  showScores();
  check("nav to scores", screen_ == kScreenScores);

  // Pong: ball advances and the paddle clamps inside the field.
  startGame(kPong);
  check("launch pong", screen_ == kScreenPlay && activeGame_ == kPong);
  float ballX0 = pong_.ballX;
  float ballY0 = pong_.ballY;
  updatePong(true);
  check("pong ball advances", pong_.ballX != ballX0 || pong_.ballY != ballY0);
  pong_.playerY = 0;
  applyMove(kDirUp);
  check("pong paddle clamps top", pong_.playerY == 0);
  pong_.playerY = kPongFieldH - kPongPaddleH;
  applyMove(kDirDown);
  check("pong paddle clamps bottom", pong_.playerY == kPongFieldH - kPongPaddleH);

  // Snake: reverse turns ignored; it advances and eventually ends.
  startGame(kSnake);
  check("launch snake", snake_.length == 5 && !snake_.gameOver);
  setSnakeDirection(kDirLeft);  // reverse of the default right
  check("snake ignores reverse", snake_.nextDir == kDirRight);
  uint8_t headX0 = snake_.x[0];
  updateSnake(true);
  check("snake head advances", snake_.x[0] != headX0 || snake_.gameOver);
  uint16_t guard = 0;
  while (!snake_.gameOver && guard++ < 400) updateSnake(true);
  check("snake terminates", snake_.gameOver);

  // 2048: seeds two tiles, moves stay valid, score never drops.
  startGame(kTwenty48);
  uint8_t seeded = 0;
  for (uint8_t y = 0; y < 4; y++)
    for (uint8_t x = 0; x < 4; x++)
      if (twenty_.cells[y][x] != 0) seeded++;
  check("2048 seeds two tiles", seeded == 2);
  check("2048 has a legal move", canMoveTwenty48());
  uint16_t prevScore = twenty_.score;
  bool scoreMonotonic = true;
  const Direction dirs[4] = {kDirLeft, kDirRight, kDirUp, kDirDown};
  for (uint8_t i = 0; i < 40; i++) {
    moveTwenty48(dirs[i & 3]);
    if (twenty_.score < prevScore) scoreMonotonic = false;
    prevScore = twenty_.score;
  }
  check("2048 score is monotonic", scoreMonotonic);

  // High-score bookkeeping keeps the maximum.
  uint16_t before2048 = highScores_[2];
  recordHighScore(kTwenty48, before2048 + 500);
  check("high score records a new best", highScores_[2] == before2048 + 500);
  recordHighScore(kTwenty48, before2048);  // lower value must not overwrite
  check("high score keeps the maximum", highScores_[2] == before2048 + 500);

  // Restart resets the live score.
  startGame(kPong);
  check("restart clears pong score", pong_.playerScore == 0 && pong_.cpuScore == 0);

  char summary[80];
  snprintf(summary, sizeof(summary), "[selftest] summary: %u passed, %u failed", passed, failed);
  out.println(summary);

  // Leave the engine on a clean catalog screen.
  releasePongCanvas();
  screen_ = kScreenCatalog;
  activeGame_ = kMenu;
  paused_ = false;
  markDirty();
  return failed == 0;
}

// ---------------------------------------------------------------------------
// Touch handling
// ---------------------------------------------------------------------------

ArcadeEngine::UiEvent ArcadeEngine::handleTouch() {
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  if (!displayReady_) return kEvNone;

  if (touch_.pressedEdge()) {
    pressX_ = touch_.x();
    pressY_ = touch_.y();
    pressOnControl_ = (screen_ == kScreenPlay && !paused_ &&
                       inRect(pressX_, pressY_, kPauseBtnX, kPauseBtnY, kPauseBtnW, kPauseBtnH));
  }

  // Drag surface: the Pong paddle follows the finger while it is held down.
  if (screen_ == kScreenPlay && activeGame_ == kPong && !paused_ && touch_.down()) {
    applyPaddleDrag();
  }

  UiEvent ev = kEvNone;
  if (touch_.releasedEdge()) {
    if (paused_) {
      ev = handlePauseTouch();
    } else if (screen_ == kScreenCatalog) {
      ev = handleCatalogTouch();
    } else if (screen_ == kScreenScores) {
      ev = handleScoresTouch();
    } else {
      ev = handlePlayTouch();
    }
  }
  return ev;
#else
  return kEvNone;
#endif
}

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
ArcadeEngine::UiEvent ArcadeEngine::handleCatalogTouch() {
  int16_t rx = touch_.releaseX();
  int16_t ry = touch_.releaseY();
  // Bottom tab bar: [ARCADE, SCORES].
  if (pressY_ >= kChromeTabY && ry >= kChromeTabY) {
    int8_t tab = tabHit(rx, ry, 2);
    if (tab == 1) return kEvShowScores;
    return kEvNone;
  }
  const UiEvent launch[3] = {kEvLaunchPong, kEvLaunchSnake, kEvLaunch2048};
  for (uint8_t i = 0; i < 3; i++) {
    if (inRect(pressX_, pressY_, cardX(i), kCardY, kCardW, kCardH) &&
        inRect(rx, ry, cardX(i), kCardY, kCardW, kCardH)) {
      return launch[i];
    }
  }
  return kEvNone;
}

ArcadeEngine::UiEvent ArcadeEngine::handleScoresTouch() {
  int16_t rx = touch_.releaseX();
  int16_t ry = touch_.releaseY();
  if (pressY_ >= kChromeTabY && ry >= kChromeTabY) {
    int8_t tab = tabHit(rx, ry, 2);
    if (tab == 0) return kEvQuitToCatalog;
    return kEvNone;
  }
  const UiEvent launch[3] = {kEvLaunchPong, kEvLaunchSnake, kEvLaunch2048};
  for (uint8_t i = 0; i < 3; i++) {
    if (inRect(pressX_, pressY_, cardX(i), kCardY, kCardW, kCardH) &&
        inRect(rx, ry, cardX(i), kCardY, kCardW, kCardH)) {
      return launch[i];
    }
  }
  return kEvNone;
}

ArcadeEngine::UiEvent ArcadeEngine::handlePlayTouch() {
  int16_t rx = touch_.releaseX();
  int16_t ry = touch_.releaseY();
  // PAUSE button (press and release both on it).
  if (inRect(pressX_, pressY_, kPauseBtnX, kPauseBtnY, kPauseBtnW, kPauseBtnH) &&
      inRect(rx, ry, kPauseBtnX, kPauseBtnY, kPauseBtnW, kPauseBtnH)) {
    paused_ = true;
    markDirty();
    return kEvNone;
  }
  // Swipe to steer Snake / 2048 (must not start on a control).
  if (!pressOnControl_ && (activeGame_ == kSnake || activeGame_ == kTwenty48)) {
    int16_t dx = rx - pressX_;
    int16_t dy = ry - pressY_;
    if (abs(dx) > kSwipePx || abs(dy) > kSwipePx) applySwipe(dx, dy);
  }
  return kEvNone;
}

ArcadeEngine::UiEvent ArcadeEngine::handlePauseTouch() {
  int16_t rx = touch_.releaseX();
  int16_t ry = touch_.releaseY();
  int16_t x, y, w, h;
  for (uint8_t i = 0; i < 4; i++) {
    pauseOptRect(i, x, y, w, h);
    if (inRect(pressX_, pressY_, x, y, w, h) && inRect(rx, ry, x, y, w, h)) {
      paused_ = false;
      switch (i) {
        case 0: markDirty(); return kEvNone;      // RESUME
        case 1: return kEvRestart;                // RESTART
        case 2: return kEvShowScores;             // SCORES
        default: return kEvQuitToCatalog;         // QUIT
      }
    }
  }
  return kEvNone;
}

void ArcadeEngine::applyPaddleDrag() {
  if (!inRect(touch_.x(), touch_.y(), kPongFieldX, kPongFieldY, kPongFieldW, kPongFieldH)) return;
  int16_t local = touch_.y() - kPongFieldY - kPongPaddleH / 2;
  pong_.playerY = clampInt(local, 0, kPongFieldH - kPongPaddleH);
}

void ArcadeEngine::applySwipe(int16_t dx, int16_t dy) {
  Direction dir;
  if (abs(dx) > abs(dy)) {
    dir = dx > 0 ? kDirRight : kDirLeft;
  } else {
    dir = dy > 0 ? kDirDown : kDirUp;
  }
  applyMove(dir);
}
#else
ArcadeEngine::UiEvent ArcadeEngine::handleCatalogTouch() { return kEvNone; }
ArcadeEngine::UiEvent ArcadeEngine::handleScoresTouch() { return kEvNone; }
ArcadeEngine::UiEvent ArcadeEngine::handlePlayTouch() { return kEvNone; }
ArcadeEngine::UiEvent ArcadeEngine::handlePauseTouch() { return kEvNone; }
void ArcadeEngine::applyPaddleDrag() {}
void ArcadeEngine::applySwipe(int16_t, int16_t) {}
#endif  // USE_DISPLAY && P4

// ---------------------------------------------------------------------------
// Pong
// ---------------------------------------------------------------------------

void ArcadeEngine::resetPong() {
  pong_.playerY = (kPongFieldH - kPongPaddleH) / 2;
  pong_.cpuY = pong_.playerY;
  pong_.ballX = kPongFieldW / 2;
  pong_.ballY = kPongFieldH / 2;
  pong_.ballVX = random(2) == 0 ? -5.0f : 5.0f;
  pong_.ballVY = random(2) == 0 ? -3.0f : 3.0f;
  pong_.playerScore = 0;
  pong_.cpuScore = 0;
  pong_.lastStepMs = millis();
}

void ArcadeEngine::updatePong(bool force) {
  uint32_t now = millis();
  if (!force && now - pong_.lastStepMs < 16) return;
  pong_.lastStepMs = now;

  pong_.ballX += pong_.ballVX;
  pong_.ballY += pong_.ballVY;

  if (pong_.ballY <= 0) {
    pong_.ballY = 0;
    pong_.ballVY = -pong_.ballVY;
  } else if (pong_.ballY >= kPongFieldH - kPongBall) {
    pong_.ballY = kPongFieldH - kPongBall;
    pong_.ballVY = -pong_.ballVY;
  }

  int16_t cpuCenter = pong_.cpuY + kPongPaddleH / 2;
  if (pong_.ballY > cpuCenter + 6) {
    pong_.cpuY += 4;
  } else if (pong_.ballY < cpuCenter - 6) {
    pong_.cpuY -= 4;
  }
  pong_.cpuY = clampInt(pong_.cpuY, 0, kPongFieldH - kPongPaddleH);

  int16_t playerRight = kPongPadInset + kPongPaddleW;
  int16_t cpuLeft = kPongFieldW - kPongPadInset - kPongPaddleW;
  bool hitPlayer = pong_.ballVX < 0 && pong_.ballX <= playerRight &&
                   pong_.ballX >= kPongPadInset - 4 &&
                   pong_.ballY + kPongBall >= pong_.playerY &&
                   pong_.ballY <= pong_.playerY + kPongPaddleH;
  bool hitCpu = pong_.ballVX > 0 && pong_.ballX + kPongBall >= cpuLeft &&
                pong_.ballX + kPongBall <= cpuLeft + kPongPaddleW + 4 &&
                pong_.ballY + kPongBall >= pong_.cpuY &&
                pong_.ballY <= pong_.cpuY + kPongPaddleH;

  if (hitPlayer) {
    float offset = (pong_.ballY + kPongBall / 2.0f) - (pong_.playerY + kPongPaddleH / 2.0f);
    pong_.ballVX = fabsf(pong_.ballVX) + 0.3f;
    pong_.ballVY = offset / 10.0f;
  } else if (hitCpu) {
    float offset = (pong_.ballY + kPongBall / 2.0f) - (pong_.cpuY + kPongPaddleH / 2.0f);
    pong_.ballVX = -(fabsf(pong_.ballVX) + 0.3f);
    pong_.ballVY = offset / 10.0f;
  }

  if (pong_.ballX < -kPongBall) {
    pong_.cpuScore++;
    pong_.ballX = kPongFieldW / 2;
    pong_.ballY = kPongFieldH / 2;
    pong_.ballVX = 5.0f;
    pong_.ballVY = random(2) == 0 ? -3.0f : 3.0f;
    markDirty();
  } else if (pong_.ballX > kPongFieldW) {
    pong_.playerScore++;
    recordHighScore(kPong, pong_.playerScore);
    pong_.ballX = kPongFieldW / 2;
    pong_.ballY = kPongFieldH / 2;
    pong_.ballVX = -5.0f;
    pong_.ballVY = random(2) == 0 ? -3.0f : 3.0f;
    markDirty();
  }
}

// ---------------------------------------------------------------------------
// Snake
// ---------------------------------------------------------------------------

void ArcadeEngine::resetSnake() {
  snake_.length = 5;
  uint8_t startX = kSnakeCols / 2;
  uint8_t startY = kSnakeRows / 2;
  for (uint8_t i = 0; i < snake_.length; i++) {
    snake_.x[i] = startX - i;
    snake_.y[i] = startY;
  }
  snake_.dir = kDirRight;
  snake_.nextDir = kDirRight;
  snake_.score = 0;
  snake_.gameOver = false;
  snake_.lastStepMs = millis();
  placeSnakeFood();
}

void ArcadeEngine::updateSnake(bool force) {
  if (snake_.gameOver) return;
  uint32_t now = millis();
  if (!force && now - snake_.lastStepMs < 175) return;
  snake_.lastStepMs = now;
  snake_.dir = snake_.nextDir;

  int8_t dx = 0;
  int8_t dy = 0;
  if (snake_.dir == kDirLeft) dx = -1;
  if (snake_.dir == kDirRight) dx = 1;
  if (snake_.dir == kDirUp) dy = -1;
  if (snake_.dir == kDirDown) dy = 1;

  int16_t headX = snake_.x[0] + dx;
  int16_t headY = snake_.y[0] + dy;
  bool ate = headX == snake_.foodX && headY == snake_.foodY;
  bool bodyHit = false;
  uint16_t collisionLimit = (!ate && snake_.length > 0) ? snake_.length - 1 : snake_.length;
  for (uint16_t i = 0; i < collisionLimit; i++) {
    if (snake_.x[i] == headX && snake_.y[i] == headY) {
      bodyHit = true;
      break;
    }
  }
  if (headX < 0 || headX >= kSnakeCols || headY < 0 || headY >= kSnakeRows || bodyHit) {
    snake_.gameOver = true;
    recordHighScore(kSnake, snake_.score);
    markDirty();
    return;
  }

  uint16_t limit = ate && snake_.length < kSnakeMax ? snake_.length : snake_.length - 1;
  for (int16_t i = limit; i > 0; i--) {
    snake_.x[i] = snake_.x[i - 1];
    snake_.y[i] = snake_.y[i - 1];
  }
  snake_.x[0] = (uint8_t)headX;
  snake_.y[0] = (uint8_t)headY;

  if (ate) {
    if (snake_.length < kSnakeMax) snake_.length++;
    snake_.score += 10;
    recordHighScore(kSnake, snake_.score);
    placeSnakeFood();
  }
  markDirty();
}

void ArcadeEngine::placeSnakeFood() {
  uint16_t empty = kSnakeMax - snake_.length;
  if (empty == 0) {
    snake_.foodX = 0;
    snake_.foodY = 0;
    return;
  }
  uint16_t target = random(empty);
  for (uint8_t y = 0; y < kSnakeRows; y++) {
    for (uint8_t x = 0; x < kSnakeCols; x++) {
      if (snakeUses(x, y)) continue;
      if (target == 0) {
        snake_.foodX = x;
        snake_.foodY = y;
        return;
      }
      target--;
    }
  }
}

bool ArcadeEngine::snakeUses(uint8_t x, uint8_t y) const {
  for (uint16_t i = 0; i < snake_.length; i++) {
    if (snake_.x[i] == x && snake_.y[i] == y) return true;
  }
  return false;
}

void ArcadeEngine::setSnakeDirection(Direction dir) {
  if (dir == kDirNone) return;
  bool reverse = (snake_.dir == kDirUp && dir == kDirDown) ||
                 (snake_.dir == kDirDown && dir == kDirUp) ||
                 (snake_.dir == kDirLeft && dir == kDirRight) ||
                 (snake_.dir == kDirRight && dir == kDirLeft);
  if (!reverse) {
    snake_.nextDir = dir;
  }
}

// ---------------------------------------------------------------------------
// 2048
// ---------------------------------------------------------------------------

void ArcadeEngine::resetTwenty48() {
  for (uint8_t y = 0; y < 4; y++) {
    for (uint8_t x = 0; x < 4; x++) {
      twenty_.cells[y][x] = 0;
    }
  }
  twenty_.score = 0;
  twenty_.gameOver = false;
  spawnTwenty48Tile();
  spawnTwenty48Tile();
}

bool ArcadeEngine::moveTwenty48(Direction dir) {
  if (dir == kDirNone || twenty_.gameOver) return false;
  bool moved = false;
  uint16_t gained = 0;

  for (uint8_t line = 0; line < 4; line++) {
    uint16_t input[4] = {0, 0, 0, 0};
    uint8_t count = 0;
    for (uint8_t offset = 0; offset < 4; offset++) {
      uint8_t x = 0;
      uint8_t y = 0;
      if (dir == kDirLeft) {
        x = offset;
        y = line;
      } else if (dir == kDirRight) {
        x = 3 - offset;
        y = line;
      } else if (dir == kDirUp) {
        x = line;
        y = offset;
      } else {
        x = line;
        y = 3 - offset;
      }
      uint16_t value = twenty_.cells[y][x];
      if (value != 0) input[count++] = value;
    }

    uint16_t output[4] = {0, 0, 0, 0};
    uint8_t out = 0;
    for (uint8_t i = 0; i < count; i++) {
      if (i + 1 < count && input[i] == input[i + 1]) {
        output[out] = input[i] * 2;
        gained += output[out];
        i++;
      } else {
        output[out] = input[i];
      }
      out++;
    }

    for (uint8_t offset = 0; offset < 4; offset++) {
      uint8_t x = 0;
      uint8_t y = 0;
      if (dir == kDirLeft) {
        x = offset;
        y = line;
      } else if (dir == kDirRight) {
        x = 3 - offset;
        y = line;
      } else if (dir == kDirUp) {
        x = line;
        y = offset;
      } else {
        x = line;
        y = 3 - offset;
      }
      if (twenty_.cells[y][x] != output[offset]) {
        moved = true;
      }
      twenty_.cells[y][x] = output[offset];
    }
  }

  if (!moved) return false;
  twenty_.score += gained;
  spawnTwenty48Tile();
  if (!canMoveTwenty48()) {
    twenty_.gameOver = true;
  }
  recordHighScore(kTwenty48, twenty_.score);
  markDirty();
  return true;
}

bool ArcadeEngine::canMoveTwenty48() const {
  for (uint8_t y = 0; y < 4; y++) {
    for (uint8_t x = 0; x < 4; x++) {
      uint16_t value = twenty_.cells[y][x];
      if (value == 0) return true;
      if (x < 3 && value == twenty_.cells[y][x + 1]) return true;
      if (y < 3 && value == twenty_.cells[y + 1][x]) return true;
    }
  }
  return false;
}

void ArcadeEngine::spawnTwenty48Tile() {
  uint8_t empty = 0;
  for (uint8_t y = 0; y < 4; y++) {
    for (uint8_t x = 0; x < 4; x++) {
      if (twenty_.cells[y][x] == 0) empty++;
    }
  }
  if (empty == 0) return;
  uint8_t target = random(empty);
  for (uint8_t y = 0; y < 4; y++) {
    for (uint8_t x = 0; x < 4; x++) {
      if (twenty_.cells[y][x] != 0) continue;
      if (target == 0) {
        twenty_.cells[y][x] = random(10) == 0 ? 4 : 2;
        return;
      }
      target--;
    }
  }
}

// ---------------------------------------------------------------------------
// Offscreen playfield canvas
// ---------------------------------------------------------------------------

bool ArcadeEngine::ensurePongCanvas() {
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  if (pongCanvas_) return true;
  PongCanvas *canvas = new PongCanvas(kPongFieldW, kPongFieldH);
  if (!canvas || !canvas->alloc()) {
    delete canvas;
    pongCanvas_ = nullptr;
    Logger::error("arcade", "pong offscreen canvas alloc failed");
    return false;
  }
  pongCanvasInternal_ = canvas->internal();
  pongCanvas_ = canvas;
  Logger::info("arcade", String("pong offscreen canvas ") +
                             (pongCanvasInternal_ ? "internal SRAM" : "PSRAM fallback"));
  return true;
#else
  return false;
#endif
}

void ArcadeEngine::releasePongCanvas() {
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  if (pongCanvas_) {
    delete pongCanvas_;
    pongCanvas_ = nullptr;
    pongCanvasInternal_ = false;
  }
#endif
}

void ArcadeEngine::blitPongField() {
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  if (!pongCanvas_) return;
  uint16_t *fb = pongCanvas_->getFramebuffer();
  Arduino_GFX *g = CrowDisplay::canvas();
  if (!fb || !g) return;
  g->draw16bitRGBBitmap(kPongFieldX, kPongFieldY, fb, kPongFieldW, kPongFieldH);
#endif
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

void ArcadeEngine::render() {
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  if (!displayReady_) {
    dirty_ = false;
    return;
  }
  Arduino_GFX *g = CrowDisplay::canvas();
  if (!g) {
    dirty_ = false;
    return;
  }

  bool full = dirty_;
  if (full) {
    g->fillScreen(kBg);
    if (screen_ == kScreenCatalog) {
      renderCatalog();
    } else if (screen_ == kScreenScores) {
      renderScores();
    } else {
      renderPlayStatic();
      // Draw the Pong field (live, or frozen when paused) so a pause overlay
      // sits over the actual game rather than an empty bezel.
      if (activeGame_ == kPong) {
        renderPongField();
        blitPongField();
        lastPongDrawMs_ = millis();
      }
    }
    if (paused_) renderPauseOverlay();
    dirty_ = false;
  }

  // Pong is the only per-frame animator: recompose the offscreen field and blit
  // it with a fast field-region flush while the game is actively running.
  if (screen_ == kScreenPlay && activeGame_ == kPong && !paused_ && !full) {
    uint32_t now = millis();
    if (now - lastPongDrawMs_ >= 16) {
      lastPongDrawMs_ = now;
      renderPongField();
      blitPongField();
      CrowDisplay::flush(kPongFieldX, kPongFieldY, kPongFieldW, kPongFieldH);
    }
  }

  if (full) CrowDisplay::flush();
#else
  dirty_ = false;
#endif
}

void ArcadeEngine::renderPlayStatic() {
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  switch (activeGame_) {
    case kPong: renderPongStatic(); break;
    case kSnake: renderSnake(); break;
    case kTwenty48: renderTwenty48(); break;
    default: break;
  }
#endif
}

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
void ArcadeEngine::renderCatalog() {
  Arduino_GFX *g = CrowDisplay::canvas();
  headerBar(g, "CYPHER GAMER", "Touch-first arcade - tap a game to play", "ARCADE", kAccent);

  const char *names[3] = {"PONG", "SNAKE", "2048"};
  const char *copy[3] = {"Drag the paddle", "Swipe to turn", "Swipe to merge"};
  const uint16_t accents[3] = {kAccent, kGreen, kAmber};
  for (uint8_t i = 0; i < 3; i++) {
    int16_t x = cardX(i);
    panel(g, x, kCardY, kCardW, kCardH, 16, kSurface, 1, kLine);
    g->fillRoundRect(x, kCardY, kCardW, 8, 4, accents[i]);
    text(g, x + 24, kCardY + 40, names[i], fontXL(), kTextHi, kLeft);
    text(g, x + 24, kCardY + 96, copy[i], fontS(), kTextMut, kLeft);
    text(g, x + 24, kCardY + 150, "HIGH", fontS(), kTextMut, kLeft);
    char hv[12];
    snprintf(hv, sizeof(hv), "%u", highScores_[i]);
    text(g, x + 24, kCardY + 170, hv, fontL(), accents[i], kLeft);
    text(g, x + kCardW - 24, kCardY + kCardH - 34, "PLAY >", fontS(), accents[i], kRight);
  }

  panel(g, 24, 412, 976, 108, 14, kSurface, 1, kLine);
  text(g, 44, 430, "Serial smoke: catalog | play pong | move up | step | score | selftest", fontS(),
       kTextHi, kLeft);
  text(g, 44, 460, "Proof: compile-ready. Touch play and SD high scores not yet observed on hardware.",
       fontS(), kTextMut, kLeft);
  text(g, 44, 490,
       USE_SD_HIGHSCORES ? "SD high-score persistence flag enabled." : "RAM high scores (SD flag off).",
       fontS(), kTextMut, kLeft);

  static const char *tabs[2] = {"ARCADE", "SCORES"};
  tabBar(g, tabs, 2, 0, kAccent);
}

void ArcadeEngine::renderScores() {
  Arduino_GFX *g = CrowDisplay::canvas();
  headerBar(g, "HIGH SCORES", "Best runs this session - tap a card to play", "SCORES", kAmber);

  const char *names[3] = {"PONG", "SNAKE", "2048"};
  const uint16_t accents[3] = {kAccent, kGreen, kAmber};
  for (uint8_t i = 0; i < 3; i++) {
    int16_t x = cardX(i);
    panel(g, x, kCardY, kCardW, kCardH, 16, kSurface, 1, kLine);
    g->fillRoundRect(x, kCardY, kCardW, 8, 4, accents[i]);
    text(g, x + 24, kCardY + 40, names[i], fontL(), kTextHi, kLeft);
    text(g, x + 24, kCardY + 96, "BEST", fontS(), kTextMut, kLeft);
    char hv[12];
    snprintf(hv, sizeof(hv), "%u", highScores_[i]);
    text(g, x + kCardW / 2, kCardY + 150, hv, fontXL(), accents[i], kCenter);
    text(g, x + 24, kCardY + kCardH - 60,
         USE_SD_HIGHSCORES ? (highScoreStoreReady_ ? "source: SD card" : "source: RAM (SD off)")
                           : "source: RAM",
         fontS(), kTextMut, kLeft);
    text(g, x + kCardW - 24, kCardY + kCardH - 34, "PLAY >", fontS(), accents[i], kRight);
  }

  panel(g, 24, 412, 976, 108, 14, kSurface, 1, kLine);
  char line[96];
  snprintf(line, sizeof(line), "Persistence: %s   sd_ready=%d",
           USE_SD_HIGHSCORES ? "/cypher-gamer-scores.txt (SD_MMC)" : "RAM only (build with USE_SD_HIGHSCORES=1)",
           highScoreStoreReady_ ? 1 : 0);
  text(g, 44, 440, line, fontS(), kTextHi, kLeft);
  text(g, 44, 476, "High scores update live as you play; serial `score` prints the same values.",
       fontS(), kTextMut, kLeft);

  static const char *tabs[2] = {"ARCADE", "SCORES"};
  tabBar(g, tabs, 2, 1, kAmber);
}

void ArcadeEngine::renderPongStatic() {
  Arduino_GFX *g = CrowDisplay::canvas();
  char sub[40];
  snprintf(sub, sizeof(sub), "YOU %u    CPU %u", pong_.playerScore, pong_.cpuScore);
  drawPlayHeader(g, "PONG", sub);

  // Field bezel; the animated field blits into the interior.
  panel(g, kPongFieldX - 6, kPongFieldY - 6, kPongFieldW + 12, kPongFieldH + 12, 12, kSurface, 1,
        kLine);

  // Right info rail.
  const int16_t rx = 540;
  const int16_t rw = kScreenW - rx - 24;
  panel(g, rx, kPongFieldY - 6, rw, kPongFieldH + 12, 14, kSurface, 1, kLine);
  text(g, rx + 24, kPongFieldY + 18, "HIGH SCORE", fontS(), kTextMut, kLeft);
  char hv[12];
  snprintf(hv, sizeof(hv), "%u", highScores_[0]);
  text(g, rx + 24, kPongFieldY + 44, hv, fontXL(), kAccent, kLeft);
  g->drawFastHLine(rx + 24, kPongFieldY + 108, rw - 48, kLine);
  char you[16];
  snprintf(you, sizeof(you), "YOU  %u", pong_.playerScore);
  text(g, rx + 24, kPongFieldY + 128, you, fontL(), kTextHi, kLeft);
  char cpu[16];
  snprintf(cpu, sizeof(cpu), "CPU  %u", pong_.cpuScore);
  text(g, rx + 24, kPongFieldY + 162, cpu, fontL(), kAmber, kLeft);
  text(g, rx + 24, kPongFieldY + 216, "Drag inside the field to", fontS(), kTextMut, kLeft);
  text(g, rx + 24, kPongFieldY + 240, "move your paddle. Serial:", fontS(), kTextMut, kLeft);
  text(g, rx + 24, kPongFieldY + 264, "move up/down, step, reset.", fontS(), kTextMut, kLeft);
}

void ArcadeEngine::renderPongField() {
  Arduino_GFX *g = pongCanvas_;
  if (!g) return;
  g->fillScreen(kBg);
  // Center net.
  for (int16_t y = 8; y < kPongFieldH - 8; y += 26) {
    g->fillRect(kPongFieldW / 2 - 2, y, 4, 14, kLine);
  }
  g->fillRect(kPongPadInset, pong_.playerY, kPongPaddleW, kPongPaddleH, kAccent);
  g->fillRect(kPongFieldW - kPongPadInset - kPongPaddleW, pong_.cpuY, kPongPaddleW, kPongPaddleH,
              kAmber);
  g->fillRect((int16_t)pong_.ballX, (int16_t)pong_.ballY, kPongBall, kPongBall, kTextHi);
}

void ArcadeEngine::renderSnake() {
  Arduino_GFX *g = CrowDisplay::canvas();
  char sub[24];
  snprintf(sub, sizeof(sub), "SCORE %u", snake_.score);
  drawPlayHeader(g, "SNAKE", sub);

  const int16_t bw = kSnakeCols * kSnakeCell;
  const int16_t bh = kSnakeRows * kSnakeCell;
  panel(g, kSnakeBoardX - 6, kSnakeBoardY - 6, bw + 12, bh + 12, 12, kSurface, 1, kLine);
  g->fillRect(kSnakeBoardX, kSnakeBoardY, bw, bh, kBg);
  for (uint8_t y = 0; y < kSnakeRows; y++) {
    for (uint8_t x = 0; x < kSnakeCols; x++) {
      if (((x + y) & 1) == 0) {
        g->fillRect(kSnakeBoardX + x * kSnakeCell, kSnakeBoardY + y * kSnakeCell, kSnakeCell,
                    kSnakeCell, kSurface);
      }
    }
  }
  g->fillRoundRect(kSnakeBoardX + snake_.foodX * kSnakeCell + 4,
                   kSnakeBoardY + snake_.foodY * kSnakeCell + 4, kSnakeCell - 8, kSnakeCell - 8, 4,
                   kRed);
  for (uint16_t i = 0; i < snake_.length; i++) {
    uint16_t color = (i == 0) ? kAccent : kGreen;
    g->fillRoundRect(kSnakeBoardX + snake_.x[i] * kSnakeCell + 3,
                     kSnakeBoardY + snake_.y[i] * kSnakeCell + 3, kSnakeCell - 6, kSnakeCell - 6, 3,
                     color);
  }

  const int16_t rx = kSnakeBoardX + bw + 30;
  const int16_t rw = kScreenW - rx - 24;
  panel(g, rx, kSnakeBoardY - 6, rw, bh + 12, 14, kSurface, 1, kLine);
  text(g, rx + 24, kSnakeBoardY + 18, "HIGH SCORE", fontS(), kTextMut, kLeft);
  char hv[12];
  snprintf(hv, sizeof(hv), "%u", highScores_[1]);
  text(g, rx + 24, kSnakeBoardY + 44, hv, fontXL(), kGreen, kLeft);
  g->drawFastHLine(rx + 24, kSnakeBoardY + 108, rw - 48, kLine);
  text(g, rx + 24, kSnakeBoardY + 128, snake_.gameOver ? "GAME OVER" : "CHASE FOOD", fontL(),
       snake_.gameOver ? kRed : kGreen, kLeft);
  text(g, rx + 24, kSnakeBoardY + 176, "Swipe anywhere to turn.", fontS(), kTextMut, kLeft);
  text(g, rx + 24, kSnakeBoardY + 200, "Reverse turns are ignored.", fontS(), kTextMut, kLeft);
  text(g, rx + 24, kSnakeBoardY + 240, "PAUSE to restart or quit.", fontS(), kTextMut, kLeft);
}

void ArcadeEngine::renderTwenty48() {
  Arduino_GFX *g = CrowDisplay::canvas();
  char sub[24];
  snprintf(sub, sizeof(sub), "SCORE %u", twenty_.score);
  drawPlayHeader(g, "2048", sub);

  const int16_t frame = 4 * kTwentyCell + 5 * kTwentyGap;
  panel(g, kTwentyFrameX, kTwentyFrameY, frame, frame, 14, kSurface, 1, kLine);
  for (uint8_t y = 0; y < 4; y++) {
    for (uint8_t x = 0; x < 4; x++) {
      uint16_t value = twenty_.cells[y][x];
      int16_t tileX = kTwentyBoardX + x * (kTwentyCell + kTwentyGap);
      int16_t tileY = kTwentyBoardY + y * (kTwentyCell + kTwentyGap);
      g->fillRoundRect(tileX, tileY, kTwentyCell, kTwentyCell, 8,
                       value ? tileColor(value) : kSurfaceHi);
      if (value != 0) {
        char num[8];
        snprintf(num, sizeof(num), "%u", value);
        const GFXfont *font = value < 100 ? fontXL() : (value < 1000 ? fontL() : fontM());
        int16_t voff = value < 100 ? 17 : 9;
        uint16_t ink = value < 32 ? kTextHi : kBg;
        text(g, tileX + kTwentyCell / 2, tileY + kTwentyCell / 2 - voff, num, font, ink, kCenter);
      }
    }
  }

  const int16_t rx = kTwentyFrameX + frame + 30;
  const int16_t rw = kScreenW - rx - 24;
  panel(g, rx, kTwentyFrameY, rw, frame, 14, kSurface, 1, kLine);
  text(g, rx + 24, kTwentyFrameY + 24, "HIGH SCORE", fontS(), kTextMut, kLeft);
  char hv[12];
  snprintf(hv, sizeof(hv), "%u", highScores_[2]);
  text(g, rx + 24, kTwentyFrameY + 50, hv, fontXL(), kAmber, kLeft);
  g->drawFastHLine(rx + 24, kTwentyFrameY + 114, rw - 48, kLine);
  text(g, rx + 24, kTwentyFrameY + 134, twenty_.gameOver ? "GAME OVER" : "MERGE TO 2048", fontL(),
       twenty_.gameOver ? kRed : kAmber, kLeft);
  text(g, rx + 24, kTwentyFrameY + 182, "Swipe to slide and merge", fontS(), kTextMut, kLeft);
  text(g, rx + 24, kTwentyFrameY + 206, "equal tiles. PAUSE for", fontS(), kTextMut, kLeft);
  text(g, rx + 24, kTwentyFrameY + 230, "restart, scores, or quit.", fontS(), kTextMut, kLeft);
}

void ArcadeEngine::renderPauseOverlay() {
  Arduino_GFX *g = CrowDisplay::canvas();
  panel(g, kPauseCardX, kPauseCardY, kPauseCardW, kPauseCardH, 18, kSurfaceHi, 2, kAccent);
  text(g, kScreenW / 2, kPauseCardY + 30, "PAUSED", fontXL(), kTextHi, kCenter);
  text(g, kScreenW / 2, kPauseCardY + 82, gameTitle(activeGame_), fontS(), kTextMut, kCenter);

  const char *labels[4] = {"RESUME", "RESTART", "SCORES", "QUIT"};
  const uint16_t accents[4] = {kGreen, kAccent, kAmber, kRed};
  int16_t x, y, w, h;
  for (uint8_t i = 0; i < 4; i++) {
    pauseOptRect(i, x, y, w, h);
    touchButton(g, x, y, w, h, labels[i], true, accents[i]);
  }
}
#else  // headless: rendering compiles to no-ops
void ArcadeEngine::renderCatalog() {}
void ArcadeEngine::renderScores() {}
void ArcadeEngine::renderPongStatic() {}
void ArcadeEngine::renderPongField() {}
void ArcadeEngine::renderSnake() {}
void ArcadeEngine::renderTwenty48() {}
void ArcadeEngine::renderPauseOverlay() {}
#endif  // USE_DISPLAY && P4

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

ArcadeEngine::Direction ArcadeEngine::parseDirection(const String &direction) const {
  String value = normalized(direction);
  if (value == "up" || value == "u" || value == "w") return kDirUp;
  if (value == "down" || value == "d" || value == "s") return kDirDown;
  if (value == "left" || value == "l" || value == "a") return kDirLeft;
  if (value == "right" || value == "r") return kDirRight;
  return kDirNone;
}

ArcadeEngine::GameId ArcadeEngine::parseGame(const String &name) const {
  String value = normalized(name);
  if (value == "pong") return kPong;
  if (value == "snake") return kSnake;
  if (value == "2048" || value == "twenty48" || value == "twenty") return kTwenty48;
  return kMenu;
}

const char *ArcadeEngine::gameName(GameId game) const {
  switch (game) {
    case kPong: return "pong";
    case kSnake: return "snake";
    case kTwenty48: return "2048";
    default: return "catalog";
  }
}

const char *ArcadeEngine::gameTitle(GameId game) const {
  switch (game) {
    case kPong: return "PONG";
    case kSnake: return "SNAKE";
    case kTwenty48: return "2048";
    default: return "CATALOG";
  }
}

uint16_t ArcadeEngine::currentScore() const {
  if (activeGame_ == kPong) return pong_.playerScore;
  if (activeGame_ == kSnake) return snake_.score;
  if (activeGame_ == kTwenty48) return twenty_.score;
  return 0;
}

uint8_t ArcadeEngine::scoreIndex(GameId game) const {
  if (game == kSnake) return 1;
  if (game == kTwenty48) return 2;
  return 0;
}

void ArcadeEngine::recordHighScore(GameId game, uint16_t score) {
  uint8_t index = scoreIndex(game);
  if (score <= highScores_[index]) return;
  highScores_[index] = score;
  saveHighScores();
}

void ArcadeEngine::loadHighScores() {
#if USE_SD_HIGHSCORES
  highScoreStoreReady_ = SD_MMC.begin("/sdcard", ARCADE_SDMMC_1BIT != 0);
  if (!highScoreStoreReady_) {
    Logger::error("storage", "SD_MMC high-score mount failed; RAM scores active");
    return;
  }
  File file = SD_MMC.open("/cypher-gamer-scores.txt", FILE_READ);
  if (!file) {
    Logger::info("storage", "no SD high-score file yet");
    return;
  }
  while (file.available()) {
    String line = file.readStringUntil('\n');
    line.trim();
    int split = line.indexOf('=');
    if (split < 0) continue;
    String key = line.substring(0, split);
    uint16_t value = (uint16_t)line.substring(split + 1).toInt();
    if (key == "pong") highScores_[0] = value;
    if (key == "snake") highScores_[1] = value;
    if (key == "2048") highScores_[2] = value;
  }
  file.close();
#else
  highScoreStoreReady_ = false;
  Logger::info("storage", "SD high scores disabled; RAM scores active");
#endif
}

void ArcadeEngine::saveHighScores() {
#if USE_SD_HIGHSCORES
  if (!highScoreStoreReady_) return;
  File file = SD_MMC.open("/cypher-gamer-scores.txt", FILE_WRITE);
  if (!file) {
    Logger::error("storage", "could not write SD high scores");
    return;
  }
  file.print(F("pong="));
  file.println(highScores_[0]);
  file.print(F("snake="));
  file.println(highScores_[1]);
  file.print(F("2048="));
  file.println(highScores_[2]);
  file.close();
#endif
}
