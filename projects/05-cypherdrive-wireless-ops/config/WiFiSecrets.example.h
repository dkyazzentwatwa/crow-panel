#ifndef CYPHERDRIVE_WIFI_SECRETS_H
#define CYPHERDRIVE_WIFI_SECRETS_H

// Per-machine join credentials for the CypherDrive active field tool.
//
// Copy this file to WiFiSecrets.h (which is gitignored) and fill in the network
// you are AUTHORIZED to join. When the SSID you tap in the Wi-Fi inspector
// matches CYPHERDRIVE_JOIN_SSID, the tool associates with CYPHERDRIVE_JOIN_PASS.
// Open networks join with no key regardless. Never commit real credentials.

#define CYPHERDRIVE_JOIN_SSID "MyLabNetwork"
#define CYPHERDRIVE_JOIN_PASS "change-me"

// Optional: default host/subnet for the TCP port-scan tool. Leave empty to
// default to the current gateway at runtime. Only scan hosts you are
// authorized to test.
// #define CYPHERDRIVE_PORTSCAN_TARGET "192.168.1.1"

#endif
