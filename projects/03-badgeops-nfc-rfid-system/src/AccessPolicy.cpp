#include "AccessPolicy.h"
#include <CrowPanelShared.h>

void AccessPolicy::begin(const char *zone) {
  zone_ = zone;
  Logger::info("access-policy", "mock zone=" + zone_);
}

AccessDecision AccessPolicy::evaluate(const BadgeRead &read, const BadgeRecord &record, bool found) const {
  if (!found) {
    return {"denied", "ACCESS_DENIED unknown uid=" + read.uid, "Badge not in registry"};
  }

  if (record.status != "active") {
    return {"denied", "ACCESS_DENIED inactive badge=" + record.badgeId,
            "Badge " + record.status};
  }

  if (!zoneAllowed(record.allowedZones, zone_)) {
    return {"denied", "ACCESS_DENIED zone=" + zone_ + " badge=" + record.badgeId,
            "Zone " + zone_ + " not permitted"};
  }

  return {"granted", "ACCESS_GRANTED " + record.name + " zone=" + zone_,
          "Zone " + zone_ + " confirmed"};
}

// allowedZones is a comma-separated list like "lab,front-desk".
// Compare whole tokens: a plain indexOf() would let zone "desk" match
// "front-desk" and grant access it should deny.
bool AccessPolicy::zoneAllowed(const String &allowedZones, const String &zone) {
  int start = 0;
  const int len = allowedZones.length();
  while (start <= len) {
    int comma = allowedZones.indexOf(',', start);
    int end = (comma < 0) ? len : comma;
    String token = allowedZones.substring(start, end);
    token.trim();
    if (token == zone) {
      return true;
    }
    if (comma < 0) {
      break;
    }
    start = comma + 1;
  }
  return false;
}
