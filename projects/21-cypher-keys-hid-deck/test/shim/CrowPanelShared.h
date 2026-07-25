// Host-test stand-in for the shared library header, shadowing the real one on
// the include path. src/KeysTouch.cpp only needs CrowDisplay::touchPoints(), so
// that is all this declares - host_main.cpp implements it from a scripted list
// of raw contacts, which lets the tests exercise the real poll cadence, release
// debounce and track-id matching without a panel.
#ifndef CYPHER_KEYS_HOST_CROWPANELSHARED_H
#define CYPHER_KEYS_HOST_CROWPANELSHARED_H

#include <Arduino.h>

namespace CrowDisplay {

// Field-for-field the same struct the real DisplayBringup.h declares.
struct TouchPointData {
  int16_t x;
  int16_t y;
  uint8_t id;
};

uint8_t touchPoints(TouchPointData *out, uint8_t maxPoints);

}  // namespace CrowDisplay

#endif
