#ifndef LITEGO_TOUCH_VIEW_H
#define LITEGO_TOUCH_VIEW_H

#include "../config/ProjectConfig.h"
#include <Arduino.h>
#include "LiteGoGame.h"

class LiteGoTouchView {
 public:
  void begin(LiteGoGame *game);
  void requestRepaint();
  bool tick(int8_t &x, int8_t &y);

 private:
  LiteGoGame *game_ = nullptr;
  bool dirty_ = true;
  bool wasTouched_ = false;

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  bool mapTouch(int16_t tx, int16_t ty, int8_t &x, int8_t &y) const;
  void draw();
#endif
};

#endif
