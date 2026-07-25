#ifndef BADGEOPS_PN532_READER_H
#define BADGEOPS_PN532_READER_H

#include "BadgeReader.h"

class Pn532Reader : public BadgeReader {
 public:
  void begin(const HardwareProfile &profile) override;
  bool poll(BadgeRead &read) override;
  const char *driverName() const override;
  bool ready() const override { return ready_; }

 private:
  const HardwareProfile *profile_ = nullptr;
  bool ready_ = false;
  Throttle pollGate_{250};
  String lastUid_ = "";
  unsigned long lastUidMs_ = 0;
};

#endif
