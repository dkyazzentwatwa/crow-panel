#include "EventLog.h"
#include <CrowPanelShared.h>

void EventLog::add(const String &message) {
  latest_ = message;
  Logger::info("event-log", latest_);
}

String EventLog::latest() const {
  return latest_;
}
