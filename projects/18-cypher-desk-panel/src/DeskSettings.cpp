#include "DeskSettings.h"

#include <Preferences.h>

namespace {
constexpr const char *kNamespace = "cypher-desk";
}

void DeskSettings::begin() {
  Preferences prefs;
  if (!prefs.begin(kNamespace, true)) return;
  theme_ = static_cast<DeskThemeId>(prefs.getUChar("theme", kDeskThemeMidnightPlum));
  if (theme_ >= kDeskThemeCount) theme_ = kDeskThemeMidnightPlum;
  focusMinutes_ = prefs.getUShort("focus", 20);
  keySound_ = prefs.getUChar("key", 1);
  ambience_ = prefs.getUChar("amb", 0);
  volume_ = prefs.getUChar("vol", 18);
  wifiSsid_ = prefs.getString("ssid", "");
  wifiPassword_ = prefs.getString("wifi-pass", "");
  savedDate_ = prefs.getString("date", "2026-07-13");
  lastDocument_ = prefs.getString("last-doc", "");
  prefs.end();
}

void DeskSettings::writeString(const char *key, const String &value) {
  Preferences prefs;
  if (!prefs.begin(kNamespace, false)) return;
  prefs.putString(key, value);
  prefs.end();
}

void DeskSettings::writeByte(const char *key, uint8_t value) {
  Preferences prefs;
  if (!prefs.begin(kNamespace, false)) return;
  prefs.putUChar(key, value);
  prefs.end();
}

void DeskSettings::writeUShort(const char *key, uint16_t value) {
  Preferences prefs;
  if (!prefs.begin(kNamespace, false)) return;
  prefs.putUShort(key, value);
  prefs.end();
}

DeskThemeId DeskSettings::theme() const { return theme_; }
void DeskSettings::setTheme(DeskThemeId value) {
  theme_ = value < kDeskThemeCount ? value : kDeskThemeMidnightPlum;
  writeByte("theme", theme_);
}
uint16_t DeskSettings::focusMinutes() const { return focusMinutes_; }
void DeskSettings::setFocusMinutes(uint16_t value) {
  focusMinutes_ = value;
  writeUShort("focus", value);
}
uint8_t DeskSettings::keySound() const { return keySound_; }
void DeskSettings::setKeySound(uint8_t value) {
  keySound_ = value % 4;
  writeByte("key", keySound_);
}
uint8_t DeskSettings::ambience() const { return ambience_; }
void DeskSettings::setAmbience(uint8_t value) {
  ambience_ = value % 5;
  writeByte("amb", ambience_);
}
uint8_t DeskSettings::volume() const { return volume_; }
void DeskSettings::setVolume(uint8_t value) {
  volume_ = value > 100 ? 100 : value;
  writeByte("vol", volume_);
}
String DeskSettings::wifiSsid() const { return wifiSsid_; }
String DeskSettings::wifiPassword() const { return wifiPassword_; }
void DeskSettings::setWifiSsid(const String &value) {
  wifiSsid_ = value;
  writeString("ssid", value);
}
void DeskSettings::setWifiPassword(const String &value) {
  wifiPassword_ = value;
  writeString("wifi-pass", value);
}
String DeskSettings::savedDate() const { return savedDate_; }
void DeskSettings::setSavedDate(const String &value) {
  savedDate_ = value;
  writeString("date", value);
}
String DeskSettings::lastDocument() const { return lastDocument_; }
void DeskSettings::setLastDocument(const String &value) {
  lastDocument_ = value;
  writeString("last-doc", value);
}
