#include "AircraftStore.h"

#include <string.h>

void AircraftStore::begin() {
  if (mutex_ == nullptr) {
    mutex_ = xSemaphoreCreateMutex();
  }
}

int AircraftStore::findByIcao_(const char *icao) const {
  for (uint8_t i = 0; i < count_; i++) {
    if (strncmp(items_[i].icao, icao, sizeof(items_[i].icao)) == 0) {
      return (int)i;
    }
  }
  return -1;
}

int AircraftStore::farthestIndex_() const {
  if (count_ == 0) return -1;
  uint8_t far = 0;
  for (uint8_t i = 1; i < count_; i++) {
    if (items_[i].distanceKm > items_[far].distanceKm) far = i;
  }
  return (int)far;
}

void AircraftStore::upsert(const Aircraft &a) {
  if (mutex_) xSemaphoreTake(mutex_, portMAX_DELAY);
  int idx = findByIcao_(a.icao);
  if (idx >= 0) {
    items_[idx] = a;
  } else if (count_ < kMaxContacts) {
    items_[count_++] = a;
  } else {
    int f = farthestIndex_();
    if (f >= 0 && a.distanceKm < items_[f].distanceKm) items_[f] = a;
  }
  if (mutex_) xSemaphoreGive(mutex_);
}

void AircraftStore::commit(const char *source, uint16_t totalSeen) {
  if (mutex_) xSemaphoreTake(mutex_, portMAX_DELAY);
  source_ = source;
  totalSeen_ = totalSeen;
  generatedMs_ = millis();
  if (mutex_) xSemaphoreGive(mutex_);
}

void AircraftStore::copySnapshot(AdsbSnapshot &out) {
  if (mutex_) xSemaphoreTake(mutex_, portMAX_DELAY);

  // Age out stale entries by compacting the array in place.
  uint32_t now = millis();
  uint8_t w = 0;
  for (uint8_t i = 0; i < count_; i++) {
    if ((now - items_[i].lastSeenMs) <= kStaleMs) {
      if (w != i) items_[w] = items_[i];
      w++;
    }
  }
  count_ = w;

  uint8_t n = count_;
  for (uint8_t i = 0; i < n; i++) out.ac[i] = items_[i];
  out.count = n;
  out.totalSeen = totalSeen_;
  out.generatedMs = generatedMs_;
  out.source = source_;

  if (mutex_) xSemaphoreGive(mutex_);

  // Sort the copy by distance (nearest first) outside the lock.
  for (uint8_t i = 1; i < n; i++) {
    Aircraft key = out.ac[i];
    int j = (int)i - 1;
    while (j >= 0 && out.ac[j].distanceKm > key.distanceKm) {
      out.ac[j + 1] = out.ac[j];
      j--;
    }
    out.ac[j + 1] = key;
  }
  out.rangeRingKm = 0;  // caller (dashboard) fills the current display ring
}
