#include "Throttle.h"

bool Throttle::ready() {
  unsigned long now = millis();
  if (now - lastMs_ < intervalMs_) {
    return false;
  }
  lastMs_ = now;
  return true;
}

void Throttle::reset() {
  lastMs_ = millis();
}
