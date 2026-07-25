#ifndef ADSB_TYPES_H
#define ADSB_TYPES_H

#include <Arduino.h>

// One canonical set of value types shared by the data side (store, client,
// mock) and the render side (scope, dashboard). Plain PODs with fixed char
// arrays so a snapshot can be memcpy-copied out of the store under a mutex
// with no heap churn.

static const uint8_t kMaxContacts = 60;  // nearest-N kept / plotted

struct Aircraft {
  char icao[7];          // 24-bit ICAO hex address, e.g. "A1B2C3"
  char callsign[9];      // flight/callsign, trimmed ("" if none)
  char type[5];          // ICAO type code, e.g. "B738" ("" if unknown)
  char category[5];      // ADS-B emitter category, e.g. "A3" (fallback label)
  double lat;
  double lon;
  int32_t altFt;         // barometric (or geometric) altitude, ft
  bool haveAlt;          // false when altitude is unknown
  bool onGround;         // alt_baro == "ground"
  float groundSpeedKt;
  float trackDeg;        // true track over ground, deg (0=N)
  bool haveTrack;
  float distanceKm;      // derived: great-circle distance from home
  float bearingDeg;      // derived: initial bearing from home (0=N)
  uint32_t lastSeenMs;   // millis() when last updated
};

// A read-only, distance-sorted copy handed to the renderer each frame.
struct AdsbSnapshot {
  Aircraft ac[kMaxContacts];
  uint8_t count;         // valid entries in ac[]
  uint16_t totalSeen;    // raw aircraft in the last fetch (before the cap)
  uint16_t rangeRingKm;  // outer display ring (set by the dashboard). uint16 so
                         // `range 300` reaches the scope intact - as a uint8 it
                         // wrapped to 44 km and the disc silently disagreed
                         // with the header pill.
  uint32_t generatedMs;  // millis() of the fetch that produced this data
  const char *source;    // "airplanes.live" / "adsb.fi" / "mock" / "no-wifi"
};

#endif
