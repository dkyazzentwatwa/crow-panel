#include "BadgeRegistry.h"
#include <CrowPanelShared.h>

static const BadgeRecord RECORDS[] = {
  {"badge-demo-001", "04:A1:22:9C", "Demo Operator", "technician", "active", "lab,front-desk"},
  {"badge-demo-002", "7A:31:90:0D", "Guest Builder", "guest", "active", "front-desk"},
  {"badge-demo-003", "C2:44:10:AA", "Former Contractor", "contractor", "suspended", "none"}
};

void BadgeRegistry::begin() {
  Logger::info("badge-registry", "mock badge registry loaded");
}

bool BadgeRegistry::findByUid(const String &uid, BadgeRecord &record) const {
  for (size_t i = 0; i < sizeof(RECORDS) / sizeof(RECORDS[0]); i++) {
    if (RECORDS[i].uid == uid) {
      record = RECORDS[i];
      return true;
    }
  }

  return false;
}
