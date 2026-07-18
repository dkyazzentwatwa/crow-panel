#ifndef CROW_PANEL_STATUS_REPORT_H
#define CROW_PANEL_STATUS_REPORT_H

#include <Arduino.h>
#include "AppConfig.h"
#include "HardwareProfile.h"

// Header-only ON PURPOSE: the build flags below are macros, so this
// function must compile inside the sketch translation unit to report the
// sketch's flag values instead of the shared library's defaults. Include
// config/ProjectConfig.h before this header (every .ino already does).
inline void printSystemStatus(Stream &out, const char *appName, uint32_t eventCount) {
  out.print(F("[status] app="));
  out.println(appName);
  out.print(F("[status] uptime_s="));
  out.print(millis() / 1000);
  out.print(F(" free_heap="));
  out.println(ESP.getFreeHeap());
  out.print(F("[status] profile="));
  out.println(activeHardwareProfile().name);
  out.print(F("[status] flags MOCK_MODE="));
  out.print(MOCK_MODE);
  out.print(F(" USE_DISPLAY="));
  out.print(USE_DISPLAY);
  out.print(F(" USE_WIFI="));
  out.print(USE_WIFI);
  out.print(F(" USE_LORA_DRIVER="));
  out.println(USE_LORA_DRIVER);
  out.print(F("[status] flags USE_CAMERA_DRIVER="));
  out.print(USE_CAMERA_DRIVER);
  out.print(F(" USE_PN532_DRIVER="));
  out.print(USE_PN532_DRIVER);
  out.print(F(" USE_MFRC522_DRIVER="));
  out.print(USE_MFRC522_DRIVER);
  out.print(F(" USE_AUDIO="));
  out.println(USE_AUDIO);
  out.print(F("[status] flags USE_WIFI_SCAN="));
  out.print(USE_WIFI_SCAN);
  out.print(F(" USE_FLOCK_C6_WITNESS="));
  out.print(USE_FLOCK_C6_WITNESS);
  out.print(F(" USE_BLE_UART_BRIDGE="));
  out.print(USE_BLE_UART_BRIDGE);
  out.print(F(" USE_QR_PERSISTENCE="));
  out.print(USE_QR_PERSISTENCE);
  out.print(F(" USE_BENCH_PROBES="));
  out.println(USE_BENCH_PROBES);
  out.print(F("[status] flags USE_SD_HIGHSCORES="));
  out.print(USE_SD_HIGHSCORES);
  out.print(F(" USE_RF_UART_BRIDGE="));
  out.print(USE_RF_UART_BRIDGE);
  out.print(F(" USE_CREATOROPS_API="));
  out.print(USE_CREATOROPS_API);
  out.print(F(" USE_GPS_DRIVER="));
  out.print(USE_GPS_DRIVER);
  out.print(F(" USE_SD_WIGLE_LOG="));
  out.print(USE_SD_WIGLE_LOG);
  out.print(F(" USE_SD_POKEDEX="));
  out.println(USE_SD_POKEDEX);
  out.print(F("[status] events="));
  out.println(eventCount);
}

#endif
