#include "BadgeRegistry.h"
#include <CrowPanelShared.h>

namespace {
const BadgeRecord RECORDS[] = {
  {"badge-demo-001", "04:A1:22:9C", "Demo Operator", "technician", "active", "lab,front-desk"},
  {"badge-demo-002", "7A:31:90:0D", "Guest Builder", "guest", "active", "front-desk"},
  {"badge-demo-003", "C2:44:10:AA", "Former Contractor", "contractor", "suspended", "none"}
};
}

void BadgeRegistry::begin() {
  Logger::info("badge-registry", "demo badge registry loaded; UID-only decisions are not security");
}

bool BadgeRegistry::findByUid(const String &uid, BadgeRecord &record) const {
  for (size_t i = 0; i < count(); i++) {
    if (RECORDS[i].uid == uid) {
      record = RECORDS[i];
      return true;
    }
  }
  return false;
}

BadgeDecision BadgeRegistry::evaluateUid(const String &uid) const {
  BadgeRecord record;
  if (!findByUid(uid, record)) {
    return {"denied", "DENIED unknown badge", "UID " + uid + " is not in the demo registry"};
  }

  if (record.status != "active") {
    return {"denied", "DENIED " + record.status, record.name + " is marked " + record.status};
  }

  return {"granted", "GRANTED " + record.role, record.name + " allowed for " + record.allowedZones};
}

void BadgeRegistry::printAll(Stream &out) const {
  out.println(F("[badges] id | uid | name | role | status | zones"));
  for (size_t i = 0; i < count(); i++) {
    const BadgeRecord &record = RECORDS[i];
    out.print(F("  "));
    out.print(record.badgeId);
    out.print(F(" | "));
    out.print(record.uid);
    out.print(F(" | "));
    out.print(record.name);
    out.print(F(" | "));
    out.print(record.role);
    out.print(F(" | "));
    out.print(record.status);
    out.print(F(" | "));
    out.println(record.allowedZones);
  }
}

size_t BadgeRegistry::count() const {
  return sizeof(RECORDS) / sizeof(RECORDS[0]);
}
