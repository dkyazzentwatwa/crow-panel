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
//   /mpc/loops/<pack>/<name>.wav  16-bit mono PCM
//
// `bars` is the load-bearing field: the sequencer derives its step length from
// frames/(bars*16), which is the only way to stay locked to a loop whose real
// tempo is fractional (72.5, 77.6, 65.1...).
//
// RATES. `frames` in loops.txt counts SOURCE frames at the WAV's own rate, and
// scripts/build-loop-packs.py writes every pack at kSourceRateDefault (22050).
// That used to equal the engine rate, so the distinction did not exist and the
// player walked the buffer one frame per output frame. It no longer holds:
// getting this wrong plays the loop at the wrong speed AND silently mistunes
// the sequencer lock, because setLockedStepFrames() counts OUTPUT frames.
// stepFramesFor() therefore stays in source frames (it is what loops.txt
// describes) and stepFramesForEngine() is what the sequencer must be given.
namespace LoopLibrary {

// Every pack in the field is written at this rate; loops.txt has no rate
// column, so the catalog assumes it and loadLoop() corrects from the WAV
// header, which is authoritative.
static const uint32_t kSourceRateDefault = 22050;

struct LoopInfo {
  char name[20] = "";
  char title[24] = "";
  uint16_t bpmTenths = 0;  // 725 = 72.5 BPM, display only
  uint8_t bars = 0;
  uint32_t frames = 0;     // SOURCE frames, at srcRate
  uint32_t srcRate = kSourceRateDefault;
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
// AudioEngine::stageLoop(). Returns SOURCE frames loaded, 0 on failure.
// srcRateOut receives the rate from the WAV header (authoritative over the
// catalog's assumption) and must be passed on to stageLoop().
uint32_t loadLoop(uint8_t index, int16_t **pcmOut, uint32_t *srcRateOut = nullptr);

// Frames per 16th step in SOURCE frames: frames / (bars * 16).
uint32_t stepFramesFor(const LoopInfo &loop);

// The same step length converted to ENGINE frames. The sequencer's step clock
// counts rendered output frames, so this - not stepFramesFor() - is what
// Sequencer::setLockedStepFrames() must be given. Pass the rate reported by
// loadLoop(); 0 falls back to the catalog's assumption.
uint32_t stepFramesForEngine(const LoopInfo &loop, uint32_t srcRate);

}  // namespace LoopLibrary

#endif
