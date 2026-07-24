#ifndef PIPBOY_WIFI_SECRETS_H
#define PIPBOY_WIFI_SECRETS_H

// Copy to WiFiSecrets.h (gitignored). The CrowPanel joins this 2.4 GHz
// network through its onboard ESP32-C6 and only makes read-only NTP/weather
// requests.
#define PIPBOY_WIFI_SSID "your-2.4ghz-network"
#define PIPBOY_WIFI_PASSWORD "your-password"

#endif
