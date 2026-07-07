#include "ScanLog.h"
#include <CrowPanelShared.h>

namespace {
void copyField(char *dest, size_t len, const String &value) {
  value.toCharArray(dest, len);
}
}  // namespace

void ScanLog::recordWifi(const WifiNetworkRecord &record, const char *source) {
  String summary = record.ssid.length() > 0 ? record.ssid : "(hidden)";
  String detail = "ch=" + String(record.channel) + " rssi=" + String(record.rssi) +
                  " auth=" + record.auth + " source=" + String(source);
  push(kWifi, "wifi", summary, detail);
}

void ScanLog::recordBle(const BleAdvertisementRecord &record, const char *source) {
  String summary = record.label.length() > 0 ? record.label : record.address;
  if (summary.length() == 0) summary = "(unnamed)";
  String detail = "addr=" + record.address + " rssi=" + String(record.rssi) +
                  " vendor=" + record.vendor + " source=" + String(source);
  if (record.detail.length() > 0) detail += " note=" + record.detail;
  push(kBle, "ble", summary, detail);
}

void ScanLog::recordQr(const String &url, bool persisted) {
  push(kQr, "qr", "handoff-url", String(persisted ? "persisted " : "volatile ") + url);
}

void ScanLog::recordInfo(const String &category, const String &summary, const String &detail) {
  push(kInfo, category, summary, detail);
}

void ScanLog::print(Stream &out) const {
  if (count_ == 0) {
    out.println(F("[scan-log] empty"));
    return;
  }

  for (uint8_t i = 0; i < count_; ++i) {
    const Entry &entry = entryAt(i);
    out.print(F("[scan-log] t+"));
    out.print(entry.timestampMs / 1000.0, 1);
    out.print(F("s type="));
    out.print(typeName(entry.type));
    out.print(F(" category="));
    out.print(entry.category);
    out.print(F(" summary=\""));
    out.print(entry.summary);
    out.print(F("\" detail=\""));
    out.print(entry.detail);
    out.println(F("\""));
  }
}

uint8_t ScanLog::count() const {
  return count_;
}

uint8_t ScanLog::countType(EntryType type) const {
  uint8_t total = 0;
  for (uint8_t i = 0; i < count_; ++i) {
    if (entryAt(i).type == type) ++total;
  }
  return total;
}

const char *ScanLog::latestSummary() const {
  if (count_ == 0) return "empty";
  uint8_t latest = head_ == 0 ? kCapacity - 1 : head_ - 1;
  return entries_[latest].summary;
}

void ScanLog::push(EntryType type, const String &category, const String &summary,
                   const String &detail) {
  Entry &entry = entries_[head_];
  entry.type = type;
  entry.timestampMs = millis();
  copyField(entry.category, kMaxCategoryLen, category);
  copyField(entry.summary, kMaxSummaryLen, summary);
  copyField(entry.detail, kMaxDetailLen, detail);

  head_ = (head_ + 1) % kCapacity;
  if (count_ < kCapacity) ++count_;

  Logger::info("scan-log", String(typeName(type)) + " " + summary);
}

const ScanLog::Entry &ScanLog::entryAt(uint8_t offset) const {
  uint8_t start = (head_ + kCapacity - count_) % kCapacity;
  return entries_[(start + offset) % kCapacity];
}

const char *ScanLog::typeName(EntryType type) const {
  switch (type) {
    case kWifi:
      return "wifi";
    case kBle:
      return "ble";
    case kQr:
      return "qr";
    case kInfo:
    default:
      return "info";
  }
}
