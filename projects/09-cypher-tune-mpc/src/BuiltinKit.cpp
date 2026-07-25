#include "BuiltinKit.h"

#include <string.h>
#include "KitSamples.h"

namespace BuiltinKit {

bool loadPad(SampleBank &bank, uint8_t pad) {
  if (pad >= kBuiltinKitPads || pad >= SampleBank::kPadCount) {
    return false;
  }
  const KitSample &src = kBuiltinKit[pad];
  if (src.pcm == nullptr || src.frames == 0) {
    return false;
  }
  int16_t *dst = SampleBank::allocFrames(src.frames);
  if (dst == nullptr) {
    return false;
  }
  // Straight copy out of flash; both sides are int16 in CPU byte order.
  memcpy(dst, src.pcm, (size_t)src.frames * sizeof(int16_t));
  if (!bank.adoptPcm(pad, dst, src.frames, src.rate, src.ref)) {
    free(dst);
    return false;
  }
  bank.setGain(pad, src.gain);
  bank.setChoke(pad, src.chokeGroup);
  return true;
}

uint8_t loadAll(SampleBank &bank) {
  uint8_t loaded = 0;
  for (uint8_t pad = 0; pad < SampleBank::kPadCount; pad++) {
    if (loadPad(bank, pad)) {
      loaded++;
    }
  }
  bank.setKitName("cypher");
  return loaded;
}

}  // namespace BuiltinKit
