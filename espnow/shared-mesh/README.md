# Vendored cypher-chat mesh core

`MeshManager.{h,cpp}`, `MeshCrypto.{h,cpp}`, and `MessageAuth.{h,cpp}` are
copied verbatim from the **cypher-chat** project (`cypher-chat/cypher-chat-32D`)
so the CrowPanel bridge and sensor nodes are wire-compatible with an existing
cypher-chat mesh (same 32-byte `MeshHeader`, HMAC/compat and AES-GCM modes, TTL
relay, heartbeats). Keep them in sync with upstream if the mesh protocol
changes.

The two local shims replace cypher-chat subsystems the mesh core doesn't need:

- **`Config.h`** — trimmed to the handful of `MESH_*` macros the mesh source
  references (MAC randomization, WiFi timeout, defaults). The upstream Config.h
  also pulls in a display driver, buttons, and BLE — omitted here.
- **`OutputManager.h/.cpp`** — a no-op logger. The mesh core writes to a global
  `output`; this stub drops those logs (forward them to `Serial` if you want
  them). 

`SensorBeacon.h` is new to this repo (not from cypher-chat): the telemetry
payload the sensor node broadcasts and the bridge recognizes.

Used as an Arduino library via `--library espnow/shared-mesh` (see
`scripts/build-espnow-companions.sh`).
