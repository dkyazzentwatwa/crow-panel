# Full Port Proof Matrix

This table tracks the newer port folders as they move from mock-first
scaffolds toward real hardware paths. Keep the proof state literal:

- `compile-ready`: the sketch and feature flags build on the ESP32-P4 target.
- `uploaded`: the matching binary flashed to a real CrowPanel.
- `field-proven`: the real peripheral or bridge was observed through Serial
  logs, display/touch behavior, and the project smoke commands.

Do not upgrade a row beyond the evidence captured in the session log.

The shared hosted ESP32-C6 Wi-Fi link is field-proven on the 7-inch panel by
project 14 (`LIVE`, airplanes.live, 60 contacts). Direct passive scan projects
still need their own scan logs before their rows move beyond compile-ready.

| Project | Hardware-gated flags | Real path | Current proof target |
|---|---|---|---|
| 05 CypherDrive Wireless Ops | `USE_WIFI_SCAN`, `USE_BLE_UART_BRIDGE`, `USE_QR_PERSISTENCE` | hosted/C6 passive Wi-Fi scan with shared SDIO pin remap, UART BLE sidecar feed, persisted QR URL | compile-ready until passive scan and sidecar run logs are captured |
| 06 WireTap BenchOps Console | `USE_BENCH_PROBES`, optional `WIRETAP_ALLOW_SPI_ID_CLOCKING` | high-Z GPIO reads, I2C address scan, UART RX; SPI ID clocking only after explicit lab opt-in | compile-ready until probes are wired and Serial logs are captured |
| 07 NFC Field Lab / BadgeOps Pro | `USE_PN532_DRIVER`, `USE_MFRC522_DRIVER` | UID reads, NDEF preview, safe read-only Type 4/NDEF APDU path | compile-ready until reader wiring and tag taps are captured |
| 08 Cypher Gamer Arcade | `USE_DISPLAY`, `USE_SD_HIGHSCORES` | touch-playable Pong, Snake, 2048; optional SD high-score persistence | compile-ready until touch play is observed on panel |
| 09 Cypher Tune MPC | `USE_AUDIO` | I2S/audio path plus silent fallback transport | compile-ready until audio output is heard or measured |
| 10 LiteGo Touch Coach | `USE_DISPLAY` | ADS-B-inspired full-screen touch coach with typed board, pass, CPU, reset, score, and hint actions plus Serial `selftest` | compile-ready until touch moves and command pills are observed on panel |
| 11 CardRF Spectrum Console | `USE_RF_UART_BRIDGE` | receive-only host/HackRF `SCANROW` and `POWER` serial bridge | compile-ready until host bridge feed is captured |
| 12 CreatorOps Board | `USE_CREATOROPS_API` | read-only local/static/API cache source | compile-ready until read-only data fetch/cache is observed |
| 13 SurveyOps Wardriver Panel | `USE_GPS_DRIVER`, `USE_WIFI_SCAN`, `USE_SD_WIGLE_LOG` | GPS parser, passive Wi-Fi scan with shared SDIO pin remap, WiGLE-style CSV logging/rotation | compile-ready until GPS, passive scan, and storage logs are captured |
| 14 ADS-B Flight Tracker Radar | `USE_DISPLAY`, `USE_WIFI` | touch radar plus airplanes.live / adsb.fi, weather, quake, aurora, and air-quality feeds | field-proven for the tested live Wi-Fi path; new changes still need their own proof |
| 15 Pokedex Panel | `USE_DISPLAY`, `USE_SD_POKEDEX` | touch Pokedex UI plus source `esp32-pokedex` SD catalog streaming | compile-ready until SD_MMC mount, touch navigation, and JSON detail browsing are captured |
| 16 Cypher Flock Panel | `USE_DISPLAY`, `USE_FLOCK_UART_BRIDGE`, `USE_FLOCK_PERSISTENCE`, `USE_FLOCK_C6_WITNESS` | BW16 passive 2.4/5 GHz Wi-Fi, ESP32 passive BLE/aggregation, C6 passive AP witness, evidence catalog, five-screen P4 UI and CRC v2 FFat sessions | compile-ready; needs all three uploads/UART plus live C6 witness rows for `uploaded`, real raw 2.4+5 GHz for `dual-band-proven`, and >=90% recall with no unexplained high-tier false positives for `field-proven` |
| 18 Cypher Desk OS | `USE_DISPLAY`, `USE_CYPHER_DESK_SD`, optional `USE_WIFI`, `USE_CYPHER_DESK_AUDIO`, `USE_CYPHER_DESK_MEDIA`, `USE_CYPHER_DESK_RECORDER` | 3x4 OS launcher, reusable app router/status/keyboard, complete Writer, Today, local calendar, contacts, clock, calculator, files, Wi-Fi settings, and guarded recorder/music/podcast/weather surfaces | baseline and display+SD+Wi-Fi builds compile-ready; launcher touch, local data persistence, hosted-C6 states, SD recovery, speaker, and microphone each need new device acceptance |

## Safety Boundaries

- Wireless scan paths are passive visibility tools only.
- BenchOps must default to high-Z/read-oriented behavior; SPI ID clocking must stay explicitly opt-in and documented.
- NFC APDU support is read-only lab inspection, not payment or credential work.
- CardRF is receive-only; no RF transmit, replay, jamming, or injection controls.
- SurveyOps must not join networks, deauth clients, inject frames, or collect
  credentials.
- CreatorOps must not publish, post, delete, or mutate external services.
- Pokedex Panel must stay offline/source-only; it must not connect to Pokemon GO,
  scrape accounts, or imply official game-service integration.
- Cypher Flock must remain detection-only: no network joins, deauthentication,
  credential capture, payload forwarding, or claims that RSSI indicates location.

## Coordinator Acceptance

Before calling a port done, the coordinator should have:

1. A project-local driver or parser behind explicit feature flags.
2. Mock mode still compiling and behaving as the default.
3. Serial commands exercising the same state path used by the dashboard.
4. README wiring, flags, smoke commands, and proof-state notes.
5. Green per-project compile rows and the full `check-flag-matrix.sh`.
