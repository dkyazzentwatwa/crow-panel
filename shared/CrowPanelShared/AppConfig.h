#ifndef CROW_PANEL_APP_CONFIG_H
#define CROW_PANEL_APP_CONFIG_H

#define CROWPANEL_P4_7IN_V1_0 100
#define CROWPANEL_P4_7IN_V1_1 110
#define CROWPANEL_P4_7IN_V1_2 120

#ifndef MOCK_MODE
#define MOCK_MODE 1
#endif

#ifndef USE_DISPLAY
#define USE_DISPLAY 0
#endif

#ifndef USE_WIFI
#define USE_WIFI 0
#endif

#ifndef USE_LORA_DRIVER
#define USE_LORA_DRIVER 0
#endif

// FieldOps ESP-NOW transport: the panel reads sensor/presence frames over
// UART from an ESP-NOW<->UART bridge (a plain ESP32). The ESP32-P4 cannot be
// an ESP-NOW peer directly (WiFi is remote on the C6), so this flag only
// enables the UART reader on the panel - the radio lives on the bridge.
#ifndef USE_ESPNOW
#define USE_ESPNOW 0
#endif

#ifndef USE_CAMERA_DRIVER
#define USE_CAMERA_DRIVER 0
#endif

#ifndef USE_PN532_DRIVER
#define USE_PN532_DRIVER 0
#endif

#ifndef USE_MFRC522_DRIVER
#define USE_MFRC522_DRIVER 0
#endif

#ifndef USE_AUDIO
#define USE_AUDIO 0
#endif

#ifndef USE_WIFI_SCAN
#define USE_WIFI_SCAN 0
#endif

#ifndef USE_FLOCK_C6_WITNESS
#define USE_FLOCK_C6_WITNESS 0
#endif

#ifndef USE_RF_LAB_C6_WIFI
#define USE_RF_LAB_C6_WIFI 0
#endif

#ifndef USE_BLE_UART_BRIDGE
#define USE_BLE_UART_BRIDGE 0
#endif

#ifndef USE_QR_PERSISTENCE
#define USE_QR_PERSISTENCE 0
#endif

#ifndef USE_BENCH_PROBES
#define USE_BENCH_PROBES 0
#endif

#ifndef USE_SD_HIGHSCORES
#define USE_SD_HIGHSCORES 0
#endif

#ifndef USE_RF_UART_BRIDGE
#define USE_RF_UART_BRIDGE 0
#endif

#ifndef USE_CREATOROPS_API
#define USE_CREATOROPS_API 0
#endif

#ifndef USE_GPS_DRIVER
#define USE_GPS_DRIVER 0
#endif

#ifndef USE_SD_WIGLE_LOG
#define USE_SD_WIGLE_LOG 0
#endif

#ifndef USE_SD_POKEDEX
#define USE_SD_POKEDEX 0
#endif

#ifndef CROWPANEL_HARDWARE_PROFILE
#define CROWPANEL_HARDWARE_PROFILE CROWPANEL_P4_7IN_V1_2
#endif

#endif
