#pragma once

// Minimal Config.h for the vendored cypher-chat mesh code, used by the
// CrowPanel ESP-NOW bridge and sensor-node sketches. The full cypher-chat
// Config.h drags in a display driver, buttons, BLE, and board globals that
// the mesh core does not need; this trimmed version defines only the macros
// MeshManager.cpp / MeshCrypto.cpp actually reference. The core mesh
// constants (MESH_CHANNEL, MESH_MAX_PEERS, TTLs, timeouts) live in
// MeshManager.h; the crypto sizes live in MeshCrypto.h - don't redefine them.
//
// Values mirror cypher-chat's defaults so this device is wire-compatible.

#include <Arduino.h>

#ifndef DEFAULT_UNIT_NAME
#define DEFAULT_UNIT_NAME "CYPHER_NODE"
#endif
#ifndef DEFAULT_PASSPHRASE
#define DEFAULT_PASSPHRASE "123456"
#endif

#ifndef MAX_UNIT_NAME_LEN
#define MAX_UNIT_NAME_LEN 16
#endif
#ifndef MAX_PASSPHRASE_LEN
#define MAX_PASSPHRASE_LEN 64
#endif

#ifndef MESH_PROTOCOL_VERSION
#define MESH_PROTOCOL_VERSION 0x02
#endif

// WiFi STA startup timeout before ESP-NOW init (ms).
#ifndef MESH_WIFI_START_TIMEOUT_MS
#define MESH_WIFI_START_TIMEOUT_MS 5000
#endif

// MAC randomization: the bridge/sensor nodes keep a STABLE MAC (a gateway and
// fixed sensors are easier to reason about than rotating identities). Node
// identity on the dashboard is by unit name anyway. Set to true to match
// cypher-chat's anti-tracking chat nodes.
#ifndef MESH_MAC_RANDOMIZE
#define MESH_MAC_RANDOMIZE false
#endif
#ifndef MESH_MAC_ROTATE_MS
#define MESH_MAC_ROTATE_MS 300000
#endif
#ifndef MESH_MAC_KEEP_OUI
#define MESH_MAC_KEEP_OUI false
#endif
