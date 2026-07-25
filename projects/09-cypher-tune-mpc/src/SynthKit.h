#ifndef CYPHER_TUNE_SYNTH_KIT_H
#define CYPHER_TUNE_SYNTH_KIT_H

#include "../config/ProjectConfig.h"
#include <Arduino.h>
#include "SampleBank.h"

// The metronome click generator. The playable kit used to be synthesized here
// too; it is now real drum recordings baked into flash - see BuiltinKit.
namespace SynthKit {



// One metronome blip (accent = downbeat pitch). Caller owns the buffer
// (free() it); returns null on alloc failure.
int16_t *synthesizeMetronome(bool accent, uint32_t engineRate, uint32_t *framesOut);

}  // namespace SynthKit

#endif
