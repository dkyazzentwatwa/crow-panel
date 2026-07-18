#ifndef CYPHER_DESK_EVENT_BUS_H
#define CYPHER_DESK_EVENT_BUS_H

#include <Arduino.h>

enum DeskEventType : uint8_t {
  kDeskEventInfo,
  kDeskEventStorage,
  kDeskEventWifi,
  kDeskEventRecording,
  kDeskEventAlarm,
  kDeskEventRecovery
};

struct DeskEvent {
  DeskEventType type = kDeskEventInfo;
  uint32_t atMs = 0;
  String message;
};

class DeskEventBus {
 public:
  static constexpr uint8_t kCapacity = 16;

  void publish(DeskEventType type, const String &message) {
    events_[write_] = {type, millis(), message};
    write_ = (write_ + 1) % kCapacity;
    if (count_ < kCapacity) ++count_;
  }
  uint8_t count() const { return count_; }
  DeskEvent recent(uint8_t age = 0) const {
    if (age >= count_) return {};
    int16_t index = static_cast<int16_t>(write_) - 1 - age;
    while (index < 0) index += kCapacity;
    return events_[index];
  }
  void print(Print &out) const {
    for (uint8_t age = count_; age > 0; --age) {
      DeskEvent event = recent(age - 1);
      out.print(F("[os-event] ms="));
      out.print(event.atMs);
      out.print(F(" type="));
      out.print(event.type);
      out.print(F(" message="));
      out.println(event.message);
    }
  }

 private:
  DeskEvent events_[kCapacity];
  uint8_t write_ = 0;
  uint8_t count_ = 0;
};

#endif
