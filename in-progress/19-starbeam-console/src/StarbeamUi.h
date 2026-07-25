#ifndef STARBEAM_CONSOLE_UI_H
#define STARBEAM_CONSOLE_UI_H

#include <Arduino.h>
#include "../config/ProjectConfig.h"
#include "StarbeamTypes.h"

// Modern touch-first dashboard for the 1024x600 CrowPanel DSI panel, built on
// the shared CrowDisplay bring-up + Widgets toolkit (dark "ops" palette,
// FreeSans fonts, cards/gauges/sparklines). Replaces Starbeam's 3-button
// 128x64 menu with a categorised console:
//   HOME -> CATEGORY grid -> OPERATION screen, with a legal-ack modal gating
//   every transmit/attack action, and a persistent status bar (7 radio dots,
//   co-processor link pill, global STOP).
//
// tick() reads touch and returns an action to LAUNCH (ACT_NONE most frames).
// The .ino executes it and updates StarbeamState; the UI renders that state.

enum StarbeamScreen : uint8_t {
  SCR_HOME = 0,
  SCR_CATEGORY,
  SCR_OPERATION,
  SCR_LEGAL,
};

class StarbeamUi {
 public:
  bool begin();
  void setBanner(const char *text);

  // Returns an action the user launched this frame, else ACT_NONE.
  // ACT_STOP_ALL is returned when the on-screen STOP is tapped.
  StarbeamAction tick(StarbeamState &st);

  void showOperation(StarbeamAction a);   // engine calls this on launch
  void toHome();

 private:
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  StarbeamAction handleTouch_(StarbeamState &st);
  void draw_(StarbeamState &st);
  void drawHeader_(StarbeamState &st);
  void drawHome_();
  void drawCategory_(StarbeamState &st);
  void drawOperation_(StarbeamState &st);
  void drawLegal_();
  void drawSpectrum_(StarbeamState &st, int x, int y, int w, int h);
  void drawHeatmap_(StarbeamState &st, int x, int y, int w, int h);
  bool stopHit_(int x, int y);

  bool ready_ = false;
  bool dirty_ = true;
  bool wasTouched_ = false;
  uint32_t lastTouchMs_ = 0;
  uint32_t lastDrawMs_ = 0;
#endif
  StarbeamScreen screen_ = SCR_HOME;
  StarbeamCategory category_ = CAT_JAMMERS;
  StarbeamAction pending_ = ACT_NONE;   // action awaiting legal ack
};

#endif  // STARBEAM_CONSOLE_UI_H
