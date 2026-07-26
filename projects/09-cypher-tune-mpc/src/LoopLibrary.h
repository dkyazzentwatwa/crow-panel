#ifndef CYPHER_TUNE_LOOP_LIBRARY_H
#define CYPHER_TUNE_LOOP_LIBRARY_H

#include "../config/ProjectConfig.h"
#include <Arduino.h>

// Backing loops: long, bar-aligned musical beds that play under the pads.
// Built by scripts/build-loop-packs.py from sample-pack sources.
//
// These are far too big for flash (a 30 s loop is 1.3 MB), so exactly one is
// resident in PSRAM at a time and is loaded from SD on demand.
//
// Loops are grouped one directory per sample pack. A single flat list was fine
// at 11 loops and unusable at 41:
//
//   /mpc/loops/<pack>/pack.txt    display title, one line (optional)
//   /mpc/loops/<pack>/loops.txt   name<TAB>title<TAB>bpm<TAB>bars<TAB>frames
//   /mpc/loops/<pack>/<name>.wav  16-bit mono PCM at the engine rate
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
  uint8_t pack = 0;        // index into the pack table
};

struct PackInfo {
  char name[20] = "";      // directory name; also the path component
  char title[24] = "";     // pack.txt, else the directory name
  uint8_t firstLoop = 0;   // a pack's loops are stored contiguously
  uint8_t count = 0;
};

// Only the catalog is capped: exactly one loop's PCM is ever resident, so this
// costs ~56 bytes of metadata each, not a megabyte each.
static const uint8_t kMaxLoops = 64;
static const uint8_t kMaxPacks = 12;

// Scans the pack directories (once; repeat calls are cheap). Returns the total
// loop count across all packs.
uint8_t begin();
uint8_t count();
const LoopInfo &info(uint8_t index);
int8_t indexOfName(const char *name);  // -1 if absent; searches every pack

uint8_t packCount();
const PackInfo &pack(uint8_t index);
// Global loop index for the n'th loop of a pack, or -1.
int8_t loopInPack(uint8_t packIndex, uint8_t slot);

// Loads one loop's PCM into PSRAM. Caller owns the buffer and hands it to
// AudioEngine::stageLoop(). Returns frames loaded, 0 on failure.
uint32_t loadLoop(uint8_t index, int16_t **pcmOut);

// Frames per 16th step for this loop: frames / (bars * 16).
uint32_t stepFramesFor(const LoopInfo &loop);

}  // namespace LoopLibrary

#endif
