#include "AccessPolicy.h"
#include <CrowPanelShared.h>

void AccessPolicy::begin(const char *zone) {
  zone_ = zone;
  Logger::info("access-policy", "mock zone=" + zone_);
}

AccessDecision AccessPolicy::evaluate(const BadgeRead &read, const BadgeRecord &record, bool found) const {
  if (!found) {
    return {"denied", "ACCESS_DENIED unknown uid=" + read.uid};
  }

  if (record.status != "active") {
    return {"denied", "ACCESS_DENIED inactive badge=" + record.badgeId};
  }

  if (record.allowedZones.indexOf(zone_) < 0) {
    return {"denied", "ACCESS_DENIED zone=" + zone_ + " badge=" + record.badgeId};
  }

  return {"granted", "ACCESS_GRANTED " + record.name + " zone=" + zone_};
}
