#include "QrStateStore.h"
#include <CrowPanelShared.h>

void QrStateStore::begin(const char *scope, const String &fallbackUrl) {
  url_ = fallbackUrl;
#if USE_QR_PERSISTENCE
  ready_ = prefs_.begin(scope, false);
  if (ready_) {
    url_ = prefs_.getString("url", fallbackUrl);
    Logger::info("qr", "Preferences persistence enabled");
  } else {
    Logger::warn("qr", "Preferences begin failed; QR state is volatile");
  }
#else
  (void)scope;
  Logger::info("qr", "volatile QR state enabled");
#endif
}

bool QrStateStore::setUrl(const String &url, Stream &out) {
  String next = url;
  next.trim();
  if (next.length() == 0) {
    out.println(F("[qr] rejected empty URL"));
    return false;
  }
  if (looksSensitive(next)) {
    out.println(F("[qr] rejected URL with credential-like query fields"));
    out.println(F("[qr] keep Wi-Fi passwords, API keys, and tokens out of QR persistence"));
    return false;
  }

  url_ = next;
#if USE_QR_PERSISTENCE
  if (ready_ && prefs_.putString("url", url_) == 0) {
    out.println(F("[qr] Preferences write failed; value kept in RAM for this boot"));
    return false;
  }
#endif
  return true;
}

const String &QrStateStore::url() const {
  return url_;
}

bool QrStateStore::persistenceEnabled() const {
#if USE_QR_PERSISTENCE
  return ready_;
#else
  return false;
#endif
}

bool QrStateStore::looksSensitive(const String &value) const {
  String lower = value;
  lower.toLowerCase();
  return lower.indexOf("password=") >= 0 ||
         lower.indexOf("passwd=") >= 0 ||
         lower.indexOf("pass=") >= 0 ||
         lower.indexOf("pwd=") >= 0 ||
         lower.indexOf("ssid=") >= 0 ||
         lower.indexOf("token=") >= 0 ||
         lower.indexOf("api_key=") >= 0 ||
         lower.indexOf("apikey=") >= 0;
}
