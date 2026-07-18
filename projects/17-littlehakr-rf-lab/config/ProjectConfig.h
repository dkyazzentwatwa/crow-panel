#ifndef LITTLEHAKR_RF_LAB_PROJECT_CONFIG_H
#define LITTLEHAKR_RF_LAB_PROJECT_CONFIG_H

#if __has_include("Pins.h")
#include "Pins.h"
#endif

#if __has_include("LabProfile.h")
#include "LabProfile.h"
#endif

#ifndef USE_RF_LAB_DETECTOR
#define USE_RF_LAB_DETECTOR 0
#endif

#ifndef USE_RF_LAB_PERSISTENCE
#define USE_RF_LAB_PERSISTENCE 0
#endif

// The P4 reaches the onboard C6 over esp_hosted SDIO. Wi-Fi scanning is
// aggregate-only. BLE requires C6 hosted NimBLE firmware and remains gated.
#ifndef USE_RF_LAB_C6_WIFI
#define USE_RF_LAB_C6_WIFI 0
#endif

#ifndef USE_RF_LAB_C6_BLE
#define USE_RF_LAB_C6_BLE 0
#endif

#ifndef RF_LAB_C6_SCAN_INTERVAL_MS
#define RF_LAB_C6_SCAN_INTERVAL_MS 15000UL
#endif

// A local LabProfile.h must set this to 1 before detector mode can run.
#ifndef RF_LAB_PROFILE_CONFIRMED
#define RF_LAB_PROFILE_CONFIRMED 0
#endif

#ifndef RF_LAB_PROFILE_NAME
#define RF_LAB_PROFILE_NAME "UNCONFIRMED"
#endif

#ifndef RF_LAB_SPI_HZ
#define RF_LAB_SPI_HZ 1000000UL
#endif

#ifndef RF_LAB_NRF_CHANNEL
#define RF_LAB_NRF_CHANNEL 76
#endif

#ifndef RF_LAB_CC1101_FREQUENCY_HZ
#define RF_LAB_CC1101_FREQUENCY_HZ 433920000UL
#endif

#ifndef RF_LAB_SAMPLE_INTERVAL_MS
#define RF_LAB_SAMPLE_INTERVAL_MS 250UL
#endif

// Physical GPIO header map confirmed from the CrowPanel board silkscreen.
#ifndef RF_LAB_SCK
#define RF_LAB_SCK 52
#endif
#ifndef RF_LAB_MOSI
#define RF_LAB_MOSI 51
#endif
#ifndef RF_LAB_MISO
#define RF_LAB_MISO 50
#endif
#ifndef RF_LAB_NRF_CSN
#define RF_LAB_NRF_CSN 49
#endif
#ifndef RF_LAB_NRF_CE
#define RF_LAB_NRF_CE 2
#endif
#ifndef RF_LAB_CC1101_CS
#define RF_LAB_CC1101_CS 25
#endif
#ifndef RF_LAB_CC1101_GDO0
#define RF_LAB_CC1101_GDO0 4
#endif
#ifndef RF_LAB_CC1101_GDO2
#define RF_LAB_CC1101_GDO2 5
#endif

#include <AppConfig.h>

#endif
