# Cypher Flock Setup and Test Guide

This guide takes the Cypher Flock system from a clean compile to staged
hardware evidence. It deliberately proves one radio path at a time:

```text
CrowPanel P4 + onboard C6 witness      observation-only 2.4 GHz AP data
            |
            | UART, 115200 baud
            v
ESP32 DevKit aggregator                passive BLE + system protocol
            |
            | UART, 115200 baud
            v
BW16 / RTL8720DN                       passive 2.4 + 5 GHz Wi-Fi
```

Do not treat a successful compile as a hardware result. Record each completed
gate as `compile-ready`, `uploaded`, `dual-band-proven`, or `field-proven`.

## 1. Prepare the bench

You need a CrowPanel Advanced 7-inch ESP32-P4, one generic classic ESP32
DevKit, one BW16, three USB data cables, and jumper wires. Keep the boards
physically separated during initial testing so that USB power is the only
power source for each board.

- Use 3.3 V UART signals only.
- Connect a shared ground across all three boards.
- Do not join any 5 V rails while all boards are powered by USB.
- Power off or unplug USB before adding or moving UART jumpers.
- Confirm the CrowPanel revision and the BW16 silkscreen before treating the
  proposed UART pins as final.

| Signal | Connection | Status |
|---|---|---|
| Panel GPIO47 TX | ESP32 GPIO16 RX | Candidate, check panel revision |
| Panel GPIO48 RX | ESP32 GPIO17 TX | Candidate, check panel revision |
| BW16 PB1 / pin 4 TX | ESP32 GPIO32 RX | Candidate, check BW16 board label |
| BW16 PB2 / pin 5 RX | ESP32 GPIO33 TX | Candidate, check BW16 board label |
| Ground | Panel, ESP32, BW16 ground | Required |

The C6 lives on the CrowPanel. It does not need external wiring and must not
be confused with either external scanner. It only supplies passive nearby AP
metadata to the `C6 WIFI` screen.

## 2. Prepare the toolchain and identify ports

Install the repository dependencies once, then list serial devices with only
one target board connected at a time. This makes port identity unambiguous.

```sh
./scripts/install-cores.sh
./scripts/install-libs.sh
arduino-cli board list
```

At the time this guide was written, no target board was attached to this Mac.
The expected macOS device is normally a `/dev/cu.usbmodem*` or
`/dev/cu.usbserial*` entry, not the Bluetooth or debug-console ports.

Capture the real ports in shell variables after they appear:

```sh
read -r 'PANEL_PORT?CrowPanel port: '
read -r 'BRIDGE_PORT?ESP32 DevKit port: '
read -r 'BW16_PORT?BW16 port: '
```

For the CrowPanel's native USB-C bootloader, hold **BOOT**, tap **RESET**,
then release **BOOT** before listing ports or uploading. With `USBMode=hwcdc`,
the port can disappear as the sketch starts. Check the display rather than
assuming the board crashed.

## 3. Establish compile-ready evidence

Run the source and companion checks before flashing anything:

```sh
python3 scripts/generate-flock-catalog.py --check
python3 scripts/test-flock-catalog.py
python3 scripts/test-flock-protocol.py
./scripts/compile-flock-system.sh
```

Then compile the full CrowPanel application:

```sh
FQBN='esp32:esp32:esp32p4:USBMode=hwcdc,PSRAM=enabled,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,UploadSpeed=921600' \
CTAGS_WORKAROUND=1 \
EXTRA_FLAGS='-DUSE_DISPLAY=1 -DUSE_FLOCK_UART_BRIDGE=1 -DUSE_FLOCK_PERSISTENCE=1 -DUSE_FLOCK_C6_WITNESS=1' \
./scripts/compile-all.sh
```

Expected result: catalog and protocol fixtures pass; the ESP32 bridge, BW16
normal and fallback configurations, and Project 16 compile. This is
`compile-ready` only.

## 4. Prove the panel and C6 witness first

This step requires only the CrowPanel. It proves display, touch, the hosted
C6 link, and the passive AP witness without the external UART chain.

```sh
FQBN='esp32:esp32:esp32p4:USBMode=hwcdc,PSRAM=enabled,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,UploadSpeed=921600' \
CTAGS_WORKAROUND=1 \
EXTRA_FLAGS='-DUSE_DISPLAY=1 -DUSE_FLOCK_C6_WITNESS=1' \
./scripts/upload-project.sh projects/16-cypher-flock-panel "$PANEL_PORT"
```

On the panel:

1. Confirm that touch changes screens.
2. Open **C6 WIFI** and trigger a scan.
3. Confirm AP rows appear and can be paged without freezing touch.
4. Check that the displayed rows include a mixture of SSID or hidden state,
   BSSID, RSSI, channel, and security fields when nearby APs supply them.
5. Confirm that these rows do not add Flock alerts or lifetime detections.

If the C6 screen is empty, first verify the onboard C6 hosted firmware and
SDIO setup in [C6 Wi-Fi handoff](c6-wifi-handoff.md). The required SDIO pin
mapping is already in shared code. Do not change external UART wiring to solve
a C6 witness failure.

## 5. Flash the two external nodes independently

Build and upload the ESP32 BLE aggregator. Keep its Wi-Fi radio disabled as
designed; it should remain a BLE scanner and protocol bridge.

```sh
CTAGS_WORKAROUND=1 ./scripts/build-flock-bridge.sh
arduino-cli upload \
  --fqbn 'esp32:esp32:esp32:PartitionScheme=huge_app' \
  --port "$BRIDGE_PORT" \
  --input-dir _arduino-build/cypher-flock-bridge
```

Build and upload the BW16 Wi-Fi node:

```sh
./scripts/build-flock-bw16.sh
arduino-cli upload \
  --fqbn 'realtek:AmebaD:Ai-Thinker_BW16' \
  --port "$BW16_PORT" \
  --input-dir _arduino-build/cypher-flock-bw16
```

An upload is not yet system proof. At this point, label each node flashed and
keep USB Serial available for recovery. BW16 raw 5 GHz reception remains
unproven until its counters rise on actual 5 GHz traffic.

## 6. Wire and prove the internal BW16 to ESP32 link

With all USB cables disconnected, wire only the BW16 to the ESP32 using the
table in section 1. Reconnect their USB cables. The panel is needed for the
first reliable view of the routed BW16 health and diagnostics, so leave this
pair connected and continue directly to section 7.

The first protocol proof, after the panel is attached, is health rather than
detections:

1. The BW16 reports a `hello` and periodic `status` through the ESP32.
2. The panel marks the BW16 healthy, then stale after five seconds of silence
   and offline after ten seconds.
3. Disconnect one BW16 UART wire briefly. Confirm the health state changes,
   then reconnect it and confirm recovery.
4. Confirm parser/drop counters do not reset during recovery.

Do not proceed if the link never becomes healthy. Recheck crossed TX/RX,
shared ground, 115200 baud, and the actual PB1/PB2 board labels. Never connect
TX to TX or RX to RX.

## 7. Flash and connect the full panel

Flash the full Project 16 configuration, then wire the CrowPanel to the ESP32
with all power removed. Reconnect USB power after the UART wires are in place.

```sh
FQBN='esp32:esp32:esp32p4:USBMode=hwcdc,PSRAM=enabled,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,UploadSpeed=921600' \
CTAGS_WORKAROUND=1 \
EXTRA_FLAGS='-DUSE_DISPLAY=1 -DUSE_FLOCK_UART_BRIDGE=1 -DUSE_FLOCK_PERSISTENCE=1 -DUSE_FLOCK_C6_WITNESS=1' \
./scripts/upload-project.sh projects/16-cypher-flock-panel "$PANEL_PORT"
```

Use the panel in this order:

1. **Stats:** confirm separate healthy states for the aggregator, BLE engine,
   and BW16. They should become stale after five seconds and offline after ten.
2. **C6 WIFI:** confirm it still shows passive AP data independently of the
   Flock device count.
3. **Control:** pause and resume scanning. Confirm an acknowledgement returns.
4. **Control:** choose `2.4`, then a supported channel such as 1. Confirm Wi-Fi
   frame counters increase in a normal Wi-Fi environment.
5. Change to `5`, then channel 36 where a nearby 5 GHz AP exists. Record
   whether the BW16 raw counter rises. This is the first dual-band evidence.
6. Return to `dual` and `balanced` profile. Confirm hopping resumes.

Useful panel serial commands, when the active USB mode exposes a working serial
port, are:

```text
status
witness status
witness scan
bridge status
bridge band 2.4
bridge channel 1
bridge scan pause
bridge scan resume
bridge diag on
```

The same actions are available through the touch UI. The C6 is a witness only:
its AP list must not be interpreted as a Flock detection result.

## 8. Test behavior and persistence

Run these controlled checks before any field session:

| Test | Expected result |
|---|---|
| `demo` | Deterministic Wi-Fi, BLE, and Raven records appear through the normal panel path. |
| `selftest` | Valid samples are accepted; malformed, wrong-version, and oversized input increments rejection diagnostics without a reset. |
| Repeat a demo contact | Count and last-seen update without creating a duplicate MAC entry. |
| Rediscover after cooldown | The record displays a rediscovery indication. |
| `save`, then reboot | Current data persists when FFat is healthy; previous-session browsing works after promotion. |
| Disable or fail storage | The panel remains usable with a visible RAM-only warning and never formats FFat automatically. |
| `bridge scan pause`, then resume | Scanner state changes and acknowledgements return without losing parser counters. |

For a quick visual check, Scope plots only alert-eligible records with direct
target RSSI. Its rings are **signal proximity, not location**. Inferred peers
and indirect frame RSSI belong in Feed or diagnostics, not Scope.

## 9. Field-test safely and record evidence

Use the system only where passive monitoring is lawful and authorized. Do not
join APs, transmit frames, deauthenticate clients, collect credentials, or
attempt to identify people from observations.

Start with a known, authorized test environment:

1. Run five short tests with the same known target or fixture source present.
2. Record detected versus expected observations for 2.4 GHz, 5 GHz, and BLE
   separately.
3. Run a dense ordinary Wi-Fi/BLE environment as a negative control.
4. Review every high-evidence alert. Investigate unexplained alerts as catalog
   evidence issues, not as confirmed identities.
5. Export calibration only after reviewing that it contains sanitized metadata,
   never payloads, coordinates, credentials, or an exact device list.

Use this minimum bench log for each session:

```text
Date and location authorization:
CrowPanel revision and C6 hosted firmware:
ESP32 and BW16 board revisions:
Build flags and Git revision:
C6 witness: AP rows / rescan / touch result:
BLE reports observed:
BW16 raw frames, channel 1:
BW16 raw frames, channel 36:
UART hello/status/ack result:
Parser drops and errors:
Save/reboot/previous-session result:
Known-target results:
Negative-control high alerts:
Proof state reached:
```

## 10. Proof-state decision

- **compile-ready:** the commands in section 3 pass.
- **uploaded:** all three boards are flashed and both UART links exchange
  `hello`, `status`, and an acknowledged command.
- **dual-band-proven:** real BW16 counters rise on both a 2.4 GHz channel and a
  5 GHz non-DFS channel, with the band state shown on the panel.
- **field-proven:** authorized field tests reach at least 90% recall for the
  defined known-target set and have no unexplained high-tier false positives.

Stop at the last state supported by your recorded evidence. In particular,
successful compilation, a C6 AP list, or a single UART hello does not prove
dual-band reception or detector accuracy.

## Fast troubleshooting map

| Symptom | First check |
|---|---|
| CrowPanel will not upload | BOOT + RESET sequence, then the real port from `arduino-cli board list`. |
| Display works but C6 WIFI has no rows | C6 hosted firmware/version and the documented SDIO setup, not external UART wiring. |
| Panel says bridge offline | Panel TX47 to ESP32 RX16, panel RX48 from ESP32 TX17, shared ground, 115200 baud. |
| BLE works but BW16 stays offline | BW16 PB1 TX to ESP32 GPIO32 RX, PB2 RX from GPIO33 TX, board pin labels, crossed lines. |
| BW16 reports `scan-fallback` | This is a visible receive-only fallback. It cannot prove promiscuous address/wildcard matching. |
| No 5 GHz raw frames | Test near a known non-DFS 5 GHz AP on channel 36/40/44/48 or 149-165; do not claim dual-band proof yet. |
| Scope looks wrong | Verify the record has direct RSSI. Scope intentionally excludes inferred-peer RSSI. |
| Data disappears after reboot | Check the FFat warning first. Persistence is opt-in and must never auto-format storage. |

For system details and safety boundaries, see the [three-board architecture](cypher-flock-three-board.md) and the [Project 16 README](../projects/16-cypher-flock-panel/README.md).
