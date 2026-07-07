#ifndef BADGEOPS_UI_H
#define BADGEOPS_UI_H

#include "../config/ProjectConfig.h"
#include <Arduino.h>
#include "BadgeReader.h"
#include "BadgeRegistry.h"
#include "AccessPolicy.h"

class BadgeOpsUi {
 public:
  void begin();
  // Call once per loop(); drives the status screen when USE_DISPLAY=1, no-op otherwise.
  void tick();
  void renderTap(const BadgeRead &read);
  void renderDecision(const AccessDecision &decision, const BadgeRecord &record, bool found);
};

#endif
