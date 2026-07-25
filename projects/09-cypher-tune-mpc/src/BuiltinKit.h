#ifndef CYPHER_TUNE_BUILTIN_KIT_H
#define CYPHER_TUNE_BUILTIN_KIT_H

#include "../config/ProjectConfig.h"
#include <Arduino.h>
#include "SampleBank.h"

// Installs the flash-resident built-in kit (KitSamples.cpp, generated from real
// drum recordings by scripts/build-builtin-kit.py) into a SampleBank.
//
// The PCM is copied out of flash into PSRAM rather than played in place. An NVS
// write - which happens whenever the theme, brightness, or idle-dim setting
// changes - briefly disables the flash cache, and the audio task reading
// XIP-mapped sample data at that moment would glitch or fault. The copy costs
// ~440 KB of the 32 MB PSRAM and removes the hazard entirely.
namespace BuiltinKit {

// Fills all 16 pads. Returns how many received a buffer (16 on success).
uint8_t loadAll(SampleBank &bank);

// One pad, used as the per-pad fallback when an SD kit omits a WAV.
bool loadPad(SampleBank &bank, uint8_t pad);

}  // namespace BuiltinKit

#endif
