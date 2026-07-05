#ifndef BADGEOPS_EVENT_LOG_H
#define BADGEOPS_EVENT_LOG_H

#include <Arduino.h>

class EventLog {
 public:
  void add(const String &message);
  String latest() const;

 private:
  String latest_ = "none";
};

#endif
