#ifndef CROW_PANEL_EVENT_BUS_H
#define CROW_PANEL_EVENT_BUS_H

#include <Arduino.h>

struct AppEvent {
  String type;
  String payload;
  unsigned long timestampMs;
};

class EventBus {
 public:
  void publish(const String &type, const String &payload);
  AppEvent lastEvent() const;

 private:
  AppEvent last_ = {"none", "none", 0};
};

#endif
