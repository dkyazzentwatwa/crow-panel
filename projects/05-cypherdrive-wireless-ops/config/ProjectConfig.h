#ifndef CYPHERDRIVE_PROJECT_CONFIG_H
#define CYPHERDRIVE_PROJECT_CONFIG_H

// Project-local flags. Keep mock mode as the default; enable these one at a
// time with compiler -D flags when proving a specific hardware path.
#ifndef USE_WIFI_SCAN
#define USE_WIFI_SCAN 0
#endif

#ifndef USE_BLE_UART_BRIDGE
#define USE_BLE_UART_BRIDGE 0
#endif

#ifndef USE_QR_PERSISTENCE
#define USE_QR_PERSISTENCE 0
#endif

#ifndef CYPHERDRIVE_DEFAULT_QR_URL
#define CYPHERDRIVE_DEFAULT_QR_URL "https://techtiff.ai/cypher-drive"
#endif

#ifndef CYPHERDRIVE_WIFI_SCAN_MAX_RESULTS
#define CYPHERDRIVE_WIFI_SCAN_MAX_RESULTS 8
#endif

#ifndef CYPHERDRIVE_WIFI_SCAN_MS_PER_CHANNEL
#define CYPHERDRIVE_WIFI_SCAN_MS_PER_CHANNEL 120
#endif

// BLE is not driven by the CrowPanel directly. A separate ESP32 sidecar runs
// BLE scanning and sends CSV advertisement summaries over UART.
#if __has_include("Pins.h")
#include "Pins.h"
#endif

#ifndef CYPHERDRIVE_BLE_UART_PORT
#define CYPHERDRIVE_BLE_UART_PORT 1
#endif

#ifndef CYPHERDRIVE_BLE_UART_RX_PIN
#define CYPHERDRIVE_BLE_UART_RX_PIN -1
#endif

#ifndef CYPHERDRIVE_BLE_UART_TX_PIN
#define CYPHERDRIVE_BLE_UART_TX_PIN -1
#endif

#ifndef CYPHERDRIVE_BLE_UART_BAUD
#define CYPHERDRIVE_BLE_UART_BAUD 115200
#endif

#ifndef CYPHERDRIVE_BLE_UART_MAX_LINE
#define CYPHERDRIVE_BLE_UART_MAX_LINE 128
#endif

#include <AppConfig.h>

#endif
