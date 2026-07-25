#ifndef CYPHER_FLOCK_SESSION_STORE_H
#define CYPHER_FLOCK_SESSION_STORE_H

#include <ArduinoJson.h>
#include "FlockDetectionStore.h"

class FlockSessionStore {
 public:
  bool begin(FlockDetectionStore &previous, FlockLifetimeStats &lifetime, String &status);
  bool save(const FlockDetectionStore &current, const FlockLifetimeStats &lifetime, String &status);
  bool clearCurrent(String &status);
  bool appendCalibration(JsonObjectConst observation, String &status);
  bool exportCalibration(Stream &output, String &status) const;
  bool clearCalibration(String &status);
  bool ready() const { return ready_; }
  bool previousReady() const { return previousReady_; }
  uint32_t lastSaveMs() const { return lastSaveMs_; }

 private:
  uint32_t crc32_(const uint8_t *data, size_t len) const;
  bool validate_(const char *path, String *payload = nullptr) const;
  bool copy_(const char *source, const char *destination) const;
  bool load_(const char *path, FlockDetectionStore &store) const;
  bool buildPayload_(const FlockDetectionStore &store, String &payload) const;

  bool ready_ = false;
  bool previousReady_ = false;
  uint32_t lastSaveMs_ = 0;
  uint16_t calibrationCount_ = 0;
};

#endif
