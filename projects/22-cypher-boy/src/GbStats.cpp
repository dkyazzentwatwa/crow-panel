#include "GbStats.h"

#include <CrowPanelShared.h>
#include <stdio.h>

int8_t GbStats::find(const String &romName) const {
  for (uint8_t i = 0; i < count_; i++) {
    if (names_[i] == romName) return (int8_t)i;
  }
  return -1;
}

int8_t GbStats::findOrAdd(const String &romName) {
  const int8_t existing = find(romName);
  if (existing >= 0) return existing;
  if (count_ >= kMaxEntries) return -1;
  names_[count_] = romName;
  seconds_[count_] = 0;
  rank_[count_] = 0;
  return (int8_t)count_++;
}

void GbStats::begin() {
#if USE_GB_SD
  FILE *f = fopen(GB_STATS_PATH, "r");
  if (!f) return;
  char line[128];
  while (fgets(line, sizeof(line), f) && count_ < kMaxEntries) {
    // name,seconds,rank - the name may not contain a comma, which is true of
    // every ROM filename we accept.
    char *c2 = strrchr(line, ',');
    if (!c2) continue;
    *c2 = 0;
    char *c1 = strrchr(line, ',');
    if (!c1) continue;
    *c1 = 0;
    names_[count_] = line;
    seconds_[count_] = (uint32_t)strtoul(c1 + 1, nullptr, 10);
    rank_[count_] = (uint32_t)strtoul(c2 + 1, nullptr, 10);
    if (rank_[count_] >= nextRank_) nextRank_ = rank_[count_] + 1;
    count_++;
  }
  fclose(f);
  Logger::info("stats", String("loaded ") + count_ + " play records");
#endif
}

void GbStats::save() const {
#if USE_GB_SD
  FILE *f = fopen(GB_STATS_PATH, "w");
  if (!f) {
    Logger::error("stats", "could not write play records");
    return;
  }
  for (uint8_t i = 0; i < count_; i++) {
    fprintf(f, "%s,%lu,%lu\n", names_[i].c_str(), (unsigned long)seconds_[i],
            (unsigned long)rank_[i]);
  }
  fclose(f);
#endif
}

void GbStats::startSession(const String &romName, uint32_t nowMs) {
  endSession(nowMs);
  active_ = findOrAdd(romName);
  sessionStartMs_ = nowMs;
  if (active_ >= 0) {
    rank_[active_] = nextRank_++;
    save();
  }
}

void GbStats::endSession(uint32_t nowMs) {
  if (active_ < 0) return;
  // millis() wraps after ~49 days; unsigned subtraction still yields the right
  // elapsed value across the wrap.
  const uint32_t elapsed = (nowMs - sessionStartMs_) / 1000;
  if (elapsed > 0) {
    seconds_[active_] += elapsed;
    save();
  }
  active_ = -1;
}

uint32_t GbStats::seconds(const String &romName) const {
  const int8_t i = find(romName);
  return i >= 0 ? seconds_[i] : 0;
}

uint32_t GbStats::rank(const String &romName) const {
  const int8_t i = find(romName);
  return i >= 0 ? rank_[i] : 0;
}

String GbStats::formatPlayed(uint32_t secs) {
  if (secs == 0) return String("never played");
  if (secs < 60) return String(secs) + "s";
  const uint32_t mins = secs / 60;
  if (mins < 60) return String(mins) + "m";
  return String(mins / 60) + "h " + String(mins % 60) + "m";
}
