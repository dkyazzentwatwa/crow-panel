#ifndef RELAYOPS_WIFI_SECRETS_H
#define RELAYOPS_WIFI_SECRETS_H

// Copy this file to WiFiSecrets.h (gitignored) and fill in real values.
// Only used when built with EXTRA_FLAGS="-DUSE_WIFI=1". The hub joins this
// network as a STA; nodes then POST sensor data to the hub's IP and the hub
// sends GPIO commands back to the nodes on the same LAN.

#define WIFI_SSID "your-ssid"
#define WIFI_PASS "your-password"

#endif
