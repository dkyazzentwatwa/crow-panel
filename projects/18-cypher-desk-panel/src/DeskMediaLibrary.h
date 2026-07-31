#ifndef CYPHER_DESK_MEDIA_LIBRARY_H
#define CYPHER_DESK_MEDIA_LIBRARY_H

#include "../config/ProjectConfig.h"
#include <Arduino.h>

class DeskStorageService;

// Index of the playable media on the card.
//
// The old Music and Podcast screens listed raw directory entries and printed a
// hardcoded "16 kHz MONO PCM ONLY" caption regardless of what the files
// actually were. This reads each file's header once at index time, so the app
// can show a real duration and a real format - and can say "this one will not
// play, and here is why" instead of failing silently at tap time.
//
// Fixed-size arrays, no heap growth: this is a long-running panel.
class DeskMediaLibrary {
 public:
  static constexpr uint8_t kMaxEntries = 64;

  enum Kind : uint8_t { kAudio, kVideo };

  struct Entry {
    String path;
    String name;      // basename without extension
    String format;    // "44.1 kHz stereo 16-bit", or the reason it is unplayable
    uint32_t bytes = 0;
    uint32_t durationMs = 0;
    bool playable = false;
  };

  // Rescans `directory` unless the card's mount generation is unchanged and
  // the directory is the one already loaded. Returns the entry count.
  uint8_t scan(DeskStorageService *storage, const String &directory, Kind kind);
  // Forces the next scan() of the same directory to re-read the card.
  void invalidate();

  uint8_t count() const { return count_; }
  const Entry &entry(uint8_t index) const { return entries_[index < count_ ? index : 0]; }
  const String &directory() const { return directory_; }
  uint8_t playableCount() const;
  // Index of the nth playable entry, or 0xFF.
  uint8_t playableAt(uint8_t ordinal) const;
  // Next/previous playable entry, wrapping. Returns 0xFF when there are none.
  uint8_t nextPlayable(uint8_t from) const;
  uint8_t previousPlayable(uint8_t from) const;
  // Deterministic shuffle step: walks the playable set in a fixed but
  // non-sequential order, so "shuffle" never needs an RNG and never repeats a
  // track before the set is exhausted.
  uint8_t shuffleNext(uint8_t from, uint32_t seed) const;

  String summary() const;

 private:
  Entry entries_[kMaxEntries];
  uint8_t count_ = 0;
  String directory_;
  uint32_t generation_ = 0xFFFFFFFFu;

  void readAudioHeader(Entry &entry);
  void readVideoHeader(Entry &entry);
};

#endif
