#ifndef FIELDOPS_UI_H
#define FIELDOPS_UI_H

#include "../config/ProjectConfig.h"
#include <Arduino.h>
#include "SensorNode.h"
#include "FieldOpsDashboard.h"

// Thin adapter between the sketch's processPacket() pipeline and the bespoke
// touch dashboard. It echoes each update to Serial (the headless view) and
// forwards it into FieldOpsDashboard, then exposes the dashboard's serial-parity
// actions so every touch control has an equivalent command.
class FieldOpsUi {
 public:
  void begin();

  // Call once per loop(); drives the dashboard + touch. Returns true and fills
  // `event` when the user launched an app-domain action (pin / ack) that the
  // sketch should record in the shared event log.
  bool tick(FieldOpsUiEvent &event);

  // Data in (from processPacket).
  void renderDashboard(const SensorPacket &packet);
  void renderAlert(const String &alert);
  void renderSummary(const String &summary);
  void note(const String &line) { dashboard_.note(line); }

  // Serial-parity actions (the screen / pin / ack / log commands).
  bool setScreen(FieldOpsScreen s) { return dashboard_.setScreen(s); }
  bool pinNodeByIndex(int8_t idx) { return dashboard_.pinNodeByIndex(idx); }
  int8_t pinNodeByName(const String &name) { return dashboard_.pinNodeByName(name); }
  int8_t ackNewestAlert() { return dashboard_.ackNewestAlert(); }
  int8_t ackAlertByDisplay(uint8_t d) { return dashboard_.ackAlertByDisplay(d); }
  void logPagePrev() { dashboard_.logPagePrev(); }
  void logPageNext() { dashboard_.logPageNext(); }
  bool setLogPage(uint16_t p) { return dashboard_.setLogPage(p); }

  void printTouch(Print &out) const { dashboard_.printTouch(out); }

  // Read-only access for status/selftest introspection.
  const FieldOpsDashboard &dashboard() const { return dashboard_; }

 private:
  FieldOpsDashboard dashboard_;
};

#endif
