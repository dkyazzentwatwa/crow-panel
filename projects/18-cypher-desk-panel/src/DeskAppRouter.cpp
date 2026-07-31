#include "DeskAppRouter.h"

bool DeskAppRouter::registerApp(DeskApplication *app) {
  if (app == nullptr || appCount_ >= kMaxApps || find(app->id()) != nullptr) return false;
  apps_[appCount_++] = app;
  return true;
}

bool DeskAppRouter::has(DeskAppId id) const { return find(id) != nullptr; }

DeskApplication *DeskAppRouter::find(DeskAppId id) const {
  for (uint8_t i = 0; i < appCount_; ++i) if (apps_[i]->id() == id) return apps_[i];
  return nullptr;
}

bool DeskAppRouter::open(DeskAppId id) {
  if (id == kDeskAppHome) { home(); return true; }
  DeskApplication *next = find(id);
  if (next == nullptr) return false;
  DeskApplication *active = current();
  if (active != nullptr) active->onExit();
  if (current_ != id && historyCount_ < kHistoryDepth) history_[historyCount_++] = current_;
  current_ = id;
  next->onEnter();
  return true;
}

bool DeskAppRouter::back() {
  DeskApplication *active = current();
  if (active != nullptr && active->handleBack()) return true;
  if (active != nullptr) active->onExit();
  current_ = historyCount_ ? history_[--historyCount_] : kDeskAppHome;
  active = current();
  if (active != nullptr) active->onEnter();
  return true;
}

void DeskAppRouter::home() {
  DeskApplication *active = current();
  if (active != nullptr) active->onExit();
  current_ = kDeskAppHome;
  historyCount_ = 0;
}

DeskApplication *DeskAppRouter::current() const { return find(current_); }
DeskAppId DeskAppRouter::currentId() const { return current_; }
const char *DeskAppRouter::currentTitle() const {
  DeskApplication *app = current();
  return app == nullptr ? "Home" : app->title();
}
uint8_t DeskAppRouter::registeredCount() const { return appCount_; }
void DeskAppRouter::print(Print &out) const {
  out.print(F("[router] current=")); out.print(currentTitle());
  out.print(F(" apps=")); out.print(appCount_);
  out.print(F(" history=")); out.println(historyCount_);
  for (uint8_t i = 0; i < appCount_; ++i) {
    out.print(F("[router] id=")); out.print(apps_[i]->id());
    out.print(F(" title=")); out.println(apps_[i]->title());
  }
}
