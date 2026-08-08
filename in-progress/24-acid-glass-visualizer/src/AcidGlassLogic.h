#ifndef ACID_GLASS_LOGIC_H
#define ACID_GLASS_LOGIC_H

#include <stdint.h>

namespace AcidGlassLogic {

inline uint8_t wrapIndex(int16_t value, uint8_t count) {
  if (count == 0) return 0;
  while (value < 0) value += count;
  return static_cast<uint8_t>(value % count);
}

inline uint8_t mapCoordinate(int32_t coordinate, int32_t maximum) {
  if (coordinate <= 0 || maximum <= 0) return 0;
  if (coordinate >= maximum) return 255;
  return static_cast<uint8_t>(coordinate * 255L / maximum);
}

inline bool validPcmFormat(uint16_t format, uint16_t channels, uint16_t bits,
                           uint32_t sampleRate) {
  return format == 1 && (channels == 1 || channels == 2) && bits == 16 &&
         sampleRate >= 8000 && sampleRate <= 48000;
}

inline uint32_t resampleStepQ16(uint32_t sourceRate, uint32_t outputRate = 44100) {
  if (sourceRate == 0 || outputRate == 0) return 0;
  return static_cast<uint32_t>((static_cast<uint64_t>(sourceRate) << 16) / outputRate);
}

inline uint8_t smoothByte(uint8_t current, uint8_t target, uint8_t amount = 72) {
  if (current == target) return current;
  int16_t delta = static_cast<int16_t>(target) - current;
  int16_t step = (delta * amount) / 255;
  if (step == 0) step = delta > 0 ? 1 : -1;
  return static_cast<uint8_t>(static_cast<int16_t>(current) + step);
}

inline uint8_t adaptiveTargetFps(uint32_t frameUs, uint8_t currentTarget,
                                 uint32_t stableFrames, uint8_t mode) {
  if (mode == 1) return 30;
  if (mode == 2) return 45;
  if (frameUs > 28000) return 30;
  if (frameUs > 22000) return 36;
  if (currentTarget < 45 && stableFrames >= 150) return currentTarget == 30 ? 36 : 45;
  return currentTarget;
}

inline uint8_t qualityTierFor(uint8_t targetFps, uint8_t mode) {
  if (mode == 1 || targetFps <= 30) return 0;
  if (mode == 2 || targetFps >= 45) return 2;
  return 1;
}

}  // namespace AcidGlassLogic

#endif
