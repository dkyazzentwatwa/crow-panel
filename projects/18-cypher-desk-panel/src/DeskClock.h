#ifndef CYPHER_DESK_PANEL_CLOCK_H
#define CYPHER_DESK_PANEL_CLOCK_H

#include "../config/ProjectConfig.h"
#include "DeskSettings.h"

class DeskWifiService;

enum DeskDateSource {
  kDeskDateSaved,
  kDeskDateNtp
};

class DeskClock {
 public:
  void begin(DeskSettings *settings, DeskWifiService *wifi = nullptr);
  void tick();
  void requestSync();
  String isoDate() const;
  String prettyDate() const;
  String sourceLabel() const;
  String status() const;
  DeskDateSource source() const;
  bool synced() const;
  void previousDay();
  void nextDay();
  void confirmDate();

 private:
  DeskSettings *settings_ = nullptr;
  DeskWifiService *wifi_ = nullptr;
  String date_ = "2026-07-13";
  String status_ = "saved date";
  DeskDateSource source_ = kDeskDateSaved;
  bool syncRequested_ = false;
  bool ntpConfigured_ = false;
  uint32_t syncStartedMs_ = 0;
  uint32_t lastPollMs_ = 0;
  void adjustDay(int delta);
  void acceptSystemTime();
};

#endif
