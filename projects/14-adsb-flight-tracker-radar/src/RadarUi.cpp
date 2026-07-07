#include "RadarUi.h"

void RadarUi::begin(uint16_t initialRangeKm) {
  dashboard_.begin(initialRangeKm);
  Logger::info("radar", "dashboard ready (range " + String(initialRangeKm) + " km)");
}

void RadarUi::tick(AircraftStore &store) {
  dashboard_.tick(store);
  // Cheap heartbeat for headless / on-camera runs (count is a benign racy read).
  if (heartbeat_.ready()) {
    int c = (int)store.count();
    if (c != lastCount_) {
      lastCount_ = c;
      Logger::info("radar", String(c) + " aircraft tracked");
    }
  }
}

void RadarUi::setRangeKm(uint16_t km) { dashboard_.setRangeKm(km); }
uint16_t RadarUi::rangeKm() const { return dashboard_.rangeKm(); }
void RadarUi::setWorldFeeds(const WorldFeeds &feeds) { dashboard_.setWorldFeeds(feeds); }
void RadarUi::nextScreen() { dashboard_.nextScreen(); }
bool RadarUi::setScreen(const String &name) { return dashboard_.setScreen(name); }
const char *RadarUi::screenName() const { return dashboard_.screenName(); }
