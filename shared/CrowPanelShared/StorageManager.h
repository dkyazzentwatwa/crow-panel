#ifndef CROW_PANEL_STORAGE_MANAGER_H
#define CROW_PANEL_STORAGE_MANAGER_H

#include <Arduino.h>

class StorageManager {
 public:
  void begin(const char *scope);
  void incrementEventCount();
  uint32_t eventCount() const;
  String scope() const;

 private:
  String scope_ = "mock";
  uint32_t eventCount_ = 0;
};

#endif
