#ifndef CROW_PANEL_EVENT_LOG_H
#define CROW_PANEL_EVENT_LOG_H

#include <Arduino.h>

// Fixed-capacity, timestamped ring buffer of recent app events.
//
// Capacity is compile-time fixed and entries use fixed char arrays, so the
// log never grows the heap as events churn. Messages longer than
// kMaxMessageLen - 1 are truncated. This is the repo's exemplar of the
// storage policy: long-lived storage gets fixed buffers, transient
// formatting keeps Arduino String.
class EventLog {
 public:
  static const uint8_t kCapacity = 16;
  static const uint8_t kMaxMessageLen = 96;  // bytes, including terminator

  // Stores the entry and echoes it via Logger::info("event-log", ...).
  void add(const String &message);

  const char *latest() const;  // "" when empty
  uint8_t size() const;
  void clear();

  // Oldest to newest: "[t+123.4s] message"
  void printHistory(Stream &out) const;

 private:
  struct Entry {
    unsigned long timestampMs = 0;
    char message[kMaxMessageLen] = {0};
  };

  Entry entries_[kCapacity];
  uint8_t head_ = 0;   // next slot to overwrite
  uint8_t count_ = 0;  // saturates at kCapacity
};

#endif
