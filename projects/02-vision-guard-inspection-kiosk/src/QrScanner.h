#ifndef VISION_GUARD_QR_SCANNER_H
#define VISION_GUARD_QR_SCANNER_H

#include <Arduino.h>

class QrScanner {
 public:
  void begin();
  bool poll(String &qr);

 private:
  unsigned long lastScanMs_ = 0;
  uint8_t index_ = 0;
};

#endif
