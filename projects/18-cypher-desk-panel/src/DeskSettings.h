#ifndef CYPHER_DESK_PANEL_SETTINGS_H
#define CYPHER_DESK_PANEL_SETTINGS_H

#include "DeskTypes.h"

class DeskSettings {
 public:
  void begin();
  DeskThemeId theme() const;
  void setTheme(DeskThemeId value);
  uint16_t focusMinutes() const;
  void setFocusMinutes(uint16_t value);
  uint8_t keySound() const;
  void setKeySound(uint8_t value);
  uint8_t ambience() const;
  void setAmbience(uint8_t value);
  uint8_t volume() const;
  void setVolume(uint8_t value);
  String wifiSsid() const;
  String wifiPassword() const;
  void setWifiSsid(const String &value);
  void setWifiPassword(const String &value);
  String savedDate() const;
  void setSavedDate(const String &value);
  String lastDocument() const;
  void setLastDocument(const String &value);

 private:
  DeskThemeId theme_ = kDeskThemeMidnightPlum;
  uint16_t focusMinutes_ = 20;
  uint8_t keySound_ = 1;
  uint8_t ambience_ = 0;
  uint8_t volume_ = 18;
  String wifiSsid_;
  String wifiPassword_;
  String savedDate_ = "2026-07-13";
  String lastDocument_;
  void writeString(const char *key, const String &value);
  void writeByte(const char *key, uint8_t value);
  void writeUShort(const char *key, uint16_t value);
};

#endif
