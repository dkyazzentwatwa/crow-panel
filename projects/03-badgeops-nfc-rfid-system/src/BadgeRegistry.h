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
};

#endif
