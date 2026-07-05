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
  void renderTap(const BadgeRead &read);
  void renderDecision(const AccessDecision &decision, const BadgeRecord &record, bool found);
};

#endif
