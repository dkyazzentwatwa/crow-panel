#ifndef RELAYOPS_SENSOR_SERVER_H
#define RELAYOPS_SENSOR_SERVER_H

#include "../config/ProjectConfig.h"
#include <Arduino.h>
#include "HubTypes.h"

#if USE_WIFI
#include <WebServer.h>
#endif

// Callbacks fire from handle() when a node POSTs. Plain function pointers on
// purpose, matching SerialCommandRouter: the sketch's globals are directly
// reachable from free-function handlers.
typedef void (*SensorHandler)(const SensorReading &reading);
typedef void (*DeviceRegisterHandler)(const ControlDevice &device);

// Inbound web server the hub runs so remote ESP32 nodes can push data:
//   POST /sensor    telemetry JSON -> onSensor (optionally self-registers a
//                   control device if the body carries control_url + pin)
//   POST /register  {deviceId, host, path, pin} or {deviceId, control_url,
//                   pin} -> onRegister
//   GET  /health    liveness probe
//
// Gated on USE_WIFI: with the flag off every method is a no-op so the sketch
// still runs Serial-only (driven by the mock source and `feed`).
class SensorServer {
 public:
  void begin(uint16_t port, SensorHandler onSensor, DeviceRegisterHandler onRegister);
  void handle();          // call once per loop()
  bool running() const { return running_; }

 private:
#if USE_WIFI
  void handleSensor();
  void handleRegister();
  void emitRegister(const String &id, const String &host, const String &path, uint8_t pin);
  WebServer *server_ = nullptr;
#endif
  SensorHandler onSensor_ = nullptr;
  DeviceRegisterHandler onRegister_ = nullptr;
  uint16_t port_ = 80;
  bool running_ = false;
};

#endif
