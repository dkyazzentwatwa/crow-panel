# Cypher Flock Three-Board Architecture

```text
CrowPanel P4 + hosted C6 witness (UI, sessions, passive AP metadata)
    <-> UART 115200
ESP32 DevKit (passive BLE, aggregation, LED)
    <-> UART 115200
BW16 / RTL8720DN (passive 2.4 + 5 GHz Wi-Fi)
```

The detector lineage is `flock-you@9da9ea6`. The active BW16 source lineage is
the receive-only `/Users/cypher/Documents/GitHub/5ghz-wardriver-bw16` project.
No deauthentication or transmit logic is copied from other BW16 projects.

## Wiring

| From | To |
|---|---|
| CrowPanel GPIO47 TX | ESP32 GPIO16 RX |
| CrowPanel GPIO48 RX | ESP32 GPIO17 TX |
| BW16 Serial1 TX, PB1 / board pin 4 | ESP32 GPIO32 RX |
| BW16 Serial1 RX, PB2 / board pin 5 | ESP32 GPIO33 TX |
| Ground | Shared ground across all three boards |

All UART signals are 3.3 V. Power each board separately over USB during initial
bring-up and do not join the 5 V rails. GPIO47/48 and PB1/PB2 remain
hardware-unverified until checked against the exact board revisions.

## Builds

```sh
python3 scripts/generate-flock-catalog.py --check
python3 scripts/test-flock-catalog.py
CTAGS_WORKAROUND=1 ./scripts/build-flock-bridge.sh
./scripts/build-flock-bw16.sh

FQBN='esp32:esp32:esp32p4:USBMode=hwcdc,PSRAM=enabled,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,UploadSpeed=921600' \
CTAGS_WORKAROUND=1 \
EXTRA_FLAGS='-DUSE_DISPLAY=1 -DUSE_FLOCK_UART_BRIDGE=1 -DUSE_FLOCK_PERSISTENCE=1 -DUSE_FLOCK_C6_WITNESS=1' \
./scripts/compile-all.sh
```

The ESP32 build requires NimBLE-Arduino and ArduinoJson. The BW16 build uses the
installed `realtek:AmebaD:Ai-Thinker_BW16` core and ArduinoJson. One generated
`shared/FlockSignatureCatalog` header serves both cores.

## Onboard C6 witness

`USE_FLOCK_C6_WITNESS=1` configures the proven P4-to-C6 SDIO pins before Wi-Fi
initialization and starts asynchronous passive scans with hidden-network results.
The fifth `C6 WIFI` screen exposes the richest AP record available through the
hosted Arduino API: SSID/hidden state, BSSID, RSSI, channel, authentication,
pairwise/group cipher, PHY flags, bandwidth, WPS, FTM, country code, and Wi-Fi 6
BSS color. It stores at most 64 rows in RAM and never joins an AP.

Witness rows are deliberately isolated from the Flock catalog, device store,
alerts, confidence, lifetime totals, calibration files, and FFat sessions. The
C6 is 2.4 GHz-only and the hosted API does not expose the raw addr1/addr2/addr3
promiscuous-frame path, so it cannot replace the BW16.

## Evidence policy

The catalog is not a bulk OUI list. Common module-vendor prefixes are
corroborators. XUNTONG company ID `0x09C8` is a candidate, not a Flock identity.
Exact names, name-to-MAC relationships, multi-channel wildcard behavior, and
custom Raven UUID combinations determine medium/high evidence. RSSI and BLE
address type never increase identity confidence. Addr1 and non-transmitting
addr3 records set `direct_rssi=false` and are excluded from the proximity scope.

Balanced mode alerts high and corroborated medium records. Candidates remain in
their own feed and do not increment lifetime counters. Precision limits alerts
to high evidence. Recall also surfaces candidates but retains their label.

## Protocol and failure behavior

Both UART links use newline-delimited JSON v1. The ESP32 preserves BW16 `seq` as
`source_seq`, rejects duplicate/out-of-order source events, and emits one global
sequence to the panel. Parsers drop malformed, unsupported, or oversized input
without resetting a board. Aggregator, BLE, and BW16 health are separate, stale
after 5 seconds, and offline after 10 seconds.

If raw BW16 initialization fails, the node reports `scan-fallback` and performs
receive-only passive dual-band beacon scans. Address-role and wildcard matching are unavailable in
that state. DFS channels are absent from the default target schedule.

Calibration files contain sanitized metadata only: OUI, hashed identity, name
grammar, UUID counts, frame role, channel/band, RSSI bucket, and match flags.
They never contain payloads, coordinates, credentials, or exact device lists.

## Proof states

- `compile-ready`: all three targets and catalog fixtures compile/pass.
- `uploaded`: all boards are flashed and both UART links exchange hello/status.
- `dual-band-proven`: real BW16 raw counters rise on 2.4 and 5 GHz channels.
- `field-proven`: known-target recall is at least 90% with no unexplained
  high-tier false positives in dense negative-control tests.

Current evidence is compile-only. Upload, UART pin behavior, raw 5 GHz reception,
sustained scanning, persistence recovery, and field accuracy need hardware tests.

## Primary references

- [Realtek AmebaD BW16 guide](https://www.amebaiot.com/cn/amebad-micropython-bw16typec-getting-started/): RTL8720DN dual-band Wi-Fi/BLE capability and PB1/PB2 Serial1 pin mapping.
- [Bluetooth SIG Assigned Numbers](https://www.bluetooth.com/wp-content/uploads/Files/Specification/HTML/Assigned_Numbers/out/en/index-en.html): company identifiers, including `0x09C8` assigned to XUNTONG.
- [IEEE public OUI registry](https://standards-oui.ieee.org/oui/oui.txt): authoritative MA-L assignment source used for direct-owner OUI review.

Community and local observations are intentionally labeled as such in
`signatures/cypher-flock/catalog.json`; they never silently inherit the authority
of these primary sources.
