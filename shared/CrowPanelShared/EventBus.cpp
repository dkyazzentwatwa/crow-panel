#include "EventBus.h"
#include "Logger.h"

void EventBus::publish(const String &type, const String &payload) {
  last_ = {type, payload, millis()};
  Logger::info("event", type + ": " + payload);
}

AppEvent EventBus::lastEvent() const {
  return last_;
}
