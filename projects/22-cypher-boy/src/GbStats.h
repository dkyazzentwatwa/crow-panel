#ifndef CYPHER_BOY_STATS_H
#define CYPHER_BOY_STATS_H

#include "../config/ProjectConfig.h"
#include <Arduino.h>

// Per-ROM play time and recency, so the picker reads like a console library
// rather than a directory listing.
//
// Stored as one small CSV at GB_STATS_PATH: `name,seconds,rank`. `rank` is a
// monotonically increasing counter rather than a wall-clock time, because the
// panel has no RTC on this build - a higher rank simply means "played more
// recently", which is all the sort needs.
class GbStats {
 public:
  static const uint8_t kMaxEntries = 32;

  void begin();
  void save() const;

  // Called when a ROM is launched and when it is put down.
  void startSession(const String &romName, uint32_t nowMs);
  void endSession(uint32_t nowMs);

  uint32_t seconds(const String &romName) const;
  uint32_t rank(const String &romName) const;  // 0 = never played
  // "2h 14m" / "18m" / "never"
  static String formatPlayed(uint32_t secs);

 private:
  int8_t find(const String &romName) const;
  int8_t findOrAdd(const String &romName);

  String names_[kMaxEntries];
  uint32_t seconds_[kMaxEntries] = {0};
  uint32_t rank_[kMaxEntries] = {0};
  uint8_t count_ = 0;
  uint32_t nextRank_ = 1;

  int8_t active_ = -1;
  uint32_t sessionStartMs_ = 0;
};

#endif
