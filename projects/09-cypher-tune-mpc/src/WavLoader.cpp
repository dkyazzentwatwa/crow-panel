#include "WavLoader.h"

#include "BuiltinKit.h"

#if USE_MPC_SD
#include <SD_MMC.h>

namespace {

bool gSdMounted = false;

uint16_t readU16(File &file) {
  uint8_t b[2] = {};
  if (file.read(b, 2) != 2) return 0;
  return (uint16_t)b[0] | ((uint16_t)b[1] << 8);
}

uint32_t readU32(File &file) {
  uint8_t b[4] = {};
  if (file.read(b, 4) != 4) return 0;
  return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) |
         ((uint32_t)b[3] << 24);
}

// RIFF chunk walk (same shape as project 18's DeskAudio parser, but the
// sample rate is captured instead of enforced - the voice resampler absorbs
// it). Returns frames loaded, 0 on any structural problem.
uint32_t loadWavFile(const String &path, int16_t **pcmOut, uint32_t *rateOut) {
  File file = SD_MMC.open(path, FILE_READ);
  if (!file) {
    return 0;
  }
  char riff[4], wave[4];
  if (file.readBytes(riff, 4) != 4 || readU32(file) == 0 ||
      file.readBytes(wave, 4) != 4 || memcmp(riff, "RIFF", 4) != 0 ||
      memcmp(wave, "WAVE", 4) != 0) {
    file.close();
    return 0;
  }
  bool formatOk = false;
  uint32_t rate = 0;
  while (file.available()) {
    char id[4];
    if (file.readBytes(id, 4) != 4) {
      break;
    }
    uint32_t size = readU32(file);
    if (memcmp(id, "fmt ", 4) == 0) {
      uint16_t format = readU16(file);
      uint16_t channels = readU16(file);
      rate = readU32(file);
      file.seek(file.position() + 6);
      uint16_t bits = readU16(file);
      if (size > 16) {
        file.seek(file.position() + size - 16);
      }
      formatOk = format == 1 && channels == 1 && bits == 16 && rate >= 8000 &&
                 rate <= 48000;
    } else if (memcmp(id, "data", 4) == 0) {
      if (!formatOk) {
        break;
      }
      uint32_t frames = size / 2;
      if (frames > CYPHER_TUNE_MAX_SAMPLE_FRAMES) {
        frames = CYPHER_TUNE_MAX_SAMPLE_FRAMES;
      }
      if (frames < 2) {
        break;
      }
      int16_t *pcm = SampleBank::allocFrames(frames);
      if (pcm == nullptr) {
        break;
      }
      // WAV data is little-endian int16, same as the CPU: read straight in.
      size_t want = (size_t)frames * 2;
      if (file.read((uint8_t *)pcm, want) != want) {
        free(pcm);
        break;
      }
      file.close();
      *pcmOut = pcm;
      *rateOut = rate;
      return frames;
    } else {
      file.seek(file.position() + size);
    }
  }
  file.close();
  return 0;
}

}  // namespace

namespace WavLoader {

bool beginSd() {
  if (gSdMounted) {
    return true;
  }
  bool alreadyMounted = SD_MMC.cardType() != CARD_NONE;
  if (!alreadyMounted && !SD_MMC.begin("/sdcard", CYPHER_TUNE_SDMMC_1BIT != 0)) {
    return false;
  }
  gSdMounted = SD_MMC.cardType() != CARD_NONE;
  return gSdMounted;
}

bool sdReady() {
  return gSdMounted;
}

String listKits() {
  if (!beginSd()) {
    return String();
  }
  File dir = SD_MMC.open(CYPHER_TUNE_KIT_DIR);
  if (!dir || !dir.isDirectory()) {
    return String();
  }
  String out;
  File entry = dir.openNextFile();
  while (entry) {
    if (entry.isDirectory()) {
      const char *name = entry.name();
      const char *slash = strrchr(name, '/');
      if (slash != nullptr) {
        name = slash + 1;
      }
      if (out.length()) {
        out += " ";
      }
      out += name;
    }
    entry.close();
    entry = dir.openNextFile();
  }
  dir.close();
  return out;
}

uint8_t loadKit(const char *name, SampleBank &staging, String &status) {
  if (!beginSd()) {
    status = "SD not mounted";
    return 0;
  }
  String dirPath = String(CYPHER_TUNE_KIT_DIR) + "/" + name;
  File dir = SD_MMC.open(dirPath);
  bool dirOk = dir && dir.isDirectory();
  if (dir) {
    dir.close();
  }
  if (!dirOk) {
    status = String("kit dir missing: ") + dirPath;
    return 0;
  }

  staging.beginDefaults(staging.engineRate());
  uint8_t fromSd = 0;
  for (uint8_t pad = 0; pad < SampleBank::kPadCount; pad++) {
    char fileName[16];
    snprintf(fileName, sizeof(fileName), "pad%02u.wav", pad + 1);
    String path = dirPath + "/" + fileName;
    int16_t *pcm = nullptr;
    uint32_t rate = 0;
    uint32_t frames = loadWavFile(path, &pcm, &rate);
    if (frames != 0) {
      String ref = String("sd:") + name + "/" + fileName;
      if (staging.adoptPcm(pad, pcm, frames, rate, ref.c_str())) {
        fromSd++;
      } else {
        free(pcm);
      }
    } else {
      BuiltinKit::loadPad(staging, pad);  // builtin fallback for the pad
    }
  }
  staging.setKitName(name);
  status = String("kit ") + name + ": " + String(fromSd) + "/16 from SD, " +
           String(SampleBank::kPadCount - fromSd) + " builtin fallback";
  return fromSd;
}

}  // namespace WavLoader

#else  // USE_MPC_SD == 0: compile-safe stubs

namespace WavLoader {

bool beginSd() { return false; }
bool sdReady() { return false; }
String listKits() { return String(); }

uint8_t loadKit(const char *, SampleBank &, String &status) {
  status = "SD kits disabled (build with -DUSE_MPC_SD=1)";
  return 0;
}

}  // namespace WavLoader

#endif
