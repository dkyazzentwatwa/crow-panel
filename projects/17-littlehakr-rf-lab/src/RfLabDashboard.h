#ifndef LITTLEHAKR_RF_LAB_DASHBOARD_H
#define LITTLEHAKR_RF_LAB_DASHBOARD_H

#include "../config/ProjectConfig.h"
#include "C6RadioMonitor.h"
#include "RfLabTypes.h"

enum RfLabUiAction : uint8_t {
  kRfLabUiNone = 0,
  kRfLabUiProbe,
  kRfLabUiDetectorToggle,
  kRfLabUiSave,
  kRfLabUiClear,
  kRfLabUiProof,
  kRfLabUiWifiScan
};

struct RfLabUiEvent {
  RfLabUiAction action = kRfLabUiNone;
};

class RfLabDashboard {
 public:
  void begin();
  void setBanner(const String &banner);
  bool tick(const RfLabState &lab, const C6RadioSnapshot &c6, bool persistenceReady,
            RfLabUiEvent &event);

 private:
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  void draw_(const RfLabState &lab, const C6RadioSnapshot &c6, bool persistenceReady);
  void drawLab_(const RfLabState &lab, bool persistenceReady);
  void drawWifi_(const C6RadioSnapshot &c6);
  void drawBle_(const C6RadioSnapshot &c6);
  void drawProof_();
  void handleTouch_(RfLabUiEvent &event);
  void text_(int16_t x, int16_t y, uint8_t size, uint16_t color, const String &value);
  void card_(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t border);

  bool ready_ = false;
  bool dirty_ = true;
  bool wasTouched_ = false;
  uint32_t lastTouchMs_ = 0;
  uint32_t lastDrawMs_ = 0;
#endif
  uint8_t page_ = 0;
  String banner_ = "booting";
};

#endif
