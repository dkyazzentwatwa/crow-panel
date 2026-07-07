#ifndef CYPHERDRIVE_SCAN_LOG_H
#define CYPHERDRIVE_SCAN_LOG_H

#include "WirelessTypes.h"

class ScanLog {
 public:
  enum EntryType : uint8_t {
    kWifi,
    kBle,
    kQr,
    kInfo
  };

  static const uint8_t kCapacity = 16;
  static const uint8_t kMaxCategoryLen = 16;
  static const uint8_t kMaxSummaryLen = 64;
  static const uint8_t kMaxDetailLen = 112;

  void recordWifi(const WifiNetworkRecord &record, const char *source);
  void recordBle(const BleAdvertisementRecord &record, const char *source);
  void recordQr(const String &url, bool persisted);
  void recordInfo(const String &category, const String &summary, const String &detail);

  void print(Stream &out) const;
  uint8_t count() const;
  uint8_t countType(EntryType type) const;
  const char *latestSummary() const;

 private:
  struct Entry {
    EntryType type = kInfo;
    unsigned long timestampMs = 0;
    char category[kMaxCategoryLen] = {0};
    char summary[kMaxSummaryLen] = {0};
    char detail[kMaxDetailLen] = {0};
  };

  void push(EntryType type, const String &category, const String &summary,
            const String &detail);
  const Entry &entryAt(uint8_t offset) const;
  const char *typeName(EntryType type) const;

  Entry entries_[kCapacity];
  uint8_t head_ = 0;
  uint8_t count_ = 0;
};

#endif
