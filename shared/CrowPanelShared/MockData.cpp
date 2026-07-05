#include "MockData.h"

float MockData::wave(float base, float span, uint32_t periodMs) {
  uint32_t phase = millis() % periodMs;
  float normalized = phase / (float)periodMs;
  float triangle = normalized < 0.5f ? normalized * 2.0f : (1.0f - normalized) * 2.0f;
  return base + (triangle * span);
}

String MockData::badgeUid(uint8_t index) {
  static const char *uids[] = {"04:A1:22:9C", "7A:31:90:0D", "C2:44:10:AA"};
  return String(uids[index % 3]);
}

String MockData::nodeId(uint8_t index) {
  return "node-" + String(index + 1);
}

String MockData::isoTime() {
  return "2026-07-01T00:00:00Z+" + String(millis() / 1000);
}
