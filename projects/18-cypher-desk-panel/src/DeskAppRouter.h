#ifndef CYPHER_DESK_APP_ROUTER_H
#define CYPHER_DESK_APP_ROUTER_H

#include "DeskApplication.h"

class DeskAppRouter {
 public:
  bool registerApp(DeskApplication *app);
  bool open(DeskAppId id);
  bool back();
  void home();
  DeskApplication *current() const;
  DeskAppId currentId() const;
  const char *currentTitle() const;
  uint8_t registeredCount() const;
  bool has(DeskAppId id) const;
  void print(Print &out) const;

 private:
  // Was exactly full at 12 when Video was added; 16 leaves room without
  // costing anything but pointer slots.
  static constexpr uint8_t kMaxApps = 16;
  static constexpr uint8_t kHistoryDepth = 8;
  DeskApplication *apps_[kMaxApps] = {};
  uint8_t appCount_ = 0;
  DeskAppId current_ = kDeskAppHome;
  DeskAppId history_[kHistoryDepth] = {};
  uint8_t historyCount_ = 0;
  DeskApplication *find(DeskAppId id) const;
};

#endif
