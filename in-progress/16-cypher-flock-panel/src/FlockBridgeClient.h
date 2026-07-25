#ifndef CYPHER_FLOCK_BRIDGE_CLIENT_H
#define CYPHER_FLOCK_BRIDGE_CLIENT_H

#include <ArduinoJson.h>
#include "FlockTypes.h"

typedef void (*FlockDetectionHandler)(const FlockDetection &d);
typedef void (*FlockEventHandler)(const char *event, const char *message);
typedef void (*FlockCalibrationHandler)(JsonObjectConst observation);

class FlockBridgeClient {
 public:
  void begin(Stream *link, FlockDetectionHandler detectionHandler, FlockEventHandler eventHandler,
             FlockCalibrationHandler calibrationHandler = nullptr);
  void tick();
  bool inject(const String &line);
  bool sendCommand(const String &command);
  const FlockBridgeStatus &status() const { return status_; }
  const char *driverName() const;

 private:
  bool parseLine_(const char *line);
  void handleDocument_(JsonDocument &doc);
  void updateLinkState_();

  Stream *link_ = nullptr;
  FlockDetectionHandler detectionHandler_ = nullptr;
  FlockEventHandler eventHandler_ = nullptr;
  FlockCalibrationHandler calibrationHandler_ = nullptr;
  FlockBridgeStatus status_;
  char line_[FLOCK_UART_MAX_LINE] = {0};
  uint16_t lineLen_ = 0;
  bool discarding_ = false;
};

#endif
