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

  // True once the reader has a working transport. The mock reader is always
  // "ready"; the hardware drivers only return true after their chip answered.
  // Lets the Readers settings screen show an honest per-reader state.
  virtual bool ready() const { return true; }
};

#endif
