#include "FlockDetectionStore.h"
#if defined(ESP32)
#include <esp_heap_caps.h>
#endif

bool FlockDetectionStore::ensureAllocated_() {
  if (detections_) return true;
#if defined(ESP32)
  detections_ = (FlockDetection *)heap_caps_calloc(FLOCK_MAX_DETECTIONS,
      sizeof(FlockDetection), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!detections_) detections_ = (FlockDetection *)heap_caps_calloc(
      FLOCK_MAX_DETECTIONS, sizeof(FlockDetection), MALLOC_CAP_8BIT);
#else
  detections_ = (FlockDetection *)calloc(FLOCK_MAX_DETECTIONS, sizeof(FlockDetection));
#endif
  return detections_ != nullptr;
}

void FlockDetectionStore::clear() {
  if (ensureAllocated_()) memset(detections_, 0, sizeof(FlockDetection) * FLOCK_MAX_DETECTIONS);
  memset(activity_, 0, sizeof(activity_));
  count_ = 0;
  droppedFull_ = 0;
  activityHead_ = 0;
  lastActivityTick_ = millis();
}

int16_t FlockDetectionStore::findMac_(const char *mac) const {
  if (!mac || !mac[0]) return -1;
  for (uint16_t i = 0; i < count_; ++i) {
    if (strcasecmp(detections_[i].mac, mac) == 0) return (int16_t)i;
  }
  return -1;
}

int16_t FlockDetectionStore::findIdentity_(const char *identity) const {
  if (!identity || strncmp(identity, "Penguin-", 8) != 0 || strlen(identity) != 18) return -1;
  for (uint16_t i = 0; i < count_; ++i) {
    if (strcasecmp(detections_[i].identity, identity) == 0) return (int16_t)i;
  }
  return -1;
}

int16_t FlockDetectionStore::upsert(const FlockDetection &incoming, bool &isNew,
                                    bool &rediscovered) {
  if (!ensureAllocated_()) return -1;
  uint32_t now = millis();
  isNew = false;
  rediscovered = false;
  int16_t index = findMac_(incoming.mac);
  if (index < 0) index = findIdentity_(incoming.identity);
  if (index >= 0) {
    FlockDetection &d = detections_[index];
    rediscovered = (now - d.lastSeen) > FLOCK_REDISCOVER_MS;
    uint16_t oldCount = d.count;
    uint32_t firstSeen = d.firstSeen;
    uint8_t strongest = max(d.confidence, incoming.confidence);
    char strongestLabel[sizeof(d.label)];
    strlcpy(strongestLabel, d.confidence > incoming.confidence ? d.label : incoming.label,
            sizeof(strongestLabel));
    d = incoming;
    d.firstSeen = firstSeen;
    d.lastSeen = now;
    d.count = oldCount == 0xFFFF ? oldCount : oldCount + 1;
    d.confidence = strongest;
    strlcpy(d.label, strongestLabel, sizeof(d.label));
    d.rediscovered = rediscovered;
  } else {
    if (count_ >= FLOCK_MAX_DETECTIONS) {
      int16_t replace = -1;
      if (incoming.alertEligible) {
        for (uint16_t i = 0; i < count_; ++i) {
          if (detections_[i].candidate &&
              (replace < 0 || detections_[i].lastSeen < detections_[replace].lastSeen)) replace = i;
        }
      }
      if (replace < 0) { ++droppedFull_; return -1; }
      index = replace;
    } else {
      index = (int16_t)count_++;
    }
    detections_[index] = incoming;
    detections_[index].firstSeen = now;
    detections_[index].lastSeen = now;
    detections_[index].count = 1;
    detections_[index].rediscovered = false;
    isNew = true;
  }
  if (activity_[activityHead_] < 0xFFFF) ++activity_[activityHead_];
  return index;
}

bool FlockDetectionStore::restore(const FlockDetection &d) {
  if (!ensureAllocated_() || count_ >= FLOCK_MAX_DETECTIONS || d.mac[0] == '\0') return false;
  detections_[count_++] = d;
  return true;
}

const FlockDetection *FlockDetectionStore::at(uint16_t index) const {
  return index < count_ ? &detections_[index] : nullptr;
}

FlockDetection *FlockDetectionStore::at(uint16_t index) {
  return index < count_ ? &detections_[index] : nullptr;
}

bool FlockDetectionStore::matches_(const FlockDetection &d, FlockFilter filter) const {
  if (filter == kFlockFilterAll) return true;
  if (filter == kFlockFilterWifi) return flockIsWifi(d);
  if (filter == kFlockFilterRaven) return flockIsRaven(d);
  if (filter == kFlockFilterCandidate) return d.candidate;
  if (filter == kFlockFilterBw16) return strcmp(d.source, "wifi-bw16") == 0;
  if (filter == kFlockFilterEsp32) return strcmp(d.source, "ble-esp32") == 0;
  return flockIsBle(d) && !flockIsRaven(d);
}

uint16_t FlockDetectionStore::protocolCount(FlockFilter filter) const {
  uint16_t total = 0;
  for (uint16_t i = 0; i < count_; ++i) {
    if (matches_(detections_[i], filter)) ++total;
  }
  return total;
}

int16_t FlockDetectionStore::newestIndex(FlockFilter filter, uint8_t ordinal) const {
  uint8_t found = 0;
  int16_t best = -1;
  uint32_t beforeTime = UINT32_MAX;
  int16_t beforeIndex = INT16_MAX;
  while (found <= ordinal) {
    best = -1;
    uint32_t bestTime = 0;
    for (uint16_t i = 0; i < count_; ++i) {
      const FlockDetection &d = detections_[i];
      bool belowBoundary = d.lastSeen < beforeTime ||
                           (d.lastSeen == beforeTime && (int16_t)i < beforeIndex);
      bool newer = best < 0 || d.lastSeen > bestTime ||
                   (d.lastSeen == bestTime && (int16_t)i > best);
      if (!matches_(d, filter) || !belowBoundary || !newer) continue;
      best = (int16_t)i;
      bestTime = d.lastSeen;
    }
    if (best < 0) return -1;
    beforeTime = detections_[best].lastSeen;
    beforeIndex = best;
    ++found;
  }
  return best;
}

int16_t FlockDetectionStore::newestScopeIndex(FlockFilter filter, uint8_t ordinal) const {
  uint8_t found = 0;
  for (uint8_t rank = 0; rank < count_; ++rank) {
    int16_t index = newestIndex(filter, rank);
    if (index < 0) break;
    const FlockDetection &d = detections_[index];
    if (!d.alertEligible || !d.directRssi) continue;
    if (found++ == ordinal) return index;
  }
  return -1;
}

int16_t FlockDetectionStore::sortedIndex(FlockFilter filter, FlockSort sort, uint8_t ordinal) const {
  if (ordinal >= 16) return -1;
  int16_t chosen[16];
  for (uint8_t i = 0; i < 16; ++i) chosen[i] = -1;
  for (uint8_t pass = 0; pass <= ordinal; ++pass) {
    int16_t best = -1;
    for (uint16_t i = 0; i < count_; ++i) {
      if (!matches_(detections_[i], filter)) continue;
      bool used = false;
      for (uint8_t j = 0; j < pass; ++j) if (chosen[j] == (int16_t)i) used = true;
      if (used) continue;
      if (best < 0) { best = (int16_t)i; continue; }
      const FlockDetection &candidate = detections_[i];
      const FlockDetection &current = detections_[best];
      bool better = sort == kFlockSortSignal ? candidate.rssi > current.rssi :
                    (sort == kFlockSortConfidence ? candidate.confidence > current.confidence :
                     candidate.lastSeen > current.lastSeen);
      if (!better && sort != kFlockSortRecent) {
        int primaryCandidate = sort == kFlockSortSignal ? candidate.rssi : candidate.confidence;
        int primaryCurrent = sort == kFlockSortSignal ? current.rssi : current.confidence;
        better = primaryCandidate == primaryCurrent && candidate.lastSeen > current.lastSeen;
      }
      if (better) best = (int16_t)i;
    }
    if (best < 0) return -1;
    chosen[pass] = best;
  }
  return chosen[ordinal];
}

void FlockDetectionStore::tickActivity() {
  uint32_t now = millis();
  if (lastActivityTick_ == 0) lastActivityTick_ = now;
  while (now - lastActivityTick_ >= 1000) {
    activityHead_ = (uint8_t)((activityHead_ + 1) % 30);
    activity_[activityHead_] = 0;
    lastActivityTick_ += 1000;
  }
}
