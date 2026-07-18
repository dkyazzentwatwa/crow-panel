#ifndef LITEGO_TOUCH_VIEW_H
#define LITEGO_TOUCH_VIEW_H

#include "../config/ProjectConfig.h"
#include <Arduino.h>
#include "LiteGoGame.h"

class LiteGoTouchView {
 public:
  enum ActionType {
    kActionNone,
    kActionPlay,
    kActionPass,
    kActionCpu,
    kActionReset,
    kActionScore,
    kActionHint
  };

  struct Action {
    ActionType type;
    int8_t x;
    int8_t y;
  };

  void begin(LiteGoGame *game);
  void requestRepaint();
  void clearLastMove();
  void setLastResult(const LiteGoGame::MoveResult &result, const char *source);
  void setStatus(const String &message, bool warning = false);
  bool tick(Action &action);

 private:
  LiteGoGame *game_ = nullptr;
  bool dirty_ = true;
  bool wasTouched_ = false;
  bool lastMoveValid_ = false;
  int8_t lastMoveX_ = -1;
  int8_t lastMoveY_ = -1;
  String status_ = "Board ready. Tap an intersection or command pill.";
  bool statusWarning_ = false;

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  const char *lastTouchMap_ = "none";
  unsigned long lastTouchActionMs_ = 0;

  bool mapTouch(int16_t tx, int16_t ty, Action &action) const;
  bool hitButton(int16_t tx, int16_t ty, Action &action) const;
  void draw();
#endif
};

#endif
