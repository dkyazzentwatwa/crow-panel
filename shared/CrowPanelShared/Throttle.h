#ifndef CROW_PANEL_THROTTLE_H
#define CROW_PANEL_THROTTLE_H

#include <Arduino.h>

// Rate limiter for periodic work in loop().
//
// Uses (millis() - last) unsigned subtraction, which stays correct across
// the ~49.7-day millis() wraparound because unsigned arithmetic wraps the
// same way. Every timing gate in this repo goes through this one class so
// the pattern is explained exactly once.
class Throttle {
 public:
  explicit Throttle(unsigned long intervalMs) : intervalMs_(intervalMs) {}

  // True at most once per interval; rearms itself.
  bool ready();

  // Restart the interval from now.
  void reset();

 private:
  unsigned long intervalMs_;
  unsigned long lastMs_ = 0;  // 0 => first ready() fires one interval after boot
};

#endif
