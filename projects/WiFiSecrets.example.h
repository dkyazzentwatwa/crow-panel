#ifndef ADSB_RADAR_WIFI_SECRETS_H
#define ADSB_RADAR_WIFI_SECRETS_H

// Copy this file to WiFiSecrets.h (gitignored) and fill in real values.
// Only used when built with EXTRA_FLAGS="-DUSE_WIFI=1". The panel joins this
// network as a STA and pulls live aircraft from the ADS-B API over HTTPS.

#define WIFI_SSID "your-ssid"
#define WIFI_PASS "your-password"

#endif
