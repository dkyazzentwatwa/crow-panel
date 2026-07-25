#include "KeySoundPacks.h"

#include <stdlib.h>
#include <string.h>
#include <strings.h>  // strcasecmp / strncasecmp

// ---------------------------------------------------------------------------
// Pure part: clip-set bookkeeping, the fallback resolution order, the filename
// map and the RIFF header parser. No SD, no I2S, no Arduino state - compiled in
// every build (the linker drops it when nothing references it) so the host
// harness in test/ can exercise it against real converted WAVs.
// ---------------------------------------------------------------------------

namespace {

uint16_t rd16(const uint8_t *p) {
  return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

uint32_t rd32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}

// Basename with any extension removed, upper/lower case preserved. Returns false
// when nothing usable is left (a dotfile, or a name longer than a slot name).
bool clipBaseName(const char *fileName, char *out, size_t outLen) {
  if (fileName == nullptr || outLen == 0) return false;
  const char *slash = strrchr(fileName, '/');
  if (slash != nullptr) fileName = slash + 1;
  size_t n = 0;
  while (fileName[n] != '\0' && fileName[n] != '.') {
    if (n + 1 >= outLen) return false;  // too long to be one of our names
    out[n] = fileName[n];
    ++n;
  }
  out[n] = '\0';
  return n != 0;
}

}  // namespace

void KeySoundPacks::ClipSet::freeAll() {
  for (uint8_t i = 0; i < kSlotCount; ++i) {
    if (slots[i].pcm != nullptr) free(slots[i].pcm);
    slots[i].pcm = nullptr;
    slots[i].frames = 0;
  }
  present = 0;
  clips = 0;
  bytes = 0;
  name[0] = '\0';
}

void KeySoundPacks::ClipSet::setName(const char *n) {
  size_t i = 0;
  if (n != nullptr) {
    while (n[i] != '\0' && i + 1 < kMaxNameLen) {
      name[i] = n[i];
      ++i;
    }
  }
  name[i] = '\0';
}

// Press order: the class clip (BACKSPACE/ENTER/SPACE) -> this row's
// GENERIC_R<row> -> GENERIC_R0 -> nothing (synthesized fallback). The last two
// steps are what make a ragged pack like kbsim's mxblue (GENERIC_R0..R4 only)
// sound right on every key.
uint8_t KeySoundPacks::resolvePressSlot(uint16_t present, uint8_t keyClass,
                                        uint8_t row) {
  uint8_t classSlot = kSlotNone;
  if (keyClass == kClassBackspace) classSlot = kPressBackspace;
  else if (keyClass == kClassEnter) classSlot = kPressEnter;
  else if (keyClass == kClassSpace) classSlot = kPressSpace;
  if (classSlot != kSlotNone && (present & (uint16_t)(1u << classSlot)) != 0) {
    return classSlot;
  }
  if (row >= kPressRows) row = (uint8_t)(kPressRows - 1);
  const uint8_t rowSlot = (uint8_t)(kPressR0 + row);
  if ((present & (uint16_t)(1u << rowSlot)) != 0) return rowSlot;
  if ((present & (uint16_t)(1u << kPressR0)) != 0) return kPressR0;
  return kSlotNone;
}

// Release order: the class clip -> GENERIC -> nothing. A pack with no release
// clip at all simply has no clack, which is a legitimate sound design.
uint8_t KeySoundPacks::resolveReleaseSlot(uint16_t present, uint8_t keyClass) {
  uint8_t classSlot = kSlotNone;
  if (keyClass == kClassBackspace) classSlot = kReleaseBackspace;
  else if (keyClass == kClassEnter) classSlot = kReleaseEnter;
  else if (keyClass == kClassSpace) classSlot = kReleaseSpace;
  if (classSlot != kSlotNone && (present & (uint16_t)(1u << classSlot)) != 0) {
    return classSlot;
  }
  if ((present & (uint16_t)(1u << kReleaseGeneric)) != 0) return kReleaseGeneric;
  return kSlotNone;
}

uint8_t KeySoundPacks::slotForFile(bool releasePhase, const char *fileName) {
  char base[kMaxNameLen];
  if (!clipBaseName(fileName, base, sizeof(base))) return kSlotNone;

  if (strcasecmp(base, "BACKSPACE") == 0) {
    return releasePhase ? kReleaseBackspace : kPressBackspace;
  }
  if (strcasecmp(base, "ENTER") == 0) {
    return releasePhase ? kReleaseEnter : kPressEnter;
  }
  if (strcasecmp(base, "SPACE") == 0) {
    return releasePhase ? kReleaseSpace : kPressSpace;
  }
  if (releasePhase) {
    // Release has one generic clip. Anything else that merely STARTS with
    // GENERIC (kbsim's bluealps ships release/GENERIC_long.wav) is ignored.
    return strcasecmp(base, "GENERIC") == 0 ? kReleaseGeneric : kSlotNone;
  }
  if (strncasecmp(base, "GENERIC_R", 9) == 0 && base[9] >= '0' &&
      base[9] < (char)('0' + kPressRows) && base[10] == '\0') {
    return (uint8_t)(kPressR0 + (uint8_t)(base[9] - '0'));
  }
  return kSlotNone;
}

bool KeySoundPacks::parseWavHeader(const uint8_t *head, uint32_t len,
                                  WavInfo &out, String &why) {
  out = WavInfo();
  if (head == nullptr || len < 44) {
    why = "shorter than a WAV header";
    return false;
  }
  if (memcmp(head, "RIFF", 4) != 0 || memcmp(head + 8, "WAVE", 4) != 0) {
    why = "not RIFF/WAVE";
    return false;
  }

  bool haveFmt = false;
  uint32_t pos = 12;  // first chunk header, past "RIFF<size>WAVE"
  while (pos + 8 <= len) {
    const uint8_t *id = head + pos;
    const uint32_t size = rd32(head + pos + 4);
    const uint32_t body = pos + 8;

    if (memcmp(id, "fmt ", 4) == 0) {
      if (size < 16 || body + 16 > len) {
        why = "truncated fmt chunk";
        return false;
      }
      const uint16_t format = rd16(head + body);
      out.channels = rd16(head + body + 2);
      out.rate = rd32(head + body + 4);
      out.bits = rd16(head + body + 14);
      if (format != 1) {
        why = String("not PCM (format ") + String((uint32_t)format) + ")";
        return false;
      }
      haveFmt = true;
    } else if (memcmp(id, "data", 4) == 0) {
      if (!haveFmt) {
        why = "data chunk before fmt";
        return false;
      }
      // The engine plays clips 1:1 - no resampling on the keypress path - so
      // anything but 16-bit mono at the engine rate would be the wrong pitch.
      if (out.channels != 1) {
        why = String("not mono (") + String((uint32_t)out.channels) + " ch)";
        return false;
      }
      if (out.bits != 16) {
        why = String("not 16-bit (") + String((uint32_t)out.bits) + ")";
        return false;
      }
      if (out.rate != (uint32_t)CYPHER_KEYS_AUDIO_SAMPLE_RATE) {
        why = String("rate ") + String(out.rate) + " != " +
              String((uint32_t)CYPHER_KEYS_AUDIO_SAMPLE_RATE);
        return false;
      }
      if (size < 4) {
        why = "empty data chunk";
        return false;
      }
      out.dataOffset = body;
      out.dataBytes = size;
      return true;
    }

    // RIFF chunks are word aligned: an odd size carries one pad byte. Stop once
    // the next chunk would sit past what was read (ffmpeg puts a LIST/INFO
    // chunk before data, which is why this walk exists at all).
    const uint32_t skip = size + (size & 1u);
    if (skip > len - body) break;
    pos = body + skip;
  }
  why = haveFmt ? "no data chunk near the start of the file" : "no fmt chunk";
  return false;
}

// ---------------------------------------------------------------------------
// SD-backed part.
// ---------------------------------------------------------------------------

// NOTE: this is a plain #if, deliberately NOT `#if USE_CYPHER_KEYS_SD &&
// __has_include(<SD_MMC.h>)`. arduino-cli discovers libraries by preprocessing
// the sources before SD_MMC's include path exists, so an __has_include guard
// evaluates false during that scan, the #include is never seen, and SD_MMC is
// silently left out of the link - a green build that cannot read a card. Same
// trap projects 09, 20 and 22 call out; verify with
// `ls <build-path>/libraries/` after a key-audio-sd build.
#if USE_CYPHER_KEYS_SD

#include <SD_MMC.h>

namespace {

bool gSdMounted = false;

// A clip longer than this is not a key click, so refuse it rather than let one
// stray file eat PSRAM. 1.0 s at the engine rate; the real packs top out at
// 0.29 s.
constexpr uint32_t kMaxClipFrames = CYPHER_KEYS_AUDIO_SAMPLE_RATE;

// Header staging. File scope rather than a local so a 512-byte buffer never
// lands on the Arduino loop task's stack, and it costs nothing without the flag.
uint8_t sHead[512];

// Reads one already-open WAV into a fresh PSRAM buffer. `why` explains a reject.
bool loadClip(File &file, KeySoundPacks::Clip &clip, String &why) {
  const uint32_t size = file.size();
  const uint32_t headLen = size < sizeof(sHead) ? size : (uint32_t)sizeof(sHead);
  if (!file.seek(0) || file.read(sHead, headLen) != (size_t)headLen) {
    why = "header read failed";
    return false;
  }
  KeySoundPacks::WavInfo info;
  if (!KeySoundPacks::parseWavHeader(sHead, headLen, info, why)) return false;

  // Trust the file over the header: a truncated copy declares more than it has.
  const uint32_t avail = size > info.dataOffset ? size - info.dataOffset : 0;
  uint32_t frames = (info.dataBytes < avail ? info.dataBytes : avail) / 2;
  if (frames > kMaxClipFrames) frames = kMaxClipFrames;
  if (frames < 2) {
    why = "no samples";
    return false;
  }

  const size_t want = (size_t)frames * sizeof(int16_t);
  int16_t *pcm = (int16_t *)ps_malloc(want);
  if (pcm == nullptr) pcm = (int16_t *)malloc(want);
  if (pcm == nullptr) {
    why = "out of memory";
    return false;
  }
  // WAV payload is little-endian int16, same as the CPU: read straight in.
  if (!file.seek(info.dataOffset) || file.read((uint8_t *)pcm, want) != want) {
    free(pcm);
    why = "data read failed";
    return false;
  }
  clip.pcm = pcm;
  clip.frames = frames;
  return true;
}

// Loads every recognized clip in <dir>/press or <dir>/release into `out`.
// Returns false only when the folder itself is missing.
bool loadPhase(const String &packDir, bool releasePhase,
               KeySoundPacks::ClipSet &out, uint8_t &rejected,
               String &firstReject) {
  const String phaseDir = packDir + (releasePhase ? "/release" : "/press");
  File dir = SD_MMC.open(phaseDir);
  const bool ok = dir && dir.isDirectory();
  if (!ok) {
    if (dir) dir.close();
    return false;
  }
  File entry = dir.openNextFile();
  while (entry) {
    if (!entry.isDirectory()) {
      const uint8_t slot = KeySoundPacks::slotForFile(releasePhase, entry.name());
      if (slot != KeySoundPacks::kSlotNone && !out.has(slot)) {
        String why;
        if (loadClip(entry, out.slots[slot], why)) {
          out.present |= (uint16_t)(1u << slot);
          out.clips++;
          out.bytes += out.slots[slot].frames * sizeof(int16_t);
        } else {
          rejected++;
          if (firstReject.length() == 0) {
            const char *base = strrchr(entry.name(), '/');
            firstReject = String(base != nullptr ? base + 1 : entry.name()) +
                          ": " + why;
          }
        }
      }
    }
    entry.close();
    entry = dir.openNextFile();
  }
  dir.close();
  return true;
}

}  // namespace

bool KeySoundPacks::beginSd() {
  if (gSdMounted) return true;
  // SD_MMC is a singleton; double-mounting it fails, so honour an existing mount
  // (another feature, or an earlier call) instead of re-begin()ing it.
  const bool alreadyMounted = SD_MMC.cardType() != CARD_NONE;
  if (!alreadyMounted &&
      !SD_MMC.begin("/sdcard", CYPHER_KEYS_SDMMC_1BIT != 0)) {
    return false;
  }
  gSdMounted = SD_MMC.cardType() != CARD_NONE;
  return gSdMounted;
}

bool KeySoundPacks::sdReady() { return gSdMounted; }

uint8_t KeySoundPacks::listPacks(String *out, uint8_t maxPacks) {
  if (out == nullptr || maxPacks == 0 || !beginSd()) return 0;
  File dir = SD_MMC.open(CYPHER_KEYS_SOUNDS_DIR);
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    return 0;
  }
  uint8_t count = 0;
  File entry = dir.openNextFile();
  while (entry) {
    if (entry.isDirectory() && count < maxPacks) {
      const char *name = entry.name();
      const char *slash = strrchr(name, '/');
      if (slash != nullptr) name = slash + 1;
      // Skip "." / ".." and macOS dot-directories so the UI never offers one.
      if (name[0] != '\0' && name[0] != '.') out[count++] = name;
    }
    entry.close();
    entry = dir.openNextFile();
  }
  dir.close();

  // Sorted, so the settings row and `sound next` cycle in a stable order no
  // matter what order the filesystem hands the directory back in.
  for (uint8_t i = 1; i < count; ++i) {
    const String key = out[i];
    int16_t j = (int16_t)i - 1;
    while (j >= 0 && strcasecmp(out[j].c_str(), key.c_str()) > 0) {
      out[j + 1] = out[j];
      --j;
    }
    out[j + 1] = key;
  }
  return count;
}

bool KeySoundPacks::loadPack(const char *name, ClipSet &out, String &status) {
  const uint32_t startMs = millis();
  if (name == nullptr || name[0] == '\0') {
    status = "no pack name given";
    return false;
  }
  if (!beginSd()) {
    status = String("pack ") + name + ": no SD card mounted";
    return false;
  }
  const String packDir = String(CYPHER_KEYS_SOUNDS_DIR) + "/" + name;

  uint8_t rejected = 0;
  String firstReject;
  const bool havePress = loadPhase(packDir, false, out, rejected, firstReject);
  const bool haveRelease = loadPhase(packDir, true, out, rejected, firstReject);
  if (!havePress && !haveRelease) {
    status = String("pack ") + name + ": not found under " + packDir;
    return false;
  }

  status = String("pack ") + name + ": " + String((uint32_t)out.clips) + "/" +
           String((uint32_t)kSlotCount) + " clips, " + String(out.bytes / 1024) +
           " KB, " + String(millis() - startMs) + " ms";
  if (rejected != 0) {
    status += String("  skipped ") + String((uint32_t)rejected) + " (" +
              firstReject + ")";
  }
  if (!out.has(kPressR0)) {
    status += "  - no usable press/GENERIC_R0.wav, cannot be used";
    return false;
  }
  if (!out.has(kReleaseGeneric)) status += "  (no release clip)";
  return true;
}

#else  // USE_CYPHER_KEYS_SD == 0 (or no SD_MMC): compile-safe stubs

bool KeySoundPacks::beginSd() { return false; }
bool KeySoundPacks::sdReady() { return false; }

uint8_t KeySoundPacks::listPacks(String *out, uint8_t maxPacks) {
  (void)out;
  (void)maxPacks;
  return 0;
}

bool KeySoundPacks::loadPack(const char *name, ClipSet &out, String &status) {
  (void)name;
  (void)out;
  status = "SD sound packs disabled (build with -DUSE_CYPHER_KEYS_SD=1)";
  return false;
}

#endif
