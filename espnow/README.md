# FieldOps over ESP-NOW

Feed the FieldOps dashboard from an ESP-NOW mesh of plain ESP32s — sensor
nodes (telemetry) and cypher-chat chat nodes (presence) — using the panel's
existing transport seam.

## Why a bridge?

**The CrowPanel ESP32-P4 cannot be an ESP-NOW peer.** Its WiFi lives on the
onboard ESP32-C6 (esp_hosted / `CONFIG_ESP_WIFI_REMOTE_ENABLED=1`), and the
Arduino ESP-NOW library is compiled out on that target — ESP-NOW needs direct
control of a native radio. So a **plain ESP32 joins the mesh and bridges to
the panel over UART**:

```
 sensor nodes ─┐
 chat nodes  ──┤ ESP-NOW mesh (ch 1, cypher-chat wire format)
               └────────► BRIDGE (plain ESP32) ──UART CSV──► CrowPanel P4
                          joins mesh, forwards            EspNowGateway → dashboard
```

The bridge and nodes are wire-compatible with cypher-chat: they reuse its real
`MeshManager` (vendored in `shared-mesh/`), so existing chat nodes appear too.

## Contents

| Path | Runs on | Purpose |
|------|---------|---------|
| `shared-mesh/` | — | Vendored cypher-chat mesh core (`MeshManager`, `MeshCrypto`, `MessageAuth`) + a trimmed `Config.h`, a no-op `OutputManager`, and `SensorBeacon.h`. Provenance: copied from `cypher-chat/cypher-chat-32D`. |
| `bridge/` | plain ESP32 | Joins the mesh, forwards `SENSOR` / `PRESENCE` CSV frames to the panel over UART1. |
| `sensor-node/` | plain ESP32 | A cypher-chat node that broadcasts a `SensorBeacon` every few seconds. Flash one per sensor with a unique `NODE_NAME`. |

## UART frame format (bridge → panel)

Newline-terminated CSV, parsed by `SensorNode::parseCsvFrame` on the panel:

```
SENSOR,<name>,<tempC>,<hum>,<batt>,<motion0/1>,<rssi>
PRESENCE,<name>,<rssi>,<type>
```

## Build & flash

1. **Panel** (ESP32-P4): build FieldOps with the ESP-NOW transport + display:
   ```sh
   CTAGS_WORKAROUND=1 EXTRA_FLAGS="-DUSE_ESPNOW=1 -DUSE_DISPLAY=1" \
     ./scripts/upload-project.sh in-progress/01-fieldops-control-center <PANEL_PORT>
   ```
   Set the bridge UART pins first in `in-progress/01-fieldops-control-center/config/Pins.h`
   (`ESPNOW_UART_RX/TX`) — two FREE CrowPanel header pins (verify against the
   board silk; must not clash with the DSI backlight/reset, touch I2C, or the
   wireless socket).

2. **Companions** (plain ESP32s):
   ```sh
   CTAGS_WORKAROUND=1 ./scripts/build-espnow-companions.sh
   ```
   Then flash `bridge` to one ESP32 and `sensor-node` to one or more others
   (edit `NODE_NAME` per node). All must share the mesh **passphrase**
   (default `123456`) and **channel 1** — matching your cypher-chat nodes.

3. **Wire** the bridge to the panel:
   ```
   bridge TX (GPIO17) ─────► panel RX (ESPNOW_UART_RX)
   bridge RX (GPIO16) ◄───── panel TX (ESPNOW_UART_TX)
   bridge GND ───────────── panel GND
   ```
   (Bridge pins are `BRIDGE_UART_TX/RX` in `bridge/bridge.ino`.)

## Test path

- **No radio yet:** on the panel's USB serial monitor (115200, Newline), type
  `feed SENSOR,ATTIC,29.5,40,88,0,-58` → a node card + gauges appear;
  `feed PRESENCE,CYPHER_NODE,-70,chat` → a presence tile appears. This exercises
  the parser + dashboard before any ESP-NOW hardware.
- **End to end:** power the bridge + a sensor node. The sensor's telemetry shows
  on the panel; a running cypher-chat chat node shows as a presence tile. Watch
  the bridge's own USB serial for the parsed frames it forwards.

## Notes / caveats

- **Crypto mode:** cypher-chat runs compat (HMAC) or AES-GCM. The vendored
  `MeshManager` handles both, so the bridge decodes whatever your nodes send —
  as long as the **passphrase matches**. If nodes see nothing, confirm the
  passphrase and that everyone is on channel 1.
- **MAC randomization:** chat nodes rotate their MAC; identity on the dashboard
  is the **unit name** (from heartbeats), not the MAC. The bridge/sensor nodes
  keep a stable MAC (`MESH_MAC_RANDOMIZE false` in `shared-mesh/Config.h`).
- **Not built by `compile-all.sh`** — the companions target a different FQBN
  (plain ESP32). Use `scripts/build-espnow-companions.sh`.
