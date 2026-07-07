#ifndef ADSB_MOCK_ADSB_SOURCE_H
#define ADSB_MOCK_ADSB_SOURCE_H

#include <Arduino.h>
#include <CrowPanelShared.h>
#include "AdsbTypes.h"
#include "AircraftStore.h"

// Stand-in for the live feed when USE_WIFI=0: synthesizes a handful of aircraft
// that drift around the home position, so the radar + list are fully alive on
// the bench with no network. Feeds the SAME AircraftStore the live client uses,
// so the render side is byte-identical in mock vs live. Mirrors project 04's
// MockSensorSource. Library-free (compiles in the baseline build).
class MockAdsbSource {
 public:
  void begin(double homeLat, double homeLon, float rangeKm);

  // At cadence: advance every aircraft, upsert into the store, and commit.
  // Returns true on the ticks where it refreshed the store.
  bool tick(AircraftStore &store);

 private:
  static const uint8_t kCount = 8;

  struct MockPlane {
    double lat;
    double lon;
    int32_t altFt;
    bool haveAlt;
    float trackDeg;
    float speedKt;
    char icao[7];
    char callsign[9];
    char type[5];
  };

  void respawn_(MockPlane &p);

  MockPlane planes_[kCount];
  double homeLat_ = 0;
  double homeLon_ = 0;
  float rangeKm_ = 100.0f;
  uint32_t lastMs_ = 0;
  Throttle cadence_{1000};
};

#endif
