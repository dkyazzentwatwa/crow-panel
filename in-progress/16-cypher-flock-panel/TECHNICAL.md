# 16 - Cypher Flock Panel Technical Reference

## AI setup prompt

Copy and paste this prompt into an AI coding assistant from the repository root:

```text
Set up and verify the project at in-progress/16-cypher-flock-panel.

Read the repository AGENTS.md first. Preserve this project's existing behavior, safety boundaries, mock-first defaults, and proof-state requirements. Start by inspecting the current source, configuration, and the rest of this technical reference. Do not edit unrelated worktree changes.

Use the documented build and upload commands for this project. Keep credentials, local device settings, and other ignored files out of Git. Do not claim an upload or runtime result unless the exact command succeeded and the behavior was observed on the intended hardware. Report results precisely as compile-ready, uploaded, or field-proven.

At the end, summarize files changed, commands run, and remaining proof gaps. Keep the project README user-facing and put implementation details in in-progress/16-cypher-flock-panel/TECHNICAL.md.
```

---

A standalone three-board port of Cypher Flock for the CrowPanel Advanced 7-inch
ESP32-P4. A BW16 owns passive dual-band Wi-Fi, a generic ESP32 owns passive BLE
and protocol aggregation, the onboard C6 acts only as a passive 2.4 GHz witness,
and the P4 owns the touch UI, history, and persistence.

The detector logic is derived from `/Users/cypher/Documents/GitHub/flock-you`
at clean source commit `9da9ea6`. The Flask dashboard, GPS, audio, and AP/web UI
remain excluded. C6 witness rows are observation-only and are never detections.

## Safety boundary

This is a passive field-visibility tool. It inspects management/data frame
headers and BLE advertisements for documented signatures. It does not join
networks, deauthenticate clients, capture credentials, or store packet payloads.
MAC/SSID observations can still be sensitive; use it only where monitoring is
lawful and authorized.

## Build modes

```sh
# Mock/Serial build
CTAGS_WORKAROUND=1 ./scripts/compile-all.sh

# Full panel build
CTAGS_WORKAROUND=1 \
EXTRA_FLAGS="-DUSE_DISPLAY=1 -DUSE_FLOCK_UART_BRIDGE=1 -DUSE_FLOCK_PERSISTENCE=1 -DUSE_FLOCK_C6_WITNESS=1" \
./scripts/compile-all.sh

# ESP32 BLE aggregator and BW16 Wi-Fi scanner
CTAGS_WORKAROUND=1 ./scripts/build-flock-bridge.sh
./scripts/build-flock-bw16.sh
```

Mock mode requires no companion board. Run `demo` to inject deterministic Wi-Fi,
BLE, and Raven contacts through the same parser and store as real UART events.

## Wiring

Initial V1.2 candidates, not yet hardware-verified:

```text
ESP32 TX GPIO17  -> CrowPanel RX GPIO48
ESP32 RX GPIO16  <- CrowPanel TX GPIO47
ESP32 GND        <-> CrowPanel GND
BW16 TX PB1/pin4 -> ESP32 RX GPIO32
BW16 RX PB2/pin5 <- ESP32 TX GPIO33
BW16 GND         <-> shared GND
```

Power all three boards separately from USB during first bring-up. Do not connect
the 5V rails. Copy `config/Pins.example.h` to the gitignored `config/Pins.h` to
override the panel pins after checking the silkscreen and matching Elecrow UART
example.

## UI

- **Scope** maps RSSI to proximity rings and a stable MAC hash to angle. It is
  explicitly not a location or direction display.
- **Feed** lists current/previous contacts with Wi-Fi/BLE/Raven/candidate evidence.
- **C6 WiFi** pages through up to 64 nearby 2.4 GHz APs with SSID/hidden state,
  BSSID, RSSI, channel, security/ciphers, PHY, bandwidth, country, WPS/FTM,
  and BSS color when the hosted scan reports them.
- **Stats** shows separate aggregator, BLE, and BW16 health plus per-band counters.
- **Control** changes scan, hopping, band, channel, profile, diagnostics,
  calibration, stealth, save, and two-tap reset state.

FFat persistence is opt-in. It uses `/cypher-flock/`, validates CRC before
promoting a temporary save, never auto-formats the partition, and falls back to
RAM with a visible warning.

## Serial commands

| Command | Purpose |
|---|---|
| `status` | Print panel, bridge, parser, and storage state |
| `screen scope|feed|witness|stats|control|next` | Change screen |
| `witness status|scan|list|screen` | Inspect or refresh the C6 witness snapshot |
| `filter all|wifi|ble|raven|candidate|bw16|esp32` | Filter by protocol, evidence, or source |
| `source current|previous` | Select current or previous session |
| `demo` | Inject deterministic Wi-Fi, BLE, and Raven hits |
| `inject <json>` | Inject a compact v1 detection object (95-character Serial-router limit) |
| `bridge <command>` | Send a scanner command over UART |
| `calibration on|off|export|clear` | Manage sanitized calibration observations |
| `save` | Save current session to FFat |
| `session reset|current|previous` | Manage session view/state |
| `stealth on|off` | Black out or wake the display while scanning continues |
| `selftest` | Exercise Wi-Fi/BLE/Raven events plus malformed/version/oversize rejection |

Bridge commands add `band 2.4|5|dual`, `profile precision|balanced|recall`,
`calibration on|off|export|clear`, `catalog`, and supported dual-band channels.
See [three-board architecture](../../docs/cypher-flock-three-board.md).
For a staged physical setup, upload, and evidence checklist, see the
[Cypher Flock setup and test guide](../../docs/cypher-flock-test-setup-guide.md).

## Proof state

The source is **compile-ready** only. All three boards must upload and exchange
both UART hello streams before `uploaded`; real 2.4 and 5 GHz raw frames are
required for `dual-band-proven`; the documented recall/false-positive threshold
is required for `field-proven`.

The C6 path is separately hardware-gated: a successful compile does not prove
that the exact panel returns asynchronous hosted scan records. Witness snapshots
are ephemeral and are not written to FFat or counted as detected devices.
