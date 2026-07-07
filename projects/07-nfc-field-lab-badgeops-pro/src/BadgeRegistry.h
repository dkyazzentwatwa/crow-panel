#ifndef NFC_FIELD_LAB_BADGE_REGISTRY_H
#define NFC_FIELD_LAB_BADGE_REGISTRY_H

#include <Arduino.h>
#include "NfcTypes.h"

struct BadgeRecord {
  String badgeId;
  String uid;
  String name;
  String role;
  String status;
  String allowedZones;
};

struct BadgeDecision {
  String status;
  String summary;
  String detail;
};

class BadgeRegistry {
 public:
  void begin();
  bool findByUid(const String &uid, BadgeRecord &record) const;
  BadgeDecision evaluateUid(const String &uid) const;
  void printAll(Stream &out) const;
  size_t count() const;
};

#endif
