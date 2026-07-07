// Inbound web server: COMPILE-VERIFIED on esp32:esp32:esp32p4 (core 3.3.8),
// NOT HARDWARE-VERIFIED. The listening socket lives on the onboard ESP32-C6
// (esp_hosted); bring Wi-Fi up (CrowNetworkClient) before nodes can reach it.
// With USE_WIFI=0 the whole class is a no-op and the dashboard is driven by
// the mock source + `feed` serial command instead.

#include "SensorServer.h"
#include <CrowPanelShared.h>

#if USE_WIFI
#include <ArduinoJson.h>

namespace {
// "http://192.168.1.60/gpio" -> host="192.168.1.60", path="/gpio".
void splitUrl(const String &url, String &host, String &path) {
  String s = url;
  int scheme = s.indexOf("://");
  if (scheme >= 0) s = s.substring(scheme + 3);
  int slash = s.indexOf('/');
  if (slash < 0) {
    host = s;
    path = "/gpio";
  } else {
    host = s.substring(0, slash);
    path = s.substring(slash);
  }
}
}  // namespace
#endif

void SensorServer::begin(uint16_t port, SensorHandler onSensor, DeviceRegisterHandler onRegister) {
  port_ = port;
  onSensor_ = onSensor;
  onRegister_ = onRegister;
#if USE_WIFI
  server_ = new WebServer(port_);
  server_->on("/sensor", HTTP_POST, [this]() { handleSensor(); });
  server_->on("/register", HTTP_POST, [this]() { handleRegister(); });
  server_->on("/health", HTTP_GET, [this]() {
    server_->send(200, "application/json", "{\"ok\":true,\"service\":\"relayops-hub\"}");
  });
  server_->onNotFound([this]() {
    server_->send(404, "application/json", "{\"error\":\"not_found\"}");
  });
  server_->begin();
  running_ = true;
  Logger::info("server", "hub listening on :" + String(port_) + " (POST /sensor, /register)");
#else
  Logger::info("server", "mock: no web server (USE_WIFI=0); use `feed` to inject readings");
#endif
}

void SensorServer::handle() {
#if USE_WIFI
  if (server_ != nullptr) server_->handleClient();
#endif
}

#if USE_WIFI
void SensorServer::handleSensor() {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, server_->arg("plain"));
  if (err) {
    server_->send(400, "application/json", "{\"error\":\"invalid_json\"}");
    return;
  }

  SensorReading r;
  if (doc["nodeId"].is<const char *>()) {
    r.nodeId = doc["nodeId"].as<const char *>();
  } else {
    r.nodeId = doc["node"] | "node";
  }
  r.temperatureC = doc["temperatureC"] | NAN;
  r.humidityPct = doc["humidityPct"] | NAN;
  r.batteryPct = doc["batteryPct"] | NAN;
  r.rssi = doc["rssi"] | 0;
  r.motion = doc["motion"] | false;
  r.presenceOnly = doc["presence"] | (bool)isnan(r.temperatureC);
  r.receivedAtMs = millis();

  if (onSensor_ != nullptr) onSensor_(r);

  // A node that is also an actuator can advertise how to command it.
  if (doc["control_url"].is<const char *>()) {
    String host, path;
    splitUrl(doc["control_url"].as<const char *>(), host, path);
    emitRegister(r.nodeId, host, path, doc["pin"] | 2);
  }

  server_->send(200, "application/json", "{\"ok\":true}");
}

void SensorServer::handleRegister() {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, server_->arg("plain"));
  if (err) {
    server_->send(400, "application/json", "{\"error\":\"invalid_json\"}");
    return;
  }

  String id;
  if (doc["deviceId"].is<const char *>()) {
    id = doc["deviceId"].as<const char *>();
  } else {
    id = doc["id"] | "";
  }
  if (id.length() == 0) {
    server_->send(400, "application/json", "{\"error\":\"missing_device_id\"}");
    return;
  }

  String host, path;
  if (doc["control_url"].is<const char *>()) {
    splitUrl(doc["control_url"].as<const char *>(), host, path);
  } else {
    host = doc["host"] | "";
    path = doc["path"] | "/gpio";
  }
  if (host.length() == 0) {
    server_->send(400, "application/json", "{\"error\":\"missing_host\"}");
    return;
  }

  emitRegister(id, host, path, doc["pin"] | 2);
  server_->send(201, "application/json", "{\"ok\":true}");
}

void SensorServer::emitRegister(const String &id, const String &host, const String &path,
                                uint8_t pin) {
  ControlDevice dev;
  dev.deviceId = id;
  dev.host = host;
  dev.path = path.length() > 0 ? path : "/gpio";
  dev.pin = pin;
  dev.online = true;
  dev.lastSeenMs = millis();
  if (onRegister_ != nullptr) onRegister_(dev);
}
#endif  // USE_WIFI
