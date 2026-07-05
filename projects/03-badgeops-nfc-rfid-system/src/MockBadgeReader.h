#ifndef BADGEOPS_MOCK_BADGE_READER_H
#define BADGEOPS_MOCK_BADGE_READER_H

#include "BadgeReader.h"

class MockBadgeReader : public BadgeReader {
 public:
  void begin(const HardwareProfile &profile) override;
  bool poll(BadgeRead &read) override;
  const char *driverName() const override;

 private:
  const HardwareProfile *profile_ = nullptr;
  unsigned long lastReadMs_ = 0;
  uint8_t index_ = 0;
};

#endif
