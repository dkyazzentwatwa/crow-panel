#include "StorageManager.h"
#include "Logger.h"

void StorageManager::begin(const char *scope) {
  scope_ = scope;
  eventCount_ = 0;
  Logger::info("storage", "mock storage ready for " + scope_);
}

void StorageManager::incrementEventCount() {
  eventCount_++;
}

uint32_t StorageManager::eventCount() const {
  return eventCount_;
}

String StorageManager::scope() const {
  return scope_;
}
