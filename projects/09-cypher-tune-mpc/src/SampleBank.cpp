#include "SampleBank.h"

#if defined(ARDUINO_ARCH_ESP32)
#include <esp_heap_caps.h>
#endif

namespace {

struct PadDefault {
  const char *label;
  uint8_t defaultVelocity;
  uint8_t chokeGroup;
};

// Same 16-pad map the project has always shipped; hats share choke group 1
// so a closed hat cuts the open hat ringing (and vice versa).
const PadDefault kPadDefaults[SampleBank::kPadCount] = {
  {"Kick", 115, 0},
  {"Snare", 110, 0},
  {"Hat", 92, 1},
  {"OpenHat", 88, 1},
  {"Clap", 108, 0},
  {"Rim", 94, 0},
  {"PercA", 96, 0},
  {"PercB", 96, 0},
  {"BassA", 112, 2},
  {"BassB", 112, 2},
  {"ChordA", 86, 3},
  {"ChordB", 86, 3},
  {"VoxA", 90, 0},
  {"VoxB", 90, 0},
  {"FxUp", 84, 0},
  {"FxDn", 84, 0},
};

void copyString(char *dst, size_t dstSize, const char *src) {
  strncpy(dst, src, dstSize - 1);
  dst[dstSize - 1] = '\0';
}

uint32_t incFP(uint32_t srcRate, uint32_t engineRate) {
  if (engineRate == 0) {
    return 1 << 16;
  }
  uint64_t inc = ((uint64_t)srcRate << 16) / engineRate;
  return inc == 0 ? 1 : (uint32_t)inc;
}

}  // namespace

void SampleBank::beginDefaults(uint32_t engineRate) {
  engineRate_ = engineRate == 0 ? 22050 : engineRate;
  freeAll();
  for (uint8_t i = 0; i < kPadCount; i++) {
    PadSound &pad = pads_[i];
    pad = PadSound();
    copyString(pad.label, sizeof(pad.label), kPadDefaults[i].label);
    copyString(pad.sampleRef, sizeof(pad.sampleRef), "none");
    pad.defaultVelocity = kPadDefaults[i].defaultVelocity;
    pad.chokeGroup = kPadDefaults[i].chokeGroup;
  }
  copyString(kitName_, sizeof(kitName_), "builtin");
}

bool SampleBank::adoptPcm(uint8_t pad, int16_t *pcm, uint32_t frames,
                          uint32_t srcRate, const char *sampleRef) {
  if (pad >= kPadCount || pcm == nullptr || frames == 0 || srcRate == 0) {
    return false;
  }
  freePcm(pad);
  PadSound &slot = pads_[pad];
  slot.pcm = pcm;
  slot.frames = frames;
  slot.srcRate = srcRate;
  slot.baseIncFP = incFP(srcRate, engineRate_);
  copyString(slot.sampleRef, sizeof(slot.sampleRef), sampleRef ? sampleRef : "?");
  return true;
}

void SampleBank::freePcm(uint8_t pad) {
  if (pad >= kPadCount) {
    return;
  }
  if (pads_[pad].pcm != nullptr) {
    free(pads_[pad].pcm);
    pads_[pad].pcm = nullptr;
  }
  pads_[pad].frames = 0;
  pads_[pad].srcRate = 0;
  pads_[pad].baseIncFP = 0;
  copyString(pads_[pad].sampleRef, sizeof(pads_[pad].sampleRef), "none");
}

void SampleBank::freeAll() {
  for (uint8_t i = 0; i < kPadCount; i++) {
    freePcm(i);
  }
}

const PadSound &SampleBank::pad(uint8_t pad0to15) const {
  if (pad0to15 >= kPadCount) {
    pad0to15 = 0;
  }
  return pads_[pad0to15];
}

bool SampleBank::setGain(uint8_t pad, uint8_t gain) {
  if (pad >= kPadCount) {
    return false;
  }
  pads_[pad].gain = gain;
  return true;
}

bool SampleBank::setPitch(uint8_t pad, int8_t semis) {
  if (pad >= kPadCount || semis < -12 || semis > 12) {
    return false;
  }
  pads_[pad].pitchSemis = semis;
  return true;
}

bool SampleBank::setChoke(uint8_t pad, uint8_t group) {
  if (pad >= kPadCount || group > 4) {
    return false;
  }
  pads_[pad].chokeGroup = group;
  return true;
}

void SampleBank::setKitName(const char *name) {
  copyString(kitName_, sizeof(kitName_), name ? name : "?");
}

uint32_t SampleBank::loadedCount() const {
  uint32_t count = 0;
  for (uint8_t i = 0; i < kPadCount; i++) {
    if (pads_[i].pcm != nullptr) {
      count++;
    }
  }
  return count;
}

uint32_t SampleBank::totalBytes() const {
  uint32_t bytes = 0;
  for (uint8_t i = 0; i < kPadCount; i++) {
    bytes += pads_[i].frames * sizeof(int16_t);
  }
  return bytes;
}

String SampleBank::sampleMap() const {
  String out;
  for (uint8_t i = 0; i < kPadCount; i++) {
    if (i > 0) {
      out += "|";
    }
    out += String(i + 1) + ":" + pads_[i].label + "=" + pads_[i].sampleRef;
  }
  return out;
}

int16_t *SampleBank::allocFrames(uint32_t frames) {
  size_t bytes = (size_t)frames * sizeof(int16_t);
#if defined(ARDUINO_ARCH_ESP32) && defined(BOARD_HAS_PSRAM)
  int16_t *pcm = (int16_t *)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);
  if (pcm != nullptr) {
    return pcm;
  }
#endif
  return (int16_t *)malloc(bytes);
}
