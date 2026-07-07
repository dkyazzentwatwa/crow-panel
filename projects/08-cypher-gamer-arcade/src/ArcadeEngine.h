#ifndef CYPHER_GAMER_ARCADE_ENGINE_H
#define CYPHER_GAMER_ARCADE_ENGINE_H

#include "../config/ProjectConfig.h"
#include <Arduino.h>

class ArcadeEngine {
 public:
  void begin();
  void tick();

  void showCatalog(Print &out);
  void play(const String &name, Print &out);
  void move(const String &direction, Print &out);
  void step(Print &out);
  void reset(Print &out);
  void printScore(Print &out) const;
  void printCalibration(Print &out) const;
  void printFlags(Print &out) const;

 private:
  enum GameId {
    kMenu = 0,
    kPong,
    kSnake,
    kTwenty48
  };

  enum Direction {
    kDirNone = 0,
    kDirUp,
    kDirDown,
    kDirLeft,
    kDirRight
  };

  enum {
    kSnakeCols = 22,
    kSnakeRows = 16,
    kSnakeMax = kSnakeCols * kSnakeRows
  };

  struct TouchState {
    bool down = false;
    bool pressed = false;
    bool released = false;
    bool swiped = false;
    int16_t rawX = 0;
    int16_t rawY = 0;
    int16_t x = 0;
    int16_t y = 0;
    int16_t startX = 0;
    int16_t startY = 0;
    int16_t lastX = 0;
    int16_t lastY = 0;
    Direction swipe = kDirNone;
  };

  struct PongState {
    int16_t playerY = 0;
    int16_t cpuY = 0;
    float ballX = 0;
    float ballY = 0;
    float ballVX = 0;
    float ballVY = 0;
    uint8_t playerScore = 0;
    uint8_t cpuScore = 0;
    uint32_t lastStepMs = 0;
  };

  struct SnakeState {
    uint8_t x[kSnakeMax];
    uint8_t y[kSnakeMax];
    uint16_t length = 0;
    uint8_t foodX = 0;
    uint8_t foodY = 0;
    Direction dir = kDirRight;
    Direction nextDir = kDirRight;
    uint16_t score = 0;
    bool gameOver = false;
    uint32_t lastStepMs = 0;
  };

  struct Twenty48State {
    uint16_t cells[4][4];
    uint16_t score = 0;
    bool gameOver = false;
  };

  void startGame(GameId game);
  void showMenu();
  void markDirty();

  void pollTouch();
  void handleTouch();
  void handleMenuTouch();
  void handleGameButtons();
  void handlePongTouch();
  void handleSwipeTouch();

  void resetPong();
  void updatePong(bool force);
  void resetSnake();
  void updateSnake(bool force);
  void placeSnakeFood();
  bool snakeUses(uint8_t x, uint8_t y) const;
  void setSnakeDirection(Direction dir);
  void resetTwenty48();
  bool moveTwenty48(Direction dir);
  bool canMoveTwenty48() const;
  void spawnTwenty48Tile();

  void render();
  void renderMenu();
  void renderPong();
  void renderSnake();
  void renderTwenty48();

  Direction parseDirection(const String &direction) const;
  GameId parseGame(const String &name) const;
  const char *gameName(GameId game) const;
  uint16_t currentScore() const;
  uint8_t scoreIndex(GameId game) const;
  void recordHighScore(GameId game, uint16_t score);
  void loadHighScores();
  void saveHighScores();

  int16_t calibrateX(int16_t rawX, int16_t rawY) const;
  int16_t calibrateY(int16_t rawX, int16_t rawY) const;
  bool hit(int16_t x, int16_t y, int16_t w, int16_t h) const;

  GameId activeGame_ = kMenu;
  TouchState touch_;
  PongState pong_;
  SnakeState snake_;
  Twenty48State twenty_;
  uint16_t highScores_[3] = {0, 0, 0};
  bool displayReady_ = false;
  bool dirty_ = true;
  bool highScoreStoreReady_ = false;
  uint32_t lastRenderMs_ = 0;
};

#endif
