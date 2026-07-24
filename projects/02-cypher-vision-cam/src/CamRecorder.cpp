// Stills and MJPEG/AVI clips to SD, via the P4 hardware JPEG encoder.
// COMPILE-VERIFIED on esp32:esp32:esp32p4 (core 3.3.8). NOT HARDWARE-VERIFIED -
// nothing has been written to a real card and no clip has been played back.
//
// The AVI writing here is deliberately plain: a RIFF/AVI header, one '00dc'
// chunk per JPEG frame, and an 'idx1' index appended at close. That is the
// format every player already understands, and writing it by hand is far less
// work than making a real container library fit in an Arduino sketch.

#include "CamRecorder.h"

#if USE_CAM_SD

#include <SD_MMC.h>
#include <esp_heap_caps.h>

namespace {

constexpr const char *kMediaDir = "/DCIM";

File gClipFile;

// AVI index, built in PSRAM while recording and flushed at close. Bounded on
// purpose: an unbounded index would grow until the heap gave out mid-clip,
// which is a far worse failure than a clip that stops cleanly and says why.
struct AviIndexEntry {
  uint32_t offset;  // from the 'movi' fourcc
  uint32_t size;    // chunk payload bytes
};
AviIndexEntry *gIndex = nullptr;
uint32_t gIndexCap = 0;
uint32_t gIndexCount = 0;
uint16_t gClipW = 0;
uint16_t gClipH = 0;
char gClipPath[40] = {0};

// Bytes written so far. Tracked by hand rather than read back from File::size()
// - for a file open for writing, size() reflects what has been flushed, not
// what has been buffered, so using it to compute chunk offsets would silently
// corrupt the index.
uint32_t gClipBytes = 0;

// About 30 minutes at 10 fps. 16 bytes an entry, so ~288 KB of PSRAM.
constexpr uint32_t kIndexCapacity = 18000;

// --- AVI layout ------------------------------------------------------------
//
// Offsets of the fields that can only be filled in once the clip closes. These
// are absolute byte positions in the header written by writeAviHeader_, and
// they are constants rather than captured positions because getting them wrong
// produces a file that looks fine until a player tries to seek in it.
//
//   0   'RIFF'                    12  'LIST'      24  'avih'    32  usec/frame
//   4   riff size        <-patch  16  hdrl size   28  56        36  max B/s  <-patch
//   8   'AVI '                    20  'hdrl'                    48  frames   <-patch
//   88  'LIST' strl               100 'strh'                    140 length   <-patch
//   164 'strf'                    212 'LIST'      216 movi size <-patch
//   220 'movi'                    224 first frame chunk
constexpr uint32_t kOffsetRiffSize = 4;
constexpr uint32_t kOffsetMaxBytesPerSec = 36;
constexpr uint32_t kOffsetTotalFrames = 48;
constexpr uint32_t kOffsetStreamLength = 140;
constexpr uint32_t kOffsetMoviListSize = 216;
constexpr uint32_t kMoviFourccPos = 220;
constexpr uint32_t kHeaderBytes = 224;

// hdrl LIST payload: 'hdrl'(4) + avih chunk(8+56) + strl LIST(8 + 116) = 192.
constexpr uint32_t kHdrlListSize = 4 + (8 + 56) + (8 + 116);
// strl LIST payload: 'strl'(4) + strh chunk(8+56) + strf chunk(8+40) = 116.
constexpr uint32_t kStrlListSize = 4 + (8 + 56) + (8 + 40);

// These caught a real bug during development: the hdrl size was written 4 bytes
// short, which produces a file most players still open (they rescan on failure)
// but whose header is malformed. Checking the arithmetic at compile time costs
// nothing and makes that class of error impossible to reintroduce.
static_assert(kStrlListSize == 116, "strl LIST payload must be 116 bytes");
static_assert(kHdrlListSize == 192, "hdrl LIST payload must be 192 bytes");
// Header ends at: 12 (RIFF+AVI ) + 8 + kHdrlListSize + 8 (movi LIST) + 4 ('movi')
static_assert(12 + 8 + kHdrlListSize + 8 + 4 == kHeaderBytes,
              "kHeaderBytes must match the header written by writeAviHeader_");
static_assert(kMoviFourccPos + 4 == kHeaderBytes,
              "the first frame chunk must start immediately after 'movi'");
static_assert(kOffsetMoviListSize + 4 == kMoviFourccPos,
              "the movi LIST size field must immediately precede its fourcc");

// --- little-endian writers -------------------------------------------------
// AVI is little-endian and so is the P4, but writing bytes explicitly keeps the
// layout checkable against the spec and immune to struct-padding surprises.
// Each writer advances gClipBytes so offsets stay exact.

void put32(File &f, uint32_t v) {
  uint8_t b[4] = {(uint8_t)(v), (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24)};
  f.write(b, 4);
  gClipBytes += 4;
}
void put16(File &f, uint16_t v) {
  uint8_t b[2] = {(uint8_t)(v), (uint8_t)(v >> 8)};
  f.write(b, 2);
  gClipBytes += 2;
}
void putTag(File &f, const char *tag) {
  f.write((const uint8_t *)tag, 4);
  gClipBytes += 4;
}

}  // namespace

// ---------------------------------------------------------------------------

bool CamRecorder::mountStorage() {
  // Already mounted (another subsystem got there first) is success, not an
  // error - SD_MMC is a singleton and double-mounting it fails.
  const bool alreadyMounted = SD_MMC.cardType() != CARD_NONE;
  storageReady_ = alreadyMounted || SD_MMC.begin("/sdcard", VISIONCAM_SDMMC_1BIT != 0);
  if (storageReady_ && SD_MMC.cardType() == CARD_NONE) storageReady_ = false;

  if (!storageReady_) {
    lastError_ = "no SD card";
    Logger::warn("recorder", "SD_MMC did not mount; capture is unavailable");
    return false;
  }

  if (!SD_MMC.exists(kMediaDir)) SD_MMC.mkdir(kMediaDir);

  stillIndex_ = nextIndexFor_("CAM_", ".JPG");
  clipIndex_ = nextIndexFor_("VID_", ".AVI");
  lastError_ = "";
  Logger::info("recorder", String("SD mounted, next still #") + String(stillIndex_) +
                               " next clip #" + String(clipIndex_));
  return true;
}

// Scans /DCIM for the highest existing NNNNN in prefix+NNNNN+extension and
// returns the next free one, so a card carried between sessions never has its
// files overwritten.
uint32_t CamRecorder::nextIndexFor_(const char *prefix, const char *extension) {
  uint32_t highest = 0;
  File dir = SD_MMC.open(kMediaDir);
  if (!dir || !dir.isDirectory()) return 1;

  const size_t prefixLen = strlen(prefix);
  for (File entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
    String name = entry.name();
    const int slash = name.lastIndexOf('/');
    if (slash >= 0) name = name.substring(slash + 1);
    entry.close();

    if (!name.startsWith(prefix) || !name.endsWith(extension)) continue;
    const String digits = name.substring(prefixLen, name.length() - strlen(extension));
    const uint32_t value = (uint32_t)digits.toInt();
    if (value > highest) highest = value;
  }
  dir.close();
  return highest + 1;
}

bool CamRecorder::begin(JpegEncoder *encoder) {
  encoder_ = encoder;
  if (!storageReady_) {
    lastError_ = "no SD card; capture disabled";
    return false;
  }
  if (encoder_ == nullptr || !encoder_->ready()) {
    lastError_ = "no JPEG encoder";
    Logger::warn("recorder", lastError_);
    return false;
  }

  gIndex = (AviIndexEntry *)heap_caps_calloc(kIndexCapacity, sizeof(AviIndexEntry),
                                             MALLOC_CAP_SPIRAM);
  gIndexCap = (gIndex != nullptr) ? kIndexCapacity : 0;
  if (gIndexCap == 0) {
    lastError_ = "no memory for the clip index";
    Logger::warn("recorder", lastError_);
    return false;
  }

  lastError_ = "";
  Logger::info("recorder", String("ready: stills ") + String(Sc2336Sensor::kWidth) + "x" +
                               String(Sc2336Sensor::kHeight) + ", clips " +
                               String(VISIONCAM_REC_WIDTH) + "x" +
                               String(VISIONCAM_REC_HEIGHT) + " q" +
                               String(VISIONCAM_REC_QUALITY) + " @" +
                               String(VISIONCAM_REC_FPS) + "fps (targets)");
  return true;
}

bool CamRecorder::captureStill(const CrowCamera::Frame &frame) {
  if (!storageReady_) {
    lastError_ = "no SD card";
    return false;
  }
  if (encoder_ == nullptr || !encoder_->ready()) {
    lastError_ = "no JPEG encoder";
    return false;
  }

  // Stills go at full sensor resolution and high quality - the size budget that
  // shapes clip recording does not apply to a single file.
  const size_t bytes = encoder_->encode(frame, frame.width, frame.height, 90);
  if (bytes == 0) return false;

  char path[40];
  snprintf(path, sizeof(path), "%s/CAM_%05lu.JPG", kMediaDir, (unsigned long)stillIndex_);

  File file = SD_MMC.open(path, FILE_WRITE);
  if (!file) {
    lastError_ = "could not create the still file";
    return false;
  }
  const size_t written = file.write(encoder_->data(), bytes);
  file.close();

  if (written != bytes) {
    lastError_ = "short write; card may be full";
    return false;
  }

  stillIndex_++;
  lastError_ = "";
  Logger::info("recorder", String("still ") + path + " (" + String((uint32_t)bytes) + " B)");
  return true;
}

// --- AVI -------------------------------------------------------------------

bool CamRecorder::writeAviHeader_(uint16_t width, uint16_t height, uint8_t fps) {
  // RIFF
  putTag(gClipFile, "RIFF");
  put32(gClipFile, 0);  // total size - 8, patched at close
  putTag(gClipFile, "AVI ");

  // LIST hdrl
  putTag(gClipFile, "LIST");
  put32(gClipFile, kHdrlListSize);
  putTag(gClipFile, "hdrl");

  // avih - main header (56 bytes)
  putTag(gClipFile, "avih");
  put32(gClipFile, 56);
  put32(gClipFile, 1000000UL / (fps ? fps : 1));  // microseconds per frame
  put32(gClipFile, 0);                            // max bytes/sec, patched at close
  put32(gClipFile, 0);                            // padding granularity
  put32(gClipFile, 0x10);                         // AVIF_HASINDEX
  put32(gClipFile, 0);                            // total frames, patched at close
  put32(gClipFile, 0);                            // initial frames
  put32(gClipFile, 1);                            // stream count
  put32(gClipFile, 0);                            // suggested buffer size
  put32(gClipFile, width);
  put32(gClipFile, height);
  for (int i = 0; i < 4; i++) put32(gClipFile, 0);  // reserved

  // LIST strl
  putTag(gClipFile, "LIST");
  put32(gClipFile, kStrlListSize);
  putTag(gClipFile, "strl");

  // strh - stream header (56 bytes)
  putTag(gClipFile, "strh");
  put32(gClipFile, 56);
  putTag(gClipFile, "vids");
  putTag(gClipFile, "MJPG");
  put32(gClipFile, 0);    // flags
  put16(gClipFile, 0);    // priority
  put16(gClipFile, 0);    // language
  put32(gClipFile, 0);    // initial frames
  put32(gClipFile, 1);    // scale
  put32(gClipFile, fps);  // rate; rate/scale = fps
  put32(gClipFile, 0);    // start
  put32(gClipFile, 0);    // length in frames, patched at close
  put32(gClipFile, 0);    // suggested buffer size
  put32(gClipFile, 0);    // quality
  put32(gClipFile, 0);    // sample size
  put16(gClipFile, 0);    // rcFrame left
  put16(gClipFile, 0);    // rcFrame top
  put16(gClipFile, width);
  put16(gClipFile, height);

  // strf - BITMAPINFOHEADER (40 bytes)
  putTag(gClipFile, "strf");
  put32(gClipFile, 40);
  put32(gClipFile, 40);  // biSize
  put32(gClipFile, width);
  put32(gClipFile, height);
  put16(gClipFile, 1);   // planes
  put16(gClipFile, 24);  // bit count
  putTag(gClipFile, "MJPG");
  put32(gClipFile, (uint32_t)width * height * 3);
  put32(gClipFile, 0);
  put32(gClipFile, 0);
  put32(gClipFile, 0);
  put32(gClipFile, 0);

  // LIST movi - the frames themselves follow
  putTag(gClipFile, "LIST");
  put32(gClipFile, 0);  // size, patched at close
  putTag(gClipFile, "movi");

  // If this fires, the header layout above no longer matches the patch offsets
  // and every one of them would land in the wrong field.
  if (gClipBytes != kHeaderBytes) {
    Logger::warn("recorder", String("AVI header is ") + String(gClipBytes) +
                                 " bytes, expected " + String(kHeaderBytes) +
                                 " - clip index will be wrong");
    return false;
  }
  return true;
}

bool CamRecorder::startClip() {
  if (!storageReady_) {
    lastError_ = "no SD card";
    return false;
  }
  if (recording_) return true;
  if (encoder_ == nullptr || !encoder_->ready()) {
    lastError_ = "no JPEG encoder";
    return false;
  }

  if (gIndex == nullptr || gIndexCap == 0) {
    lastError_ = "no memory for the clip index";
    return false;
  }

  snprintf(gClipPath, sizeof(gClipPath), "%s/VID_%05lu.AVI", kMediaDir,
           (unsigned long)clipIndex_);
  gClipFile = SD_MMC.open(gClipPath, FILE_WRITE);
  if (!gClipFile) {
    lastError_ = "could not create the clip file";
    return false;
  }

  gClipW = VISIONCAM_REC_WIDTH;
  gClipH = VISIONCAM_REC_HEIGHT;
  gIndexCount = 0;
  gClipBytes = 0;
  clipFrames_ = 0;
  droppedFrames_ = 0;
  clipStartMs_ = millis();
  lastFrameMs_ = 0;

  if (!writeAviHeader_(gClipW, gClipH, VISIONCAM_REC_FPS)) {
    gClipFile.close();
    lastError_ = "AVI header layout check failed";
    return false;
  }

  recording_ = true;
  lastError_ = "";
  Logger::info("recorder", String("recording ") + gClipPath);
  return true;
}

void CamRecorder::offerFrame(const CrowCamera::Frame &frame) {
  if (!recording_) return;

  // Rate-limit to the configured record fps. Frames arriving between slots are
  // simply not wanted, so they are NOT counted as drops - conflating the two
  // would make a healthy recording look like a failing one.
  const uint32_t now = millis();
  const uint32_t interval = 1000UL / (VISIONCAM_REC_FPS ? VISIONCAM_REC_FPS : 1);
  if (lastFrameMs_ != 0 && (now - lastFrameMs_) < interval) return;

  if (gIndexCount >= gIndexCap) {
    // Index full. Stop cleanly rather than recording frames that will not be
    // seekable, and say so.
    Logger::warn("recorder", "clip index full; stopping recording");
    lastError_ = "clip reached the maximum length";
    stopClip();
    return;
  }

  const size_t bytes = encoder_->encode(frame, gClipW, gClipH, VISIONCAM_REC_QUALITY);
  if (bytes == 0) {
    droppedFrames_++;
    return;
  }

  const uint32_t chunkPos = gClipBytes;
  putTag(gClipFile, "00dc");
  put32(gClipFile, (uint32_t)bytes);
  const size_t written = gClipFile.write(encoder_->data(), bytes);
  gClipBytes += (uint32_t)written;
  // RIFF chunks are word-aligned; an odd-length payload needs a pad byte.
  if (bytes & 1) {
    gClipFile.write((uint8_t)0);
    gClipBytes++;
  }

  if (written != bytes) {
    // The card could not keep up. THIS is a real drop - it is the number that
    // tells you to lower the resolution, quality or frame rate.
    droppedFrames_++;
    lastError_ = "short write; card cannot keep up";
    return;
  }

  // Index offsets are measured from the 'movi' fourcc, which is the convention
  // every common player accepts.
  gIndex[gIndexCount].offset = chunkPos - kMoviFourccPos;
  gIndex[gIndexCount].size = (uint32_t)bytes;
  gIndexCount++;
  clipFrames_++;
  lastFrameMs_ = now;
}

bool CamRecorder::finalizeAvi_() {
  // idx1: one 16-byte entry per frame. Without it a player can show the clip
  // but cannot seek in it, and some refuse to open it at all.
  const uint32_t idxPos = gClipBytes;
  putTag(gClipFile, "idx1");
  put32(gClipFile, gIndexCount * 16);
  for (uint32_t i = 0; i < gIndexCount; i++) {
    putTag(gClipFile, "00dc");
    put32(gClipFile, 0x10);  // AVIIF_KEYFRAME - every MJPEG frame is a keyframe
    put32(gClipFile, gIndex[i].offset);
    put32(gClipFile, gIndex[i].size);
  }

  const uint32_t fileSize = gClipBytes;
  const uint32_t elapsedMs = millis() - clipStartMs_;
  const uint32_t bytesPerSec =
      (elapsedMs > 0) ? (uint32_t)((uint64_t)fileSize * 1000ULL / elapsedMs) : 0;

  gClipFile.flush();
  gClipFile.close();

  // Reopen to backfill the five header fields that were unknown while
  // recording. This is a separate pass on purpose: the clip file is opened
  // with FILE_WRITE ("w"), which is a truncating write-only mode - seeking
  // backwards in it to patch would be relying on undefined behaviour. "r+"
  // is the mode that means "open existing for update".
  File patch = SD_MMC.open(gClipPath, "r+");
  if (!patch) {
    lastError_ = "clip written but header could not be patched";
    Logger::warn("recorder", lastError_);
    return false;
  }

  auto patch32 = [&patch](uint32_t position, uint32_t value) {
    uint8_t b[4] = {(uint8_t)(value), (uint8_t)(value >> 8), (uint8_t)(value >> 16),
                    (uint8_t)(value >> 24)};
    patch.seek(position);
    patch.write(b, 4);
  };

  patch32(kOffsetRiffSize, fileSize - 8);
  patch32(kOffsetMaxBytesPerSec, bytesPerSec);
  patch32(kOffsetTotalFrames, gIndexCount);
  patch32(kOffsetStreamLength, gIndexCount);
  // The movi LIST size spans from its 'movi' fourcc to the start of idx1.
  patch32(kOffsetMoviListSize, idxPos - kMoviFourccPos);

  patch.flush();
  patch.close();
  return true;
}

bool CamRecorder::stopClip() {
  if (!recording_) return true;
  recording_ = false;
  finalizeAvi_();
  clipIndex_++;
  Logger::info("recorder", String("clip closed: ") + String(clipFrames_) + " frames, " +
                               String(droppedFrames_) + " dropped, " +
                               String(measuredClipFps(), 1) + " fps measured");
  return true;
}

uint32_t CamRecorder::clipElapsedSec() const {
  if (!recording_ && clipFrames_ == 0) return 0;
  return (millis() - clipStartMs_) / 1000;
}

float CamRecorder::measuredClipFps() const {
  const uint32_t elapsedMs = millis() - clipStartMs_;
  if (elapsedMs == 0 || clipFrames_ == 0) return 0.0f;
  return (float)clipFrames_ * 1000.0f / (float)elapsedMs;
}

uint8_t CamRecorder::refreshMediaList() {
  mediaCount_ = 0;
  mediaTruncated_ = false;
  if (!storageReady_) return 0;

  File dir = SD_MMC.open(kMediaDir);
  if (!dir || !dir.isDirectory()) return 0;

  for (File entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
    String name = entry.name();
    const int slash = name.lastIndexOf('/');
    if (slash >= 0) name = name.substring(slash + 1);
    const uint32_t size = entry.size();
    entry.close();

    if (!name.endsWith(".JPG") && !name.endsWith(".AVI")) continue;
    if (mediaCount_ >= kMaxListed) {
      mediaTruncated_ = true;
      break;
    }
    MediaEntry &slot = media_[mediaCount_++];
    strncpy(slot.name, name.c_str(), sizeof(slot.name) - 1);
    slot.name[sizeof(slot.name) - 1] = '\0';
    slot.bytes = size;
    slot.isVideo = name.endsWith(".AVI");
  }
  dir.close();
  return mediaCount_;
}

uint64_t CamRecorder::freeBytes() const {
  if (!storageReady_) return 0;
  return SD_MMC.totalBytes() - SD_MMC.usedBytes();
}

void CamRecorder::printStatus(Print &out) const {
  out.print(F("[recorder] "));
  if (!storageReady_) {
    out.print(F("no storage ("));
    out.print(lastError_);
    out.println(')');
    return;
  }
  out.print(recording_ ? F("RECORDING") : F("idle"));
  out.print(F(" stills_next="));
  out.print(stillIndex_);
  out.print(F(" clips_next="));
  out.print(clipIndex_);
  out.print(F(" free_mb="));
  out.println((uint32_t)(freeBytes() / (1024ULL * 1024ULL)));
  if (recording_ || clipFrames_ > 0) {
    out.print(F("[recorder] clip frames="));
    out.print(clipFrames_);
    out.print(F(" dropped="));
    out.print(droppedFrames_);
    out.print(F(" measured_fps="));
    out.print(measuredClipFps(), 1);
    out.print(F(" target_fps="));
    out.println(VISIONCAM_REC_FPS);
  }
}

#else  // USE_CAM_SD == 0

bool CamRecorder::mountStorage() {
  lastError_ = "built without USE_CAM_SD";
  return false;
}
bool CamRecorder::begin(JpegEncoder *) {
  lastError_ = "built without USE_CAM_SD";
  return false;
}
bool CamRecorder::captureStill(const CrowCamera::Frame &) { return false; }
bool CamRecorder::startClip() { return false; }
bool CamRecorder::stopClip() { return true; }
void CamRecorder::offerFrame(const CrowCamera::Frame &) {}
uint32_t CamRecorder::clipElapsedSec() const { return 0; }
float CamRecorder::measuredClipFps() const { return 0.0f; }
uint8_t CamRecorder::refreshMediaList() { return 0; }
uint64_t CamRecorder::freeBytes() const { return 0; }

void CamRecorder::printStatus(Print &out) const {
  out.println(F("[recorder] disabled (build with -DUSE_CAM_SD=1)"));
}

#endif
