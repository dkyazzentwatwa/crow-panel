#include "ArcadeEngine.h"

#include <CrowPanelShared.h>

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
#include <Arduino_GFX_Library.h>
#endif

#if USE_SD_HIGHSCORES
#include <FS.h>
#include <SD_MMC.h>
#endif

namespace {
constexpr int16_t kScreenW = 1024;
constexpr int16_t kScreenH = 600;
constexpr int16_t kHeaderH = 76;
constexpr int16_t kFooterH = 38;
constexpr int16_t kPongX = 28;
constexpr int16_t kPongY = 92;
constexpr int16_t kPongW = 968;
constexpr int16_t kPongH = 452;
constexpr int16_t kPaddleW = 16;
constexpr int16_t kPaddleH = 104;
constexpr int16_t kBallSize = 16;
constexpr int16_t kSnakeCell = 26;
constexpr int16_t kSnakeBoardX = 58;
constexpr int16_t kSnakeBoardY = 106;
constexpr int16_t kSnakeBoardW = 22 * kSnakeCell;
constexpr int16_t kSnakeBoardH = 16 * kSnakeCell;
constexpr int16_t kTwentyBoardX = 286;
constexpr int16_t kTwentyBoardY = 98;
constexpr int16_t kTwentyCell = 94;
constexpr int16_t kTwentyGap = 10;
constexpr int16_t kTouchSwipePx = 42;

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
constexpr uint16_t kBg = 0x0841;
constexpr uint16_t kSurface = 0x18E3;
constexpr uint16_t kSurfaceHi = 0x31C6;
constexpr uint16_t kText = 0xF79E;
constexpr uint16_t kMuted = 0x9CF3;
constexpr uint16_t kLine = 0x4A69;
constexpr uint16_t kCyan = 0x07FF;
constexpr uint16_t kAmber = 0xFDC0;
constexpr uint16_t kGreen = 0x57EA;
constexpr uint16_t kRed = 0xF986;
constexpr uint16_t kBlue = 0x3D9F;
constexpr uint16_t kPurple = 0x9B7F;

Arduino_GFX *gfx() {
  return CrowDisplay::canvas();
}

void textAt(int16_t x, int16_t y, const char *text, uint8_t size, uint16_t color) {
  Arduino_GFX *g = gfx();
  if (!g) return;
  g->setTextWrap(false);
  g->setTextColor(color);
  g->setTextSize(size);
  g->setCursor(x, y);
  g->print(text);
}

void textAt(int16_t x, int16_t y, const String &text, uint8_t size, uint16_t color) {
  textAt(x, y, text.c_str(), size, color);
}

void panel(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t fill, uint16_t stroke) {
  Arduino_GFX *g = gfx();
  if (!g) return;
  g->fillRect(x, y, w, h, fill);
  g->drawRect(x, y, w, h, stroke);
}

void button(int16_t x, int16_t y, int16_t w, int16_t h, const char *label, uint16_t fill) {
  panel(x, y, w, h, fill, kLine);
  textAt(x + 14, y + 14, label, 2, kText);
}

uint16_t tileColor(uint16_t value) {
  switch (value) {
    case 2: return 0xE71C;
    case 4: return 0xF73C;
    case 8: return 0xFD20;
    case 16: return 0xFB20;
    case 32: return 0xF9E8;
    case 64: return 0xF800;
    case 128: return 0xFFE0;
    case 256: return 0xEFE0;
    case 512: return 0xC7E0;
    case 1024: return 0x9FE0;
    case 2048: return 0x57EA;
    default: return value > 2048 ? kPurple : kSurfaceHi;
  }
}
#endif

int16_t clampInt(int16_t value, int16_t low, int16_t high) {
  if (value < low) return low;
  if (value > high) return high;
  return value;
}

int16_t mapAxis(int16_t value, int16_t inMin, int16_t inMax, int16_t outMax) {
  if (inMax == inMin) return 0;
  long mapped = ((long)value - inMin) * outMax / ((long)inMax - inMin);
  if (mapped < 0) mapped = 0;
  if (mapped > outMax) mapped = outMax;
  return (int16_t)mapped;
}

String normalized(String value) {
  value.trim();
  value.toLowerCase();
  return value;
}
}  // namespace

void ArcadeEngine::begin() {
  randomSeed((uint32_t)micros());
  loadHighScores();
  showMenu();

#if USE_DISPLAY
  displayReady_ = CrowDisplay::begin(activeHardwareProfile(), "CYPHER GAMER");
  displayReady_ = displayReady_ && (CrowDisplay::canvas() != nullptr);
  Logger::info("ui", displayReady_ ? "arcade display ready" : "display flag set, no canvas");
#else
  displayReady_ = false;
  Logger::info("ui", "arcade display disabled");
#endif
  markDirty();
}

void ArcadeEngine::tick() {
  pollTouch();
  handleTouch();

  if (activeGame_ == kPong) {
    updatePong(false);
  } else if (activeGame_ == kSnake) {
    updateSnake(false);
  }

  render();
}

void ArcadeEngine::showCatalog(Print &out) {
  showMenu();
  out.println(F("[catalog] Pong, Snake, and 2048 are playable"));
  out.println(F("[catalog] touch: tap a card, drag Pong paddle, swipe Snake/2048"));
  out.println(F("[catalog] serial: play <pong|snake|2048>, move <up|down|left|right>, step, reset, score"));
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

void ArcadeEngine::move(const String &direction, Print &out) {
  Direction dir = parseDirection(direction);
  if (dir == kDirNone) {
    out.println(F("[move] use up, down, left, or right"));
    return;
  }

  if (activeGame_ == kPong) {
    int16_t delta = (dir == kDirUp || dir == kDirLeft) ? -36 : 36;
    pong_.playerY = clampInt(pong_.playerY + delta, kPongY, kPongY + kPongH - kPaddleH);
    markDirty();
  } else if (activeGame_ == kSnake) {
    setSnakeDirection(dir);
  } else if (activeGame_ == kTwenty48) {
    moveTwenty48(dir);
  } else {
    out.println(F("[move] start a game first with play pong|snake|2048"));
    return;
  }

  out.print(F("[move] "));
  out.print(gameName(activeGame_));
  out.print(F(" score="));
  out.println(currentScore());
}

void ArcadeEngine::step(Print &out) {
  if (activeGame_ == kPong) {
    updatePong(true);
  } else if (activeGame_ == kSnake) {
    updateSnake(true);
  } else if (activeGame_ == kTwenty48) {
    out.println(F("[step] 2048 advances only on move/swipe"));
  } else {
    out.println(F("[step] menu is idle; start a game first"));
    return;
  }

  out.print(F("[step] "));
  out.print(gameName(activeGame_));
  out.print(F(" score="));
  out.println(currentScore());
}

void ArcadeEngine::reset(Print &out) {
  if (activeGame_ == kMenu) {
    showMenu();
    out.println(F("[reset] menu refreshed"));
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
  out.print(ARCADE_TOUCH_MIN_X);
  out.print(F(".."));
  out.print(ARCADE_TOUCH_MAX_X);
  out.print(F(" raw_y="));
  out.print(ARCADE_TOUCH_MIN_Y);
  out.print(F(".."));
  out.println(ARCADE_TOUCH_MAX_Y);
  out.print(F("[cal] swap_xy="));
  out.print(ARCADE_TOUCH_SWAP_XY);
  out.print(F(" invert_x="));
  out.print(ARCADE_TOUCH_INVERT_X);
  out.print(F(" invert_y="));
  out.println(ARCADE_TOUCH_INVERT_Y);
  out.print(F("[cal] last raw="));
  out.print(touch_.rawX);
  out.print(F(","));
  out.print(touch_.rawY);
  out.print(F(" mapped="));
  out.print(touch_.x);
  out.print(F(","));
  out.println(touch_.y);
}

void ArcadeEngine::printFlags(Print &out) const {
  out.print(F("[status] arcade USE_SD_HIGHSCORES="));
  out.print(USE_SD_HIGHSCORES);
  out.print(F(" sd_ready="));
  out.print(highScoreStoreReady_ ? 1 : 0);
  out.print(F(" display_ready="));
  out.println(displayReady_ ? 1 : 0);
}

void ArcadeEngine::startGame(GameId game) {
  activeGame_ = game;
  if (game == kPong) {
    resetPong();
  } else if (game == kSnake) {
    resetSnake();
  } else if (game == kTwenty48) {
    resetTwenty48();
  } else {
    showMenu();
  }
  Logger::info("arcade", String("start ") + gameName(activeGame_));
  markDirty();
}

void ArcadeEngine::showMenu() {
  activeGame_ = kMenu;
  markDirty();
}

void ArcadeEngine::markDirty() {
  dirty_ = true;
}

void ArcadeEngine::pollTouch() {
  touch_.pressed = false;
  touch_.released = false;
  touch_.swiped = false;
  touch_.swipe = kDirNone;

#if USE_DISPLAY
  int16_t rawX = 0;
  int16_t rawY = 0;
  bool down = displayReady_ && CrowDisplay::touchPoint(rawX, rawY);
  if (down) {
    touch_.rawX = rawX;
    touch_.rawY = rawY;
    touch_.x = calibrateX(rawX, rawY);
    touch_.y = calibrateY(rawX, rawY);
    touch_.lastX = touch_.x;
    touch_.lastY = touch_.y;
  }

  touch_.pressed = down && !touch_.down;
  touch_.released = !down && touch_.down;
  if (touch_.pressed) {
    touch_.startX = touch_.x;
    touch_.startY = touch_.y;
    Logger::info("touch", String("raw=") + String(rawX) + "," + String(rawY) +
                            " mapped=" + String(touch_.x) + "," + String(touch_.y));
  }

  if (touch_.released) {
    int16_t dx = touch_.lastX - touch_.startX;
    int16_t dy = touch_.lastY - touch_.startY;
    if (abs(dx) > kTouchSwipePx || abs(dy) > kTouchSwipePx) {
      touch_.swiped = true;
      if (abs(dx) > abs(dy)) {
        touch_.swipe = dx > 0 ? kDirRight : kDirLeft;
      } else {
        touch_.swipe = dy > 0 ? kDirDown : kDirUp;
      }
    }
  }

  touch_.down = down;
#else
  touch_.down = false;
#endif
}

void ArcadeEngine::handleTouch() {
  if (!displayReady_) return;
  if (activeGame_ == kMenu) {
    handleMenuTouch();
    return;
  }
  handleGameButtons();
  if (activeGame_ == kPong) {
    handlePongTouch();
  } else {
    handleSwipeTouch();
  }
}

void ArcadeEngine::handleMenuTouch() {
  if (!touch_.pressed) return;
  if (hit(36, 138, 292, 260)) {
    startGame(kPong);
  } else if (hit(366, 138, 292, 260)) {
    startGame(kSnake);
  } else if (hit(696, 138, 292, 260)) {
    startGame(kTwenty48);
  }
}

void ArcadeEngine::handleGameButtons() {
  if (!touch_.pressed) return;
  if (hit(16, 16, 120, 44)) {
    showMenu();
  } else if (hit(862, 16, 146, 44)) {
    startGame(activeGame_);
  }
}

void ArcadeEngine::handlePongTouch() {
  if (!touch_.down) return;
  if (!hit(kPongX, kPongY, kPongW, kPongH)) return;
  pong_.playerY = clampInt(touch_.y - kPaddleH / 2, kPongY, kPongY + kPongH - kPaddleH);
  markDirty();
}

void ArcadeEngine::handleSwipeTouch() {
  if (!touch_.released || !touch_.swiped) return;
  if (activeGame_ == kSnake) {
    setSnakeDirection(touch_.swipe);
  } else if (activeGame_ == kTwenty48) {
    moveTwenty48(touch_.swipe);
  }
}

void ArcadeEngine::resetPong() {
  pong_.playerY = kPongY + (kPongH - kPaddleH) / 2;
  pong_.cpuY = pong_.playerY;
  pong_.ballX = kPongX + kPongW / 2;
  pong_.ballY = kPongY + kPongH / 2;
  pong_.ballVX = random(2) == 0 ? -7.0f : 7.0f;
  pong_.ballVY = random(2) == 0 ? -4.0f : 4.0f;
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

  if (pong_.ballY <= kPongY || pong_.ballY >= kPongY + kPongH - kBallSize) {
    pong_.ballVY = -pong_.ballVY;
  }

  int16_t cpuCenter = pong_.cpuY + kPaddleH / 2;
  if (pong_.ballY > cpuCenter + 8) {
    pong_.cpuY += 5;
  } else if (pong_.ballY < cpuCenter - 8) {
    pong_.cpuY -= 5;
  }
  pong_.cpuY = clampInt(pong_.cpuY, kPongY, kPongY + kPongH - kPaddleH);

  bool hitPlayer = pong_.ballX <= kPongX + 42 &&
                   pong_.ballX >= kPongX + 26 &&
                   pong_.ballY + kBallSize >= pong_.playerY &&
                   pong_.ballY <= pong_.playerY + kPaddleH;
  bool hitCpu = pong_.ballX + kBallSize >= kPongX + kPongW - 42 &&
                pong_.ballX + kBallSize <= kPongX + kPongW - 26 &&
                pong_.ballY + kBallSize >= pong_.cpuY &&
                pong_.ballY <= pong_.cpuY + kPaddleH;

  if (hitPlayer) {
    float offset = (pong_.ballY + kBallSize / 2.0f) - (pong_.playerY + kPaddleH / 2.0f);
    pong_.ballVX = (pong_.ballVX < 0 ? -pong_.ballVX : pong_.ballVX) + 0.25f;
    pong_.ballVY = offset / 12.0f;
  } else if (hitCpu) {
    float offset = (pong_.ballY + kBallSize / 2.0f) - (pong_.cpuY + kPaddleH / 2.0f);
    pong_.ballVX = -((pong_.ballVX < 0 ? -pong_.ballVX : pong_.ballVX) + 0.25f);
    pong_.ballVY = offset / 12.0f;
  }

  if (pong_.ballX < kPongX - 24) {
    pong_.cpuScore++;
    pong_.ballX = kPongX + kPongW / 2;
    pong_.ballY = kPongY + kPongH / 2;
    pong_.ballVX = 7.0f;
    pong_.ballVY = random(2) == 0 ? -4.0f : 4.0f;
  } else if (pong_.ballX > kPongX + kPongW + 8) {
    pong_.playerScore++;
    recordHighScore(kPong, pong_.playerScore);
    pong_.ballX = kPongX + kPongW / 2;
    pong_.ballY = kPongY + kPongH / 2;
    pong_.ballVX = -7.0f;
    pong_.ballVY = random(2) == 0 ? -4.0f : 4.0f;
  }

  markDirty();
}

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

void ArcadeEngine::render() {
  if (!displayReady_ || !dirty_) return;
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  if (activeGame_ == kMenu) {
    renderMenu();
  } else if (activeGame_ == kPong) {
    renderPong();
  } else if (activeGame_ == kSnake) {
    renderSnake();
  } else if (activeGame_ == kTwenty48) {
    renderTwenty48();
  }
  lastRenderMs_ = millis();
  dirty_ = false;
#else
  dirty_ = false;
#endif
}

void ArcadeEngine::renderMenu() {
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  Arduino_GFX *g = gfx();
  if (!g) return;
  g->fillScreen(kBg);
  g->fillRect(0, 0, kScreenW, kHeaderH, kSurface);
  textAt(24, 18, "CYPHER GAMER ARCADE", 3, kText);
  textAt(24, 50, "touch-first offline demos", 1, kMuted);

  const char *names[3] = {"PONG", "SNAKE", "2048"};
  const char *copy[3] = {"drag the paddle", "swipe to turn", "swipe to merge"};
  uint16_t colors[3] = {kCyan, kGreen, kAmber};
  int16_t xs[3] = {36, 366, 696};
  for (uint8_t i = 0; i < 3; i++) {
    panel(xs[i], 138, 292, 260, kSurface, colors[i]);
    g->fillRect(xs[i], 138, 292, 8, colors[i]);
    textAt(xs[i] + 22, 174, names[i], 4, kText);
    textAt(xs[i] + 24, 228, copy[i], 2, kMuted);
    textAt(xs[i] + 24, 292, String("HIGH ") + highScores_[i], 2, colors[i]);
    textAt(xs[i] + 24, 348, "tap to play", 2, kText);
  }

  panel(36, 430, 952, 108, kSurface, kLine);
  textAt(58, 456, "Serial smoke path: catalog | play pong | move up | step | score | cal", 2, kText);
  textAt(58, 492, "Proof: compile-ready until flashed; touch/high-score persistence need hardware verification.", 2, kMuted);
  g->fillRect(0, 562, kScreenW, kFooterH, kSurface);
  textAt(20, 574, USE_SD_HIGHSCORES ? "SD high scores flag enabled" : "RAM high scores; SD flag off", 2, kMuted);
#endif
}

void ArcadeEngine::renderPong() {
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  Arduino_GFX *g = gfx();
  if (!g) return;
  g->fillScreen(kBg);
  button(16, 16, 120, 44, "MENU", kSurfaceHi);
  button(862, 16, 146, 44, "RESTART", kSurfaceHi);
  textAt(174, 20, "PONG", 3, kText);
  textAt(326, 24, String("YOU ") + String((int)pong_.playerScore) +
                       "  CPU " + String((int)pong_.cpuScore), 2, kMuted);
  textAt(594, 24, String("HIGH ") + highScores_[0], 2, kCyan);

  panel(kPongX, kPongY, kPongW, kPongH, 0x0000, kLine);
  for (int16_t y = kPongY + 10; y < kPongY + kPongH - 10; y += 34) {
    g->fillRect(kPongX + kPongW / 2 - 2, y, 4, 18, kSurfaceHi);
  }
  g->fillRect(kPongX + 26, pong_.playerY, kPaddleW, kPaddleH, kCyan);
  g->fillRect(kPongX + kPongW - 42, pong_.cpuY, kPaddleW, kPaddleH, kAmber);
  g->fillRect((int16_t)pong_.ballX, (int16_t)pong_.ballY, kBallSize, kBallSize, kText);
  g->fillRect(0, 562, kScreenW, kFooterH, kSurface);
  textAt(20, 574, "Drag anywhere in the playfield to move the left paddle.", 2, kMuted);
#endif
}

void ArcadeEngine::renderSnake() {
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  Arduino_GFX *g = gfx();
  if (!g) return;
  g->fillScreen(kBg);
  button(16, 16, 120, 44, "MENU", kSurfaceHi);
  button(862, 16, 146, 44, "RESTART", kSurfaceHi);
  textAt(174, 20, "SNAKE", 3, kText);
  textAt(336, 24, String("SCORE ") + snake_.score, 2, kMuted);
  textAt(544, 24, String("HIGH ") + highScores_[1], 2, kGreen);

  panel(kSnakeBoardX, kSnakeBoardY, kSnakeBoardW, kSnakeBoardH, 0x0000, kLine);
  for (uint8_t y = 0; y < kSnakeRows; y++) {
    for (uint8_t x = 0; x < kSnakeCols; x++) {
      if (((x + y) & 1) == 0) {
        g->fillRect(kSnakeBoardX + x * kSnakeCell, kSnakeBoardY + y * kSnakeCell,
                    kSnakeCell, kSnakeCell, 0x0821);
      }
    }
  }
  g->fillRect(kSnakeBoardX + snake_.foodX * kSnakeCell + 4,
              kSnakeBoardY + snake_.foodY * kSnakeCell + 4,
              kSnakeCell - 8, kSnakeCell - 8, kRed);
  for (uint16_t i = 0; i < snake_.length; i++) {
    uint16_t color = i == 0 ? kCyan : kGreen;
    g->fillRect(kSnakeBoardX + snake_.x[i] * kSnakeCell + 3,
                kSnakeBoardY + snake_.y[i] * kSnakeCell + 3,
                kSnakeCell - 6, kSnakeCell - 6, color);
  }

  panel(690, 138, 286, 282, kSurface, kLine);
  textAt(718, 170, "CONTROL", 3, kText);
  textAt(718, 222, "Swipe anywhere", 2, kMuted);
  textAt(718, 256, "to turn.", 2, kMuted);
  textAt(718, 320, snake_.gameOver ? "GAME OVER" : "CHASE FOOD", 3, snake_.gameOver ? kRed : kGreen);
  g->fillRect(0, 562, kScreenW, kFooterH, kSurface);
  textAt(20, 574, "Serial smoke: move up/down/left/right, step, reset.", 2, kMuted);
#endif
}

void ArcadeEngine::renderTwenty48() {
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  Arduino_GFX *g = gfx();
  if (!g) return;
  g->fillScreen(kBg);
  button(16, 16, 120, 44, "MENU", kSurfaceHi);
  button(862, 16, 146, 44, "RESTART", kSurfaceHi);
  textAt(174, 20, "2048", 3, kText);
  textAt(336, 24, String("SCORE ") + twenty_.score, 2, kMuted);
  textAt(544, 24, String("HIGH ") + highScores_[2], 2, kAmber);

  panel(kTwentyBoardX - 16, kTwentyBoardY - 16,
        4 * kTwentyCell + 5 * kTwentyGap,
        4 * kTwentyCell + 5 * kTwentyGap, kSurface, kLine);
  for (uint8_t y = 0; y < 4; y++) {
    for (uint8_t x = 0; x < 4; x++) {
      uint16_t value = twenty_.cells[y][x];
      int16_t tileX = kTwentyBoardX + x * (kTwentyCell + kTwentyGap);
      int16_t tileY = kTwentyBoardY + y * (kTwentyCell + kTwentyGap);
      g->fillRect(tileX, tileY, kTwentyCell, kTwentyCell, value ? tileColor(value) : kSurfaceHi);
      g->drawRect(tileX, tileY, kTwentyCell, kTwentyCell, kLine);
      if (value != 0) {
        uint8_t size = value < 100 ? 4 : (value < 1000 ? 3 : 2);
        int16_t tx = tileX + (value < 10 ? 34 : (value < 100 ? 22 : (value < 1000 ? 18 : 14)));
        int16_t ty = tileY + 32;
        textAt(tx, ty, String(value), size, value <= 4 ? 0x2104 : 0x0000);
      }
    }
  }

  panel(46, 150, 194, 220, kSurface, kLine);
  textAt(72, 184, "SWIPE", 3, kText);
  textAt(72, 236, "Merge equal", 2, kMuted);
  textAt(72, 270, "tiles until", 2, kMuted);
  textAt(72, 304, "2048.", 2, kAmber);
  if (twenty_.gameOver) {
    textAt(72, 346, "GAME OVER", 2, kRed);
  }
  g->fillRect(0, 562, kScreenW, kFooterH, kSurface);
  textAt(20, 574, "Serial smoke: move left/right/up/down, reset, score.", 2, kMuted);
#endif
}

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

int16_t ArcadeEngine::calibrateX(int16_t rawX, int16_t rawY) const {
  int16_t source = ARCADE_TOUCH_SWAP_XY ? rawY : rawX;
  int16_t value = mapAxis(source, ARCADE_TOUCH_MIN_X, ARCADE_TOUCH_MAX_X, kScreenW - 1);
  if (ARCADE_TOUCH_INVERT_X) value = kScreenW - 1 - value;
  return value;
}

int16_t ArcadeEngine::calibrateY(int16_t rawX, int16_t rawY) const {
  int16_t source = ARCADE_TOUCH_SWAP_XY ? rawX : rawY;
  int16_t value = mapAxis(source, ARCADE_TOUCH_MIN_Y, ARCADE_TOUCH_MAX_Y, kScreenH - 1);
  if (ARCADE_TOUCH_INVERT_Y) value = kScreenH - 1 - value;
  return value;
}

bool ArcadeEngine::hit(int16_t x, int16_t y, int16_t w, int16_t h) const {
  return touch_.x >= x && touch_.x <= x + w && touch_.y >= y && touch_.y <= y + h;
}
