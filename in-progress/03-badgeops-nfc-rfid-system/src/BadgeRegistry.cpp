#include "BadgeRegistry.h"
#include <CrowPanelShared.h>

// The first three rows keep the UIDs the docs and the mock reader use
// (04:A1:22:9C granted, 7A:31:90:0D zone-denied, C2:44:10:AA suspended). The
// rest give the Registry list enough rows to scroll and the audit log some
// variety of outcomes.
static const BadgeRecord RECORDS[] = {
  {"badge-demo-001", "04:A1:22:9C", "Demo Operator", "technician", "active", "lab,front-desk"},
  {"badge-demo-002", "7A:31:90:0D", "Guest Builder", "guest", "active", "front-desk"},
  {"badge-demo-003", "C2:44:10:AA", "Former Contractor", "contractor", "suspended", "none"},
  {"badge-demo-004", "19:8C:52:F1", "Lab Lead", "admin", "active", "lab,front-desk,server-room"},
  {"badge-demo-005", "63:0B:77:2A", "Night Tech", "technician", "active", "lab"},
  {"badge-demo-006", "AE:44:D0:91", "Vendor Rep", "vendor", "suspended", "front-desk"},
  {"badge-demo-007", "5F:12:63:CC", "Summer Intern", "guest", "active", "front-desk"},
  {"badge-demo-008", "88:90:A1:07", "Retired Reader", "technician", "expired", "lab"}
};

static const uint8_t RECORD_COUNT = sizeof(RECORDS) / sizeof(RECORDS[0]);

void BadgeRegistry::begin() {
  Logger::info("badge-registry", "mock badge registry loaded: " + String(RECORD_COUNT) + " badges");
}

uint8_t BadgeRegistry::count() const {
  return RECORD_COUNT;
}

const BadgeRecord &BadgeRegistry::at(uint8_t index) const {
  if (index >= RECORD_COUNT) {
    index = RECORD_COUNT - 1;
  }
  return RECORDS[index];
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

void BadgeRegistry::printAll(Stream &out) const {
  out.println(F("[badges] id | uid | name | role | status | zones"));
  for (size_t i = 0; i < sizeof(RECORDS) / sizeof(RECORDS[0]); i++) {
    const BadgeRecord &r = RECORDS[i];
    out.print(F("  "));
    out.print(r.badgeId);
    out.print(F(" | "));
    out.print(r.uid);
    out.print(F(" | "));
    out.print(r.name);
    out.print(F(" | "));
    out.print(r.role);
    out.print(F(" | "));
    out.print(r.status);
    out.print(F(" | "));
    out.println(r.allowedZones);
  }
}
