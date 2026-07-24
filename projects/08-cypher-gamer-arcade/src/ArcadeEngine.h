#ifndef CYPHER_GAMER_ARCADE_ENGINE_H
#define CYPHER_GAMER_ARCADE_ENGINE_H

#include "../config/ProjectConfig.h"
#include <Arduino.h>
#include <CrowPanelShared.h>  // CrowTouch, CrowDisplay, Widgets::, Logger

// Forward-declared so the header compiles on headless (USE_DISPLAY=0) builds
// where the Arduino_GFX classes are not included. The animated Pong playfield
// is composited into an offscreen Arduino_Canvas held in internal SRAM and
// blitted once per frame (the DSI panel is single-framebuffer, so animating it
// in place tears). Only ever dereferenced under the USE_DISPLAY + P4 guard.
class Arduino_Canvas;

// Cypher Gamer Arcade engine: owns the three game engines (Pong, Snake, 2048),
// their high scores, all touch handling (via the shared CrowTouch helper), and
// all rendering (via the shared Widgets:: toolkit and the dark ops palette).
//
// tick() reads touch, runs the active game, repaints when state changed, and
// returns a typed UiEvent for the discrete, loggable transitions. The .ino
// executes each event through the SAME methods the serial commands call, so
// touch and serial stay in perfect parity. Navigation between the catalog,
// scores, and play screens plus the in-game pause overlay is driven from here.
class ArcadeEngine {
 public:
  // Which top-level screen is showing. The pause overlay is a modal drawn on
  // top of kScreenPlay (tracked separately by paused_).
  enum Screen : uint8_t {
    kScreenCatalog = 0,  // game launcher grid
    kScreenScores,       // per-game high-score cards
    kScreenPlay          // the active game
  };

  // Discrete action a touch produced this frame (kEvNone most frames). The
  // .ino turns each into the matching engine call + event-log entry.
  enum UiEvent : uint8_t {
    kEvNone = 0,
    kEvLaunchPong,     // -> play("pong")
    kEvLaunchSnake,    // -> play("snake")
    kEvLaunch2048,     // -> play("2048")
    kEvRestart,        // -> reset()  (restart the active game)
    kEvQuitToCatalog,  // -> showCatalog()
    kEvShowScores      // -> showScores()
  };

  void begin();
  UiEvent tick();

  // Serial command surface (unchanged behavior; drives the same state as touch).
  void showCatalog(Print &out);
  void showScores();
  void play(const String &name, Print &out);
  void move(const String &direction, Print &out);
  void step(Print &out);
  void reset(Print &out);
  void printScore(Print &out) const;
  void printCalibration(Print &out) const;   // `cal`
  void printTouchDiag(Print &out) const;      // `touch`
  void printFlags(Print &out) const;
  bool runSelfTest(Print &out);               // `selftest`

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

  struct PongState {
    int16_t playerY = 0;   // field-local, 0..kPongFieldH-kPongPaddleH
    int16_t cpuY = 0;
    float ballX = 0;       // field-local
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

  // Navigation / lifecycle.
  void startGame(GameId game);
  void markDirty();
  const char *screenName() const;

  // Touch: one pass per tick that updates navigation, applies gameplay input,
  // and returns any discrete UiEvent the release produced.
  UiEvent handleTouch();
  UiEvent handleCatalogTouch();
  UiEvent handleScoresTouch();
  UiEvent handlePlayTouch();
  UiEvent handlePauseTouch();
  void applyPaddleDrag();
  void applySwipe(int16_t dx, int16_t dy);

  // Game logic (unchanged feel; kept from the original engine).
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
  void applyMove(Direction dir);

  // Rendering (all Widgets::; guarded paths defined only for USE_DISPLAY + P4).
  void render();
  void renderCatalog();
  void renderScores();
  void renderPlayStatic();
  void renderPongStatic();
  void renderPongField();   // composites the offscreen canvas + blits it
  void renderSnake();
  void renderTwenty48();
  void renderPauseOverlay();
  void blitPongField();
  bool ensurePongCanvas();
  void releasePongCanvas();

  // Helpers.
  Direction parseDirection(const String &direction) const;
  GameId parseGame(const String &name) const;
  const char *gameName(GameId game) const;
  const char *gameTitle(GameId game) const;
  uint16_t currentScore() const;
  uint8_t scoreIndex(GameId game) const;
  void recordHighScore(GameId game, uint16_t score);
  void loadHighScores();
  void saveHighScores();

  GameId activeGame_ = kMenu;
  Screen screen_ = kScreenCatalog;
  bool paused_ = false;

  CrowTouch touch_;
  bool pressOnControl_ = false;  // the press started on an interactive control
  int16_t pressX_ = 0;
  int16_t pressY_ = 0;

  PongState pong_;
  SnakeState snake_;
  Twenty48State twenty_;
  uint16_t highScores_[3] = {0, 0, 0};

  bool displayReady_ = false;
  bool dirty_ = true;
  bool highScoreStoreReady_ = false;
  uint32_t lastPongDrawMs_ = 0;

  Arduino_Canvas *pongCanvas_ = nullptr;
  bool pongCanvasInternal_ = false;
};

#endif
