#ifndef CYPHER_TUNE_LOOP_LIBRARY_H
#define CYPHER_TUNE_LOOP_LIBRARY_H

#include "../config/ProjectConfig.h"
#include <Arduino.h>

// Backing loops: long, bar-aligned musical beds that play under the pads.
//
// These are far too big for flash (a 30 s loop is 1.3 MB), so exactly one is
// resident in PSRAM at a time and is loaded from SD on demand. The catalog is
// a tab-separated manifest written by scripts/cut-gospel-loops.py:
//
//   /mpc/loops/loops.txt   name<TAB>title<TAB>bpm<TAB>bars<TAB>frames
//   /mpc/loops/<name>.wav  16-bit mono PCM at the engine rate
//
// `bars` is the load-bearing field: the sequencer derives its step length from
// frames/(bars*16), which is the only way to stay locked to a loop whose real
// tempo is fractional (72.5, 77.6, 65.1...).
namespace LoopLibrary {

struct LoopInfo {
  char name[20] = "";
  char title[24] = "";
  uint16_t bpmTenths = 0;  // 725 = 72.5 BPM, display only
  uint8_t bars = 0;
  uint32_t frames = 0;
};

static const uint8_t kMaxLoops = 16;

// Reads the manifest (once; repeat calls are cheap). Returns the entry count.
uint8_t begin();
uint8_t count();
const LoopInfo &info(uint8_t index);
int8_t indexOfName(const char *name);  // -1 if absent

// Loads one loop's PCM into PSRAM. Caller owns the buffer and hands it to
// AudioEngine::stageLoop(). Returns frames loaded, 0 on failure.
uint32_t loadLoop(uint8_t index, int16_t **pcmOut);

// Frames per 16th step for this loop: frames / (bars * 16).
uint32_t stepFramesFor(const LoopInfo &loop);

}  // namespace LoopLibrary

#endif
