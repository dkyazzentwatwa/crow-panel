#ifndef ADSB_RADAR_UI_H
#define ADSB_RADAR_UI_H

#include "../config/ProjectConfig.h"
#include <Arduino.h>
#include <CrowPanelShared.h>
#include <WorldFeed.h>
#include "AircraftStore.h"
#include "RadarDashboard.h"

// Thin wrapper over RadarDashboard, mirroring ControlHubUi: forwards to the
// dashboard (which no-ops with the display off) and logs a one-line contact
// heartbeat to Serial, so the demo reads on camera with no panel attached.
class RadarUi {
 public:
  void begin(uint16_t initialRangeKm);
  void tick(AircraftStore &store);
  void setRangeKm(uint16_t km);
  uint16_t rangeKm() const;
  void setWorldFeeds(const WorldFeeds &feeds);
  void nextScreen();
  bool setScreen(const String &name);
  const char *screenName() const;

 private:
  RadarDashboard dashboard_;
  Throttle heartbeat_{5000};
  int lastCount_ = -1;
};

#endif
