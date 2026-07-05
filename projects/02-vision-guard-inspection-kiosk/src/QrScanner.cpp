#include "QrScanner.h"
#include <CrowPanelShared.h>

void QrScanner::begin() {
  Logger::info("qr", "mock QR scanner ready");
}

bool QrScanner::poll(String &qr) {
  if (millis() - lastScanMs_ < 5000) {
    return false;
  }

  lastScanMs_ = millis();
  qr = "INSPECT-" + String(1000 + index_);
  index_ = (index_ + 1) % 12;
  Logger::info("qr", "mock scan " + qr);
  return true;
}
