#ifndef LITTLEHAKR_RF_LAB_SESSION_STORE_H
#define LITTLEHAKR_RF_LAB_SESSION_STORE_H

#include <Arduino.h>
#include "RfLabTypes.h"

class RfLabSessionStore {
 public:
  bool begin(String &status);
  bool save(const RfLabState &state, String &status);
  bool clear(String &status);
  bool ready() const { return ready_; }

 private:
  bool ready_ = false;
};

#endif
