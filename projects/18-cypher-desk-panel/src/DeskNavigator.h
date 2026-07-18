#ifndef CYPHER_DESK_PANEL_NAVIGATOR_H
#define CYPHER_DESK_PANEL_NAVIGATOR_H

#include "DeskTypes.h"

class DeskNavigator {
 public:
  DeskPage active() const { return active_; }
  DeskPage previous() const { return historyCount_ ? history_[historyCount_ - 1] : kDeskPageDesk; }
  bool modal() const { return modal_; }
  void reset(DeskPage page = kDeskPageDesk) { active_ = page; historyCount_ = 0; modal_ = false; }
  void go(DeskPage page, bool modal = false) {
    if (page != active_ && historyCount_ < 8) history_[historyCount_++] = active_;
    active_ = page;
    modal_ = modal;
  }
  void dock(DeskPage page) { historyCount_ = 0; active_ = page; modal_ = false; }
  void back() {
    active_ = historyCount_ ? history_[--historyCount_] : kDeskPageDesk;
    modal_ = false;
  }

 private:
  DeskPage active_ = kDeskPageDesk;
  DeskPage history_[8] = {};
  uint8_t historyCount_ = 0;
  bool modal_ = false;
};

#endif
