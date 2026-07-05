#ifndef CROW_PANEL_MOCK_DATA_H
#define CROW_PANEL_MOCK_DATA_H

#include <Arduino.h>

class MockData {
 public:
  static float wave(float base, float span, uint32_t periodMs);
  static String badgeUid(uint8_t index);
  static String nodeId(uint8_t index);
  static String isoTime();
};

#endif
