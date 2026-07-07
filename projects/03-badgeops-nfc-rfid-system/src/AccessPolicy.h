#ifndef BADGEOPS_ACCESS_POLICY_H
#define BADGEOPS_ACCESS_POLICY_H

#include <Arduino.h>
#include "BadgeReader.h"
#include "BadgeRegistry.h"

struct AccessDecision {
  String status;
  String message;
};

class AccessPolicy {
 public:
  void begin(const char *zone);
  AccessDecision evaluate(const BadgeRead &read, const BadgeRecord &record, bool found) const;

 private:
  static bool zoneAllowed(const String &allowedZones, const String &zone);

  String zone_ = "lab";
};

#endif
