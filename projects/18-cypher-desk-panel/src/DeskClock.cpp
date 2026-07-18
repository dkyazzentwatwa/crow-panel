#include "DeskClock.h"
#include "DeskSystemServices.h"

#include <time.h>

namespace {

bool parseDate(const String &value, tm &out) {
  if (value.length() != 10 || value[4] != '-' || value[7] != '-') return false;
  memset(&out, 0, sizeof(out));
  out.tm_year = value.substring(0, 4).toInt() - 1900;
  out.tm_mon = value.substring(5, 7).toInt() - 1;
  out.tm_mday = value.substring(8, 10).toInt();
  out.tm_hour = 12;
  out.tm_isdst = -1;
  return out.tm_year >= 120 && out.tm_mon >= 0 && out.tm_mon < 12 &&
         out.tm_mday > 0 && out.tm_mday < 32;
}

String formatDate(const tm &value, const char *format) {
  char text[48];
  if (strftime(text, sizeof(text), format, &value) == 0) return "";
  return String(text);
}

}  // namespace

void DeskClock::begin(DeskSettings *settings, DeskWifiService *wifi) {
  settings_ = settings;
  wifi_ = wifi;
  if (settings_ != nullptr && settings_->savedDate().length() == 10) {
    date_ = settings_->savedDate();
  }
  requestSync();
}

void DeskClock::requestSync() {
  syncRequested_ = true;
  ntpConfigured_ = false;
  syncStartedMs_ = millis();
#if USE_WIFI
  if (wifi_ == nullptr) {
    status_ = "time service unavailable";
  } else if (!wifi_->connected() && settings_ != nullptr && settings_->wifiSsid().length()) {
    wifi_->connect(settings_->wifiSsid(), settings_->wifiPassword(), true);
    status_ = "Wi-Fi requested for time";
  } else {
    status_ = "time sync queued";
  }
#else
  status_ = "offline saved date";
#endif
}

void DeskClock::tick() {
#if USE_WIFI
  if (!syncRequested_ || settings_ == nullptr || wifi_ == nullptr) return;
  if (settings_->wifiSsid().isEmpty() && wifi_->savedCount() == 0) {
    status_ = "Wi-Fi not configured";
    syncRequested_ = false;
    return;
  }
  if (!wifi_->connected()) {
    status_ = "connecting for time";
    if (millis() - syncStartedMs_ > 18000) {
      status_ = "time sync unavailable";
      syncRequested_ = false;
    }
    return;
  }
  if (millis() - lastPollMs_ < 250) return;
  lastPollMs_ = millis();
  if (!ntpConfigured_) {
    configTzTime(CYPHER_DESK_TIMEZONE, "pool.ntp.org", "time.nist.gov");
    ntpConfigured_ = true;
  }
  tm now = {};
  if (getLocalTime(&now, 20) && now.tm_year >= 124) {
    acceptSystemTime();
    syncRequested_ = false;
    return;
  }
  status_ = "waiting for network time";
  if (millis() - syncStartedMs_ > 15000) {
    status_ = "time sync unavailable";
    syncRequested_ = false;
  }
#endif
}

void DeskClock::acceptSystemTime() {
  tm now = {};
  if (!getLocalTime(&now, 20)) return;
  date_ = formatDate(now, "%Y-%m-%d");
  source_ = kDeskDateNtp;
  status_ = "NTP time synced";
  if (settings_ != nullptr) settings_->setSavedDate(date_);
}

void DeskClock::adjustDay(int delta) {
  tm value = {};
  if (!parseDate(date_, value)) return;
  value.tm_mday += delta;
  time_t epoch = mktime(&value);
  if (epoch < 0) return;
  localtime_r(&epoch, &value);
  date_ = formatDate(value, "%Y-%m-%d");
  source_ = kDeskDateSaved;
  status_ = "date awaiting confirmation";
}

void DeskClock::previousDay() { adjustDay(-1); }
void DeskClock::nextDay() { adjustDay(1); }
void DeskClock::confirmDate() {
  if (settings_ != nullptr) settings_->setSavedDate(date_);
  status_ = "saved date confirmed";
}

String DeskClock::isoDate() const { return date_; }
String DeskClock::prettyDate() const {
  tm value = {};
  return parseDate(date_, value) ? formatDate(value, "%A, %B %d") : date_;
}
String DeskClock::sourceLabel() const { return source_ == kDeskDateNtp ? "NTP" : "saved"; }
String DeskClock::status() const { return status_; }
DeskDateSource DeskClock::source() const { return source_; }
bool DeskClock::synced() const { return source_ == kDeskDateNtp; }
