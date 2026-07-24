#ifndef CYPHER_TUNE_WAV_LOADER_H
#define CYPHER_TUNE_WAV_LOADER_H

#include "../config/ProjectConfig.h"
#include <Arduino.h>
#include "SampleBank.h"

// SD-card WAV kit loading (guarded by USE_MPC_SD; stubs otherwise). Kits are
// folders under CYPHER_TUNE_KIT_DIR ("/mpc/kits/<name>/pad01.wav ...
// pad16.wav"), 16-bit PCM mono at any rate 8-48 kHz - files keep their
// native rate, the engine's voice resampler converts on the fly. Missing pad
// files fall back to the builtin synthesized sound. All SD I/O happens in
// loop context; the audio task never touches the card.
namespace WavLoader {

// Mounts SD_MMC once (idempotent). Returns card-present.
bool beginSd();
bool sdReady();

// Space-separated kit folder names found on the card (excluding "builtin").
String listKits();

// Loads a kit into `staging`. Returns pads loaded from SD (0 = kit dir
// missing/unreadable; the caller should not swap to it). Pads without a
// valid WAV get the builtin sound so the kit is always fully playable.
uint8_t loadKit(const char *name, SampleBank &staging, String &status);

}  // namespace WavLoader

#endif
