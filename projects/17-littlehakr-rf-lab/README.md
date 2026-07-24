# LittleHakr RF Lab

A touch-first radio bench for the Elecrow CrowPanel Advanced 7-inch display,
built around an external nRF24L01+ and a CC1101.

It starts where every radio project should start: proving the modules are
actually there and talking. Read their identity registers, watch their GDO pin
levels, and — once you explicitly authorize a lab profile — see activity
indications on a fixed frequency. Separate pages show aggregate Wi-Fi and BLE
status from the panel's onboard C6.

> This is Project 17 in the [CrowPanel Arduino suite](../../README.md).

## Status

Compile-ready only. The panel has not been flashed for this project, so the
screen, the touch zones, the C6 hosted path, the radio IDs, the GDO levels, and
any authorized lab activity all still need to be observed live. See the
[technical reference](TECHNICAL.md).

## What you get

- Register proof by default: nRF24 `STATUS`, CC1101 `PARTNUM` and `VERSION`, and
  live GDO0/GDO2 input levels
- An optional activity detector on a fixed, authorized frequency — the nRF24's
  RPD bit and the CC1101's relative RSSI plus GDO transitions
- An optional aggregate C6 Wi-Fi page showing count and strongest relative RSSI
- A separate C6 BLE status page
- Optional session-summary persistence to FFat
- A touch UI with Serial controls that cannot change the frequency profile or
  enable transmit

## What this lab deliberately does not do

No transmit. No payload reads — neither radio's FIFO is touched. No identity
collection, no replay, no jamming, no brute force, and no raw RF traces. The
Wi-Fi page shows counts and signal strength only, never SSIDs or BSSIDs.

The BLE page is honest rather than impressive: the current P4 Arduino profile
does not expose hosted NimBLE, so the page reports that firmware gate instead of
pretending to scan.

## Before you enable the detector

The activity detector stays off until you copy the example lab profile to a
local, gitignored config header and set the confirmation flag yourself. Do that
only after confirming that the fixed 433.92 MHz and nRF24 test equipment is
yours, or that you are explicitly authorized to work with it.

Wiring, decoupling, and the header pins are in the
[technical reference](TECHNICAL.md) — note that the header's 5 V pins are not for
these modules.

## Technical reference

For installation, build flags, configuration, upload commands, device details,
file layout, troubleshooting, safety boundaries, and proof terminology, see
[TECHNICAL.md](TECHNICAL.md).
