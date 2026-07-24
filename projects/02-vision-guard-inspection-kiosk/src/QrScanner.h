#ifndef VISION_GUARD_QR_SCANNER_H
#define VISION_GUARD_QR_SCANNER_H

#include <Arduino.h>
#include <CrowPanelShared.h>

class QrScanner {
 public:
  void begin();
  bool poll(String &qr);

 private:
  Throttle cadence_{9000};
  uint8_t index_ = 0;
};

#endif
