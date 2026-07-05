#ifndef BADGEOPS_PN532_READER_H
#define BADGEOPS_PN532_READER_H

#include "BadgeReader.h"

class Pn532Reader : public BadgeReader {
 public:
  void begin(const HardwareProfile &profile) override;
  bool poll(BadgeRead &read) override;
  const char *driverName() const override;

 private:
  const HardwareProfile *profile_ = nullptr;
};

#endif
