#include "EventLog.h"
#include "Logger.h"

void EventLog::add(const String &message) {
  Entry &entry = entries_[head_];
  entry.timestampMs = millis();
  strlcpy(entry.message, message.c_str(), kMaxMessageLen);
  head_ = (head_ + 1) % kCapacity;
  if (count_ < kCapacity) {
    count_++;
  }
  Logger::info("event-log", message);
}

const char *EventLog::latest() const {
  if (count_ == 0) {
    return "";
  }
  uint8_t newest = (head_ + kCapacity - 1) % kCapacity;
  return entries_[newest].message;
}

uint8_t EventLog::size() const {
  return count_;
}

void EventLog::clear() {
  head_ = 0;
  count_ = 0;
}

void EventLog::printHistory(Stream &out) const {
  if (count_ == 0) {
    out.println(F("[history] empty"));
    return;
  }
  out.print(F("[history] last "));
  out.print(count_);
  out.println(F(" events, oldest first:"));
  for (uint8_t i = 0; i < count_; i++) {
    uint8_t slot = (head_ + kCapacity - count_ + i) % kCapacity;
    out.print(F("  [t+"));
    out.print(entries_[slot].timestampMs / 1000);
    out.print(F("."));
    out.print((entries_[slot].timestampMs % 1000) / 100);
    out.print(F("s] "));
    out.println(entries_[slot].message);
  }
}
