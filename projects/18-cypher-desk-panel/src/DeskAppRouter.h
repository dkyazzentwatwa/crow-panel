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
  void print(Print &out) const;

 private:
  static constexpr uint8_t kMaxApps = 12;
  static constexpr uint8_t kHistoryDepth = 8;
  DeskApplication *apps_[kMaxApps] = {};
  uint8_t appCount_ = 0;
  DeskAppId current_ = kDeskAppHome;
  DeskAppId history_[kHistoryDepth] = {};
  uint8_t historyCount_ = 0;
  DeskApplication *find(DeskAppId id) const;
};

#endif
