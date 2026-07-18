# LittleHakr RF Lab

Project 17 is a touch-first CrowPanel Advanced ESP32-P4 lab for an external
nRF24L01+ and CC1101. It starts with register proof and keeps transmit,
payload reads, identity collection, replay, jamming, brute force, and raw RF
traces out of the project.

## Wiring

Use the external header photographed on the CrowPanel:

| Signal | CrowPanel GPIO |
|---|---:|
| Shared SPI SCK / MOSI / MISO | 52 / 51 / 50 |
| nRF24 CSN / CE | 49 / 2 |
| CC1101 CS / GDO0 / GDO2 | 25 / 4 / 5 |
| Both modules | 3V3 and shared GND |

Use 10 to 47 uF plus 100 nF decoupling beside the nRF24 and at least 10 uF
beside the CC1101. Do not use the header's 5 V pins.

## Modes

- Default: register proof only. It reads nRF24 `STATUS` and CC1101 `PARTNUM` /
  `VERSION`, then shows the GDO0/GDO2 input levels. TX is disabled.
- `USE_RF_LAB_DETECTOR=1`: enables a fixed local authorized-lab profile only
  when `config/LabProfile.h` sets `RF_LAB_PROFILE_CONFIRMED 1`. The nRF24 uses
  its RPD activity bit; the CC1101 uses relative RSSI plus GDO transitions.
  Neither radio's payload FIFO is read.
- `USE_RF_LAB_PERSISTENCE=1`: saves only the latest aggregate session summary
  to FFat.
- `USE_RF_LAB_C6_WIFI=1`: adds an onboard C6 aggregate Wi-Fi scan page. It
  shows count and strongest relative RSSI only, never SSIDs or BSSIDs.
- `USE_RF_LAB_C6_BLE=1`: enables the separate C6 BLE status page. The current
  P4 Arduino profile does not expose hosted NimBLE yet, so the page reports
  the firmware gate honestly rather than pretending to scan.

Copy `config/LabProfile.example.h` to the gitignored `config/LabProfile.h`
only after confirming that the fixed 433.92 MHz and nRF24 test equipment is
yours or explicitly authorized for the lab.

## Compile

From the repository root:

```sh
CTAGS_WORKAROUND=1 arduino-cli compile \
  --fqbn "esp32:esp32:esp32p4:USBMode=hwcdc,PSRAM=enabled,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,UploadSpeed=921600" \
  --libraries "$PWD/shared" \
  --build-property "tools.ctags.cmd.path=/usr/bin/true" \
  projects/17-littlehakr-rf-lab
```

Compile a display and C6 Wi-Fi page build:

```sh
CTAGS_WORKAROUND=1 arduino-cli compile \
  --fqbn "esp32:esp32:esp32p4:USBMode=hwcdc,PSRAM=enabled,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,UploadSpeed=921600" \
  --libraries "$PWD/shared" \
  --build-property "tools.ctags.cmd.path=/usr/bin/true" \
  --build-property "compiler.cpp.extra_flags=-DUSE_DISPLAY=1 -DUSE_RF_LAB_C6_WIFI=1" \
  projects/17-littlehakr-rf-lab
```

## Serial controls

`status`, `probe`, `start`, `pause`, `save`, `clear`, `wifi`, and `proof`.
None can change the fixed frequency profile or enable TX.

## Proof state

This project is `compile-ready` only until the exact panel is flashed and the
screen, touch zones, C6 hosted path, radio IDs, GDO levels, and authorized lab
activity are observed live.
