#include "LoopLibrary.h"

#include "SampleBank.h"

namespace {

LoopLibrary::LoopInfo gLoops[LoopLibrary::kMaxLoops];
LoopLibrary::PackInfo gPacks[LoopLibrary::kMaxPacks];
uint8_t gCount = 0;
uint8_t gPackCount = 0;
bool gScanned = false;

void copyField(char *dst, size_t dstSize, const String &src) {
  size_t n = src.length() < dstSize - 1 ? src.length() : dstSize - 1;
  memcpy(dst, src.c_str(), n);
  dst[n] = '\0';
}

}  // namespace

#if USE_MPC_SD
#include <SD_MMC.h>
#include "WavLoader.h"

namespace {

String nextField(String &line) {
  int tab = line.indexOf('\t');
  if (tab < 0) {
    String all = line;
    line = "";
    return all;
  }
  String field = line.substring(0, tab);
  line = line.substring(tab + 1);
  return field;
}

}  // namespace

namespace LoopLibrary {

// Reads one pack directory's manifest into the flat loop table. Returns how
// many loops it contributed.
uint8_t scanPack(const String &dirName, uint8_t packIndex) {
  String dir = String(CYPHER_TUNE_LOOP_DIR) + "/" + dirName;
  File manifest = SD_MMC.open(dir + "/loops.txt", FILE_READ);
  if (!manifest) {
    return 0;
  }
  uint8_t added = 0;
  while (manifest.available() && gCount < kMaxLoops) {
    String line = manifest.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) {
      continue;
    }
    LoopInfo &slot = gLoops[gCount];
    copyField(slot.name, sizeof(slot.name), nextField(line));
    copyField(slot.title, sizeof(slot.title), nextField(line));
    slot.bpmTenths = (uint16_t)(nextField(line).toFloat() * 10.0f + 0.5f);
    slot.bars = (uint8_t)nextField(line).toInt();
    slot.frames = (uint32_t)nextField(line).toInt();
    slot.pack = packIndex;
    // bars is what the tempo lock divides by, so a malformed row is skipped
    // rather than allowed to produce a zero-length step.
    if (slot.name[0] != '\0' && slot.bars > 0 && slot.frames > 0) {
      if (slot.title[0] == '\0') {
        copyField(slot.title, sizeof(slot.title), String(slot.name));
      }
      gCount++;
      added++;
    }
  }
  manifest.close();
  return added;
}

uint8_t begin() {
  if (gScanned) {
    return gCount;
  }
  gScanned = true;
  gCount = 0;
  gPackCount = 0;
  if (!WavLoader::beginSd()) {
    return 0;
  }
  File root = SD_MMC.open(CYPHER_TUNE_LOOP_DIR);
  if (!root || !root.isDirectory()) {
    return 0;
  }
  File entry = root.openNextFile();
  while (entry && gPackCount < kMaxPacks && gCount < kMaxLoops) {
    if (entry.isDirectory()) {
      const char *path = entry.name();
      const char *leaf = strrchr(path, '/');
      String dirName = leaf ? String(leaf + 1) : String(path);
      entry.close();

      PackInfo &pk = gPacks[gPackCount];
      copyField(pk.name, sizeof(pk.name), dirName);
      pk.firstLoop = gCount;
      uint8_t added = scanPack(dirName, gPackCount);
      if (added > 0) {
        pk.count = added;
        // Display title from pack.txt; fall back to the directory name so a
        // pack dropped in by hand still shows up sensibly.
        File pf = SD_MMC.open(String(CYPHER_TUNE_LOOP_DIR) + "/" + dirName + "/pack.txt",
                              FILE_READ);
        String title;
        if (pf) {
          title = pf.readStringUntil('\n');
          title.trim();
          pf.close();
        }
        copyField(pk.title, sizeof(pk.title), title.length() ? title : dirName);
        gPackCount++;
      }
    } else {
      entry.close();
    }
    entry = root.openNextFile();
  }
  if (entry) {
    entry.close();
  }
  root.close();
  return gCount;
}

uint32_t loadLoop(uint8_t index, int16_t **pcmOut, uint32_t *srcRateOut) {
  if (index >= gCount || pcmOut == nullptr || !WavLoader::beginSd()) {
    return 0;
  }
  const LoopInfo &loop = gLoops[index];
  const char *packDir = loop.pack < gPackCount ? gPacks[loop.pack].name : "";
  String path = String(CYPHER_TUNE_LOOP_DIR) + "/" + packDir + "/" + loop.name + ".wav";
  File file = SD_MMC.open(path, FILE_READ);
  if (!file) {
    return 0;
  }
  // Loops are written by our own tool: canonical 44-byte header, 16-bit mono.
  // Validate the essentials, then read the block whole.
  uint8_t header[44];
  if (file.read(header, 44) != 44 || memcmp(header, "RIFF", 4) != 0 ||
      memcmp(header + 8, "WAVE", 4) != 0 || memcmp(header + 36, "data", 4) != 0) {
    file.close();
    return 0;
  }
  uint16_t channels = (uint16_t)header[22] | ((uint16_t)header[23] << 8);
  // Bytes 24-27 are the sample rate. This was never read - the loader simply
  // assumed the loop matched the engine rate, which held only while both were
  // 22050. The header is authoritative over the catalog's assumption.
  uint32_t rate = (uint32_t)header[24] | ((uint32_t)header[25] << 8) |
                  ((uint32_t)header[26] << 16) | ((uint32_t)header[27] << 24);
  uint16_t bits = (uint16_t)header[34] | ((uint16_t)header[35] << 8);
  uint32_t dataBytes = (uint32_t)header[40] | ((uint32_t)header[41] << 8) |
                       ((uint32_t)header[42] << 16) | ((uint32_t)header[43] << 24);
  if (channels != 1 || bits != 16 || dataBytes < 4 || rate < 8000 || rate > 48000) {
    file.close();
    return 0;
  }
  gLoops[index].srcRate = rate;
  if (srcRateOut != nullptr) {
    *srcRateOut = rate;
  }
  uint32_t frames = dataBytes / 2;
  int16_t *pcm = SampleBank::allocFrames(frames);
  if (pcm == nullptr) {
    file.close();
    return 0;
  }
  // A megabyte-plus in one call would stall; read in chunks so the loop
  // context stays responsive while the audio task keeps playing.
  const uint32_t kChunk = 8192;
  uint8_t *dst = (uint8_t *)pcm;
  uint32_t left = dataBytes;
  while (left > 0) {
    uint32_t want = left < kChunk ? left : kChunk;
    int got = file.read(dst, want);
    if (got <= 0) {
      free(pcm);
      file.close();
      return 0;
    }
    dst += got;
    left -= (uint32_t)got;
  }
  file.close();
  *pcmOut = pcm;
  return frames;
}

}  // namespace LoopLibrary

#else  // no SD support compiled in

namespace LoopLibrary {

uint8_t begin() { return 0; }
uint32_t loadLoop(uint8_t, int16_t **, uint32_t *) { return 0; }

}  // namespace LoopLibrary

#endif

namespace LoopLibrary {

uint8_t count() { return gCount; }

const LoopInfo &info(uint8_t index) {
  static const LoopInfo kEmpty;
  return index < gCount ? gLoops[index] : kEmpty;
}

uint8_t packCount() { return gPackCount; }

const PackInfo &pack(uint8_t index) {
  static const PackInfo kEmpty;
  return index < gPackCount ? gPacks[index] : kEmpty;
}

int8_t loopInPack(uint8_t packIndex, uint8_t slot) {
  if (packIndex >= gPackCount || slot >= gPacks[packIndex].count) {
    return -1;
  }
  return (int8_t)(gPacks[packIndex].firstLoop + slot);
}

int8_t indexOfName(const char *name) {
  if (name == nullptr) {
    return -1;
  }
  for (uint8_t i = 0; i < gCount; i++) {
    if (strcasecmp(gLoops[i].name, name) == 0) {
      return (int8_t)i;
    }
  }
  return -1;
}

uint32_t stepFramesFor(const LoopInfo &loop) {
  uint32_t steps = (uint32_t)loop.bars * 16;
  if (steps == 0 || loop.frames == 0) {
    return 0;
  }
  return loop.frames / steps;
}

uint32_t stepFramesForEngine(const LoopInfo &loop, uint32_t srcRate) {
  if (srcRate == 0) {
    srcRate = loop.srcRate != 0 ? loop.srcRate : kSourceRateDefault;
  }
  uint32_t steps = (uint32_t)loop.bars * 16;
  if (steps == 0 || loop.frames == 0) {
    return 0;
  }
  // Convert the whole loop first and divide once: dividing per step and then
  // scaling would round away up to a step's worth of drift over 16 bars.
  uint64_t engineFrames = ((uint64_t)loop.frames * CYPHER_TUNE_ENGINE_RATE) / srcRate;
  return (uint32_t)(engineFrames / steps);
}

}  // namespace LoopLibrary
