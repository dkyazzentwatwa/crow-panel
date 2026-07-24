#ifndef BADGEOPS_BADGE_REGISTRY_H
#define BADGEOPS_BADGE_REGISTRY_H

#include <Arduino.h>

struct BadgeRecord {
  String badgeId;
  String uid;
  String name;
  String role;
  String status;
  String allowedZones;
};

class BadgeRegistry {
 public:
  void begin();
  bool findByUid(const String &uid, BadgeRecord &record) const;
  void printAll(Stream &out) const;

  // Iteration for the on-panel Registry screen. The table is a fixed const
  // array, so these are cheap reads with no allocation.
  uint8_t count() const;
  const BadgeRecord &at(uint8_t index) const;  // clamps to the last row
};

#endif
