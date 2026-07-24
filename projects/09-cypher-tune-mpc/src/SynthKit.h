#ifndef CYPHER_TUNE_SYNTH_KIT_H
#define CYPHER_TUNE_SYNTH_KIT_H

#include "../config/ProjectConfig.h"
#include <Arduino.h>
#include "SampleBank.h"

// Boot-time synthesis of the built-in kit: pure float math rendered once into
// PCM buffers, so the mixer only ever plays samples and an SD kit can replace
// any subset of pads later. Costs a few hundred ms and ~300 KB of PSRAM.
namespace SynthKit {

// Fills all 16 pads of the bank at the bank's engine rate. Returns the number
// of pads that received a buffer (16 on success; fewer only on alloc failure).
uint8_t synthesizeBuiltinKit(SampleBank &bank);

// One pad's builtin sound (used as the fallback when an SD kit is missing
// that pad's WAV).
bool synthesizePad(SampleBank &bank, uint8_t pad);

// One metronome blip (accent = downbeat pitch). Caller owns the buffer
// (free() it); returns null on alloc failure.
int16_t *synthesizeMetronome(bool accent, uint32_t engineRate, uint32_t *framesOut);

}  // namespace SynthKit

#endif
