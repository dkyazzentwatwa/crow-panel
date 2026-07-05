#ifndef BADGEOPS_BADGE_READER_H
#define BADGEOPS_BADGE_READER_H

#include "../config/ProjectConfig.h"
#include <Arduino.h>
#include <CrowPanelShared.h>

struct BadgeRead {
  String uid;
  String reader;
  unsigned long readAtMs;
};

class BadgeReader {
 public:
  virtual ~BadgeReader() {}
  virtual void begin(const HardwareProfile &profile) = 0;
  virtual bool poll(BadgeRead &read) = 0;
  virtual const char *driverName() const = 0;
};

#endif
