#ifndef CYPHER_FLOCK_DETECTION_STORE_H
#define CYPHER_FLOCK_DETECTION_STORE_H

#include "FlockTypes.h"

class FlockDetectionStore {
 public:
  void clear();
  int16_t upsert(const FlockDetection &incoming, bool &isNew, bool &rediscovered);
  bool restore(const FlockDetection &d);
  uint16_t count() const { return count_; }
  const FlockDetection *at(uint16_t index) const;
  FlockDetection *at(uint16_t index);
  uint16_t protocolCount(FlockFilter filter) const;
  int16_t newestIndex(FlockFilter filter, uint8_t ordinal = 0) const;
  int16_t newestScopeIndex(FlockFilter filter, uint8_t ordinal = 0) const;
  int16_t sortedIndex(FlockFilter filter, FlockSort sort, uint8_t ordinal = 0) const;
  uint32_t droppedFull() const { return droppedFull_; }
  const uint16_t *activity() const { return activity_; }
  uint8_t activityHead() const { return activityHead_; }
  void tickActivity();

 private:
  bool ensureAllocated_();
  int16_t findMac_(const char *mac) const;
  int16_t findIdentity_(const char *identity) const;
  bool matches_(const FlockDetection &d, FlockFilter filter) const;

  FlockDetection *detections_ = nullptr;
  uint16_t count_ = 0;
  uint32_t droppedFull_ = 0;
  uint16_t activity_[30] = {0};
  uint8_t activityHead_ = 0;
  uint32_t lastActivityTick_ = 0;
};

#endif
