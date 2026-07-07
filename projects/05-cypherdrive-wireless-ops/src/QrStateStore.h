#ifndef CYPHERDRIVE_QR_STATE_STORE_H
#define CYPHERDRIVE_QR_STATE_STORE_H

#include "../config/ProjectConfig.h"
#include <Arduino.h>

#if USE_QR_PERSISTENCE
#include <Preferences.h>
#endif

class QrStateStore {
 public:
  void begin(const char *scope, const String &fallbackUrl);
  bool setUrl(const String &url, Stream &out);
  const String &url() const;
  bool persistenceEnabled() const;

 private:
  bool looksSensitive(const String &value) const;

  String url_;
#if USE_QR_PERSISTENCE
  Preferences prefs_;
  bool ready_ = false;
#endif
};

#endif
