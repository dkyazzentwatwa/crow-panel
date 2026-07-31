#include "DeskMediaLibrary.h"

#include "DeskSystemServices.h"
#include "DeskWavReader.h"

#if USE_CYPHER_DESK_SD
#include <SD_MMC.h>
#endif

namespace {

String baseName(const String &path) {
  const int slash = path.lastIndexOf('/');
  return slash >= 0 ? path.substring(slash + 1) : path;
}

String stripExtension(const String &name) {
  const int dot = name.lastIndexOf('.');
  return dot > 0 ? name.substring(0, dot) : name;
}

bool hasSuffix(const String &name, const char *extension) {
  String lower = name;
  lower.toLowerCase();
  return lower.endsWith(extension);
}

}  // namespace

void DeskMediaLibrary::invalidate() { generation_ = 0xFFFFFFFFu; }

uint8_t DeskMediaLibrary::scan(DeskStorageService *storage, const String &directory, Kind kind) {
  if (storage == nullptr) {
    count_ = 0;
    return 0;
  }
  const uint32_t generation = storage->mountGeneration();
  if (generation == generation_ && directory == directory_) return count_;
  generation_ = generation;
  directory_ = directory;
  count_ = 0;

#if USE_CYPHER_DESK_SD
  if (!storage->mounted()) return 0;
  File folder = SD_MMC.open(directory);
  if (!folder || !folder.isDirectory()) return 0;
  File file = folder.openNextFile();
  while (file && count_ < kMaxEntries) {
    if (!file.isDirectory()) {
      const String name = baseName(String(file.name()));
      const bool wanted = kind == kAudio ? hasSuffix(name, ".wav")
                                         : (hasSuffix(name, ".avi") || hasSuffix(name, ".mjpg"));
      if (wanted && !name.startsWith(".")) {
        Entry &entry = entries_[count_];
        entry.path = directory + "/" + name;
        entry.name = stripExtension(name);
        entry.bytes = file.size();
        entry.durationMs = 0;
        entry.playable = false;
        entry.format = "";
        ++count_;
      }
    }
    file = folder.openNextFile();
  }
  folder.close();

  // Header pass, separate from the directory walk so the folder handle is
  // already closed - some cards get unhappy about a nested open during
  // openNextFile().
  for (uint8_t i = 0; i < count_; ++i) {
    if (kind == kAudio) readAudioHeader(entries_[i]);
    else readVideoHeader(entries_[i]);
  }
#else
  (void)kind;
#endif
  return count_;
}

void DeskMediaLibrary::readAudioHeader(Entry &entry) {
#if USE_CYPHER_DESK_SD
  File file = SD_MMC.open(entry.path, FILE_READ);
  if (!file) {
    entry.format = "cannot open";
    return;
  }
  DeskWavFileSource source(file);
  DeskWavFormat format;
  String reason;
  if (DeskWav::parse(source, format, reason)) {
    entry.format = format.describe();
    entry.durationMs = format.durationMs();
    entry.playable = true;
  } else {
    entry.format = reason;  // shown in the list, so a bad file explains itself
    entry.playable = false;
  }
  file.close();
#else
  (void)entry;
#endif
}

void DeskMediaLibrary::readVideoHeader(Entry &entry) {
  // Filled in by the video player in the phase that adds it; until then a clip
  // is listed with its size and no claim about whether it will play.
  entry.format = "AVI";
  entry.playable = false;
}

uint8_t DeskMediaLibrary::playableCount() const {
  uint8_t total = 0;
  for (uint8_t i = 0; i < count_; ++i) {
    if (entries_[i].playable) ++total;
  }
  return total;
}

uint8_t DeskMediaLibrary::playableAt(uint8_t ordinal) const {
  uint8_t seen = 0;
  for (uint8_t i = 0; i < count_; ++i) {
    if (!entries_[i].playable) continue;
    if (seen == ordinal) return i;
    ++seen;
  }
  return 0xFF;
}

uint8_t DeskMediaLibrary::nextPlayable(uint8_t from) const {
  if (count_ == 0) return 0xFF;
  for (uint8_t step = 1; step <= count_; ++step) {
    const uint8_t index = static_cast<uint8_t>((from + step) % count_);
    if (entries_[index].playable) return index;
  }
  return 0xFF;
}

uint8_t DeskMediaLibrary::previousPlayable(uint8_t from) const {
  if (count_ == 0) return 0xFF;
  for (uint8_t step = 1; step <= count_; ++step) {
    const uint8_t index = static_cast<uint8_t>((from + count_ - step) % count_);
    if (entries_[index].playable) return index;
  }
  return 0xFF;
}

uint8_t DeskMediaLibrary::shuffleNext(uint8_t from, uint32_t seed) const {
  const uint8_t playable = playableCount();
  if (playable == 0) return 0xFF;
  if (playable == 1) return playableAt(0);
  // Walk the playable set with a stride that is coprime with its size, so the
  // order is scrambled but every track is visited exactly once per cycle. No
  // RNG, no repeat-before-exhausted, and it is reproducible for the tests.
  uint8_t stride = static_cast<uint8_t>((seed % (playable - 1)) + 1);
  while (playable % stride == 0 && stride > 1) --stride;

  uint8_t ordinal = 0;
  for (uint8_t i = 0; i < playable; ++i) {
    if (playableAt(i) == from) {
      ordinal = i;
      break;
    }
  }
  return playableAt(static_cast<uint8_t>((ordinal + stride) % playable));
}

String DeskMediaLibrary::summary() const {
  if (count_ == 0) return "no files";
  const uint8_t playable = playableCount();
  String text = String(count_) + (count_ == 1 ? " file" : " files");
  if (playable != count_) text += ", " + String(playable) + " playable";
  return text;
}
