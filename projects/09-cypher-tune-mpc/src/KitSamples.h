#ifndef CYPHER_TUNE_KIT_SAMPLES_H
#define CYPHER_TUNE_KIT_SAMPLES_H

#include <Arduino.h>

// The built-in kit, baked into flash as 16-bit PCM at the engine rate.
// Generated from real drum recordings by scripts/build-builtin-kit.py.
//
// These live in flash (.rodata, memory-mapped) but are COPIED into PSRAM at
// boot rather than played in place: an NVS write (theme/brightness) briefly
// disables the flash cache, and the audio task reading XIP-mapped sample data
// at that moment would glitch or fault. PSRAM costs ~480 KB of 32 MB.
struct KitSample {
  const int16_t *pcm;
  uint32_t frames;
  uint32_t rate;
  const char *label;
  const char *ref;
  uint8_t defaultVelocity;
  uint8_t chokeGroup;
  uint8_t gain;
};

static const uint8_t kBuiltinKitPads = 16;
extern const KitSample kBuiltinKit[kBuiltinKitPads];

#endif
