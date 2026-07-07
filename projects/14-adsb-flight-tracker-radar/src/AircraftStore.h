#ifndef ADSB_AIRCRAFT_STORE_H
#define ADSB_AIRCRAFT_STORE_H

#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "AdsbTypes.h"

// Mutex-protected set of the current aircraft, keyed by ICAO hex. The producer
// is either the mock source (in loop()) or the live ADS-B poller (a background
// FreeRTOS task on core 0); the renderer consumes a sorted snapshot each frame.
// The mutex is the single sync point - the producer may do slow HTTP work, so a
// mutex (not a spinlock) is correct.
//
// FreeRTOS ships with the ESP32 core, so this whole class compiles in the
// baseline (no-library) build; nothing here is gated on USE_WIFI/USE_DISPLAY.
class AircraftStore {
 public:
  void begin();

  // Insert or update one aircraft (distance/bearing already derived). When the
  // table is full, a newcomer replaces the current farthest entry only if it is
  // nearer - so the store always keeps the kMaxContacts nearest aircraft.
  void upsert(const Aircraft &a);

  // Mark the end of a fetch batch: records the data source and the raw count
  // seen before the nearest-N cap (for the header's "N contacts").
  void commit(const char *source, uint16_t totalSeen);

  // Copy a snapshot out: ages out stale entries under the lock, then (outside
  // the lock) sorts the copy by distance. rangeRingKm is left 0 for the caller.
  void copySnapshot(AdsbSnapshot &out);

  uint8_t count() const { return count_; }

 private:
  int findByIcao_(const char *icao) const;
  int farthestIndex_() const;

  static const uint32_t kStaleMs = 45000;  // drop aircraft unseen for 45 s

  Aircraft items_[kMaxContacts];
  uint8_t count_ = 0;
  uint16_t totalSeen_ = 0;
  const char *source_ = "no-wifi";
  uint32_t generatedMs_ = 0;
  SemaphoreHandle_t mutex_ = nullptr;
};

#endif
