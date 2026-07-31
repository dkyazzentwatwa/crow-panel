#ifndef CYPHER_DESK_OS_TYPES_H
#define CYPHER_DESK_OS_TYPES_H

#include <Arduino.h>

enum DeskAppId : uint8_t {
  kDeskAppHome,
  kDeskAppWriter,
  kDeskAppToday,
  kDeskAppCalendar,
  kDeskAppContacts,
  kDeskAppClock,
  kDeskAppCalculator,
  kDeskAppFiles,
  kDeskAppSettings,
  kDeskAppRecorder,
  kDeskAppMusic,
  kDeskAppPodcasts,
  kDeskAppVideo,
  kDeskAppWeather,
  kDeskAppCount
};

enum DeskSdState : uint8_t {
  kDeskSdNotPresent,
  kDeskSdMounting,
  kDeskSdMounted,
  kDeskSdReadOnly,
  kDeskSdUnsupportedFilesystem,
  kDeskSdCorrupted,
  kDeskSdFull,
  kDeskSdRemovedUnexpectedly,
  kDeskSdError
};

enum DeskWifiState : uint8_t {
  kDeskWifiDisabled,
  kDeskWifiIdle,
  kDeskWifiScanning,
  kDeskWifiNetworksFound,
  kDeskWifiConnecting,
  kDeskWifiConnected,
  kDeskWifiConnectedNoInternet,
  kDeskWifiAuthenticationFailed,
  kDeskWifiNetworkNotFound,
  kDeskWifiCaptivePortalSuspected,
  kDeskWifiDisconnected,
  kDeskWifiError
};

struct DeskTouchEvent {
  int16_t x = 0;
  int16_t y = 0;
  bool pressed = false;
  bool released = false;
  uint32_t atMs = 0;
};

struct DeskAppDescriptor {
  DeskAppId id;
  const char *title;
  const char *subtitle;
  uint16_t accent;
};

struct DeskWifiNetwork {
  String ssid;
  int32_t rssi = -127;
  bool secured = true;
};

struct DeskCalendarEvent {
  String date;
  String time;
  String title;
  String notes;
  bool alarm = false;
};

struct DeskContactRecord {
  String name;
  String organization;
  String phone;
  String email;
  String notes;
};

#endif
