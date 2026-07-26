# CrowPanel AIoT Arduino Suite

Twenty standalone Arduino sketches for the **Elecrow CrowPanel Advanced 7-inch
ESP32-P4 HMI AI Display** — a camera, a groovebox, a Game Boy, a USB keyboard, an
aircraft radar, an RF lab, and more, each a complete touch product rather than a
demo snippet.

No PlatformIO, no CMake, no IDE project. Everything builds with `arduino-cli`
through the scripts in `scripts/`. Rendering is Arduino_GFX (the
Adafruit-GFX-style API) — **no LVGL, by design**.

```bash
git clone <this-repo> && cd crow-panel
./scripts/install-cores.sh          # esp32:esp32@3.3.8
./scripts/install-libs.sh           # verified library set (versions in libraries.txt)
arduino-cli board list              # find your port
./scripts/upload-project.sh projects/14-adsb-flight-tracker-radar /dev/cu.usbmodemXXXX
```

Every sketch also boots **mock-first**: with all hardware flags off it runs a
Serial-driven demo of itself, so you can explore any project before wiring
anything up. No board at all? Several suites run entirely on your machine — see
[Building and verifying](#building-and-verifying).

---

## The honesty contract

Most hobby repos blur "it compiles" into "it works." This one keeps them apart,
because on embedded hardware the gap between them is where all the pain lives.
Two words appear throughout, and they mean exactly one thing each:

| Term | Meaning |
|---|---|
| **compile-verified** | Builds green for the real ESP32-P4 target. Proves *nothing* about the physical board. |
| **field-proven** | Observed working on a real CrowPanel, with the behaviour and the exact build recorded. |

`docs/full-port-proof-matrix.md` tracks every project's state
(`compile-ready` → `uploaded` → `field-proven`) and **rows are never upgraded
past the evidence**. `docs/hardware-risk-register.md` is the companion: a running
list of things that compile but could still fail on a bench, each with its
mitigation and, where it happened, how it actually resolved.

That register is the most useful file here. It's where you learn that the audio
amp enable is active-**low** (drive it high and the speaker is silent while I2S
happily streams), that the panel wires its ESP32-C6 SDIO data lines *reversed*
versus Espressif's reference board, and that a green build is not proof a library
linked.

---

## Hardware

- **Board:** [CrowPanel Advanced 7-inch ESP32-P4 HMI AI Display](https://www.elecrow.com/crowpanel-advanced-7inch-esp32-p4-hmi-ai-display-1024x600-ips-touch-screen-with-wifi-6-compatible-with-arduino-lvgl-micropython.html)
  ([vendor examples](https://github.com/Elecrow-RD/CrowPanel-Advanced-7inch-ESP32-P4-HMI-AI-Display-1024x600-IPS-Touch-Screen))
- **SoC:** ESP32-P4NRW32 — RISC-V up to 400 MHz, 16 MB flash, 32 MB PSRAM
- **Display:** 7.0" IPS 1024×600, MIPI-DSI (EK79007), GT911 5-point capacitive touch
- **Radio:** none on the P4. Wi-Fi 6 / BLE 5.3 ride an onboard **ESP32-C6 over
  SDIO** (`esp_hosted`)
- **Camera:** SC2336 over MIPI-CSI — no third-party library needed; core 3.3.8
  already links the ESP-IDF camera stack for this target
- **Audio:** NS4168 I2S amp + speaker, dual PDM mics
- **Expansion:** SD_MMC, SX1262 LoRa, nRF24 / CC1101 sockets

Development is against board revision **V1.2**; pin groups for V1.0/V1.1 live in
`shared/CrowPanelShared/HardwareProfile.cpp`. Read pins from the profile — never
hardcode a GPIO in a project.

---

## The projects

Numbering has intentional gaps (no 06, 12). Proof states are as of the latest
session; see the proof matrix for the detail behind each.

### Field-proven on hardware

| # | Project | What it is |
|---|---|---|
| 02 | [Cypher Vision Cam](projects/02-cypher-vision-cam) | Portable touch camera: MIPI-CSI viewfinder PPA-blitted 1:1 to the panel, hardware-JPEG stills and MJPEG/AVI clips to SD, on-panel gallery review, plus an MJPEG feed and web gallery served off the C6 |
| 09 | [Cypher Tune MPC](projects/09-cypher-tune-mpc) | Real groovebox: velocity-sensitive 4×4 multi-touch pads, 8-voice I2S sample engine, 16-step / 4-pattern sequencer with swing, built-in synth kit, hot-swappable SD WAV kits and tempo-locking backing loops |
| 14 | [ADS-B Flight Tracker Radar](projects/14-adsb-flight-tracker-radar) | Live aircraft radar over Wi-Fi (airplanes.live / adsb.fi) with weather, earthquake, aurora and air-quality screens |
| 21 | [Cypher Keys HID Deck](projects/21-cypher-keys-hid-deck) | The panel *is* a keyboard: multi-touch typing with real modifier chording, macro presets, an app launcher and mechanical key-click audio — over **USB or Bluetooth**. The trackpad is USB-only |
| 22 | [Cypher Boy](projects/22-cypher-boy) | Game Boy / GBC player on a vendored gnuboy core, with NES and Mega Drive cores behind flags; ROMs and saves from SD |

### Uploaded / host-tested

| # | Project | What it is |
|---|---|---|
| 10 | [LiteGo Touch Coach](projects/10-litego-touch-coach) | Playable 9×9 Go against a Monte-Carlo opponent: positional superko, komi scoring, undo, hints. 28/28 rules fixtures pass on the host |

### Compile-ready

| # | Project | What it is |
|---|---|---|
| 04 | [RelayOps Wi-Fi Control Hub](projects/04-relayops-wifi-control-hub) | Five-screen hub: sensor nodes POST into a web server on the panel, the panel sends HTTP GPIO commands back out, plus a live weather / quake / aurora / air-quality screen |
| 05 | [CypherDrive Active Field Tool](projects/05-cypherdrive-wireless-ops) | Active Wi-Fi (probe scan + join + captive-portal detection / mDNS / port-scan), on-panel BLE central (scan + GATT) and USB/BLE HID output — all through the onboard C6. Excludes deauth/jamming, evil-twin capture, and autorun HID |
| 07 | [NFC Field Lab / BadgeOps Pro](projects/07-nfc-field-lab-badgeops-pro) | Tag bench plus badge kiosk: UID, NDEF preview, step-through read-only APDU trace, on-tag file list, and a grant/deny registry |
| 08 | [Cypher Gamer Arcade](projects/08-cypher-gamer-arcade) | Touch arcade — Pong, Snake, 2048 — with pause overlay and SD high scores |
| 13 | [SurveyOps Wardriver Panel](projects/13-surveyops-wardriver-panel) | Passive GPS + Wi-Fi site survey with WiGLE-style CSV logging |
| 15 | [Pokedex Panel](projects/15-pokedex-panel) | Offline creature field guide; built-in catalog, optional SD catalog streaming |
| 17 | [LittleHakr RF Lab](projects/17-littlehakr-rf-lab) | nRF24 + CC1101 register-proof bench; activity detection gated behind a local lab profile |
| 18 | [Cypher Desk OS](projects/18-cypher-desk-panel) | Offline-first creator workstation: 3×4 app grid, full Writer, calendar/contacts/clock/files on SD |
| 20 | [Pip-Boy 3000 Terminal](projects/20-pipboy-terminal) | Unofficial fan prop — green phosphor and CRT scanlines over a card launcher: stat page with optional NTP/weather, map, items, SD gallery, holotape audio |

### In progress

Still being worked on and not part of the featured set. These live in
`in-progress/` rather than `projects/`, but they build the same way and
`compile-all.sh` still covers them. Treat their docs and proof states as
provisional.

| # | Project | What it is |
|---|---|---|
| 01 | [FieldOps Control Center](in-progress/01-fieldops-control-center) | Remote-sensor ops dashboard; SX1262 LoRa and ESP-NOW-over-UART transports behind flags |
| 03 | [BadgeOps NFC/RFID](in-progress/03-badgeops-nfc-rfid-system) | Badge check-in / attendance terminal; PN532 (I2C) and MFRC522 (SPI) readers behind flags |
| 11 | [CardRF Spectrum Console](in-progress/11-cardrf-spectrum-console) | Receive-only spectrum + heatmap fed by a host/HackRF UART bridge |
| 16 | [Cypher Flock Panel](in-progress/16-cypher-flock-panel) | Three-board passive dual-band detector: BW16 Wi-Fi + ESP32 BLE + C6 witness, P4 renders and persists |
| 19 | [Starbeam Console](in-progress/19-starbeam-console) | 1:1 port of project-starbeam: 5× nRF24 + 2× CC1101 on one SPI bus, Wi-Fi/BLE half proxied to an ESP32 co-processor |

---

## Building and verifying

Everything is `arduino-cli` behind `scripts/`. `scripts/project-registry.sh` is the
canonical project list — both tiers build the same way.

```bash
./scripts/compile-all.sh            # every sketch, both tiers, default flags
./scripts/check-flag-matrix.sh      # every supported flag combination — the real gate
```

`check-flag-matrix.sh` is what "supported" actually means here: a flag combination
counts as supported only if it has a green row in that script. It compiles well
over a hundred rows and prints its own count, `PASS`/`SKIP`/`FAIL` per row, and a
non-zero exit on any failure. Rows whose library isn't installed `SKIP` rather than
fail, so read the summary rather than trusting the exit code alone.

> On some machines a broken local `ctags` mangles Arduino's sketch preprocessing.
> If you see `expected constructor, destructor, or type conversion` on valid code,
> prefix any build with `CTAGS_WORKAROUND=1`. That skips prototype generation —
> which is why **every sketch defines its functions before use**.

Feature flags that gate *shared* library code must be passed as compiler defines,
not just set in a project header — see [TECHNICAL.md](TECHNICAL.md) for the
three-layer rule, which is the single most error-prone part of the repo:

```bash
CTAGS_WORKAROUND=1 EXTRA_FLAGS="-DUSE_DISPLAY=1 -DUSE_WIFI=1" ./scripts/compile-all.sh
```

### Host-side tests — no board required

Some of the trickier logic is pure C++ or Python and is tested on your machine,
independent of hardware. These are fast and worth running before you flash anything:

```bash
./scripts/test-litego.sh --quick       # Go rules fixtures + AI hygiene + playout bench (g++)
./scripts/test-cypher-keys.sh          # HID chording, touch cadence, sound-pack parsing (clang++)
python3 scripts/test-flock-catalog.py  # signature catalog + policy assertions
python3 scripts/test-flock-protocol.py # UART session, framing, and dedup edge cases
./scripts/compile-flock-system.sh      # catalog freshness + both Python suites + companion builds
```

They compile the same translation units that ship in firmware, so a pass means the
shipped logic passed — not a parallel reimplementation of it.

### Companion firmware

Two projects need a helper board, because the P4 cannot do the job itself — it has
no radio of its own and cannot be an ESP-NOW peer. These are **not** built by
`compile-all.sh`:

```bash
./scripts/build-espnow-companions.sh   # plain ESP32: ESP-NOW bridge + sensor node (project 01)
./scripts/build-flock-bridge.sh        # plain ESP32: BLE aggregator (project 16)
./scripts/build-flock-bw16.sh          # Ai-Thinker BW16: dual-band Wi-Fi scanner (project 16)
```

The BW16 build needs the third-party `realtek:AmebaD` core, which
`install-cores.sh` does not install.

---

## What else is in the repo

| Path | What's in it |
|---|---|
| `shared/CrowPanelShared/` | The spine every sketch includes: hardware profile (pins, panel timing, polarity), feature-flag defaults, the shared Serial UX, and the touch-UI foundation |
| `companions/` | Cypher Flock helper firmware — plain-ESP32 BLE aggregator and BW16 dual-band Wi-Fi scanner |
| `espnow/` | FieldOps mesh companions: an ESP-NOW bridge and a sensor node, plus the vendored mesh core. Needed because the P4 cannot be an ESP-NOW peer |
| `signatures/` | Cypher Flock signature catalog and test fixtures; `generate-flock-catalog.py` compiles them into a shared Arduino library |
| `mock-api/` | Optional local Express server (`npm start`, port 8787) standing in for cloud endpoints — a fake relay for project 04, event and badge endpoints for others. Not needed in mock mode |

---

## Safety boundaries

These are product constraints, deliberately built in:

- **RF projects (11, 13, 17) are passive / receive-only.** No transmit,
  replay, jamming, injection, deauthentication, network joins, or credential
  capture — those capabilities are absent from the code, not merely disabled.
- **Project 05 (CypherDrive) is an active field tool**, and bounded on purpose.
  It does active probe scanning, joins networks you configure, probes them
  (captive-portal *detection*, mDNS/service discovery, TCP port scan), runs a
  BLE central, and sends operator-driven HID. It does **not** implement deauth
  or jamming, evil-twin or captive-portal *credential harvesting*, unattended
  BadUSB autorun payloads, or on-panel 802.11 promiscuous capture. Every HID
  action is tap-to-type, never autorun. Operate only on networks and devices you
  own or are authorized to test.
- **Project 19 (Starbeam) is the sole transmit-capable build**, a faithful port of
  the author's own tool. Every transmit path is disarmed unless a **gitignored
  local `config/LabProfile.h`** sets `STARBEAM_TX_CONFIRMED`, and the UI
  additionally requires an authorized-use acknowledgement. Operate only on
  radios, frequencies and networks you own or are authorized to test.
- **NFC (03, 07) is UID-only and read-only.** UID-based RFID is *not* secure
  access control — see `docs/security-notes.md` before repeating any of it
  publicly.
- Wireless scan output describes other people's devices. Keep captures local.

Per-machine secrets are gitignored (`config/WiFiSecrets.h`, `Pins.h`,
`Location.h`, `LabProfile.h`, `Devices.h`, `CamSecrets.h`); edit the matching
`.example.h` when adding a setting.

---

## Documentation

**Start here**

| File | What's in it |
|---|---|
| [TECHNICAL.md](TECHNICAL.md) | Build system, the feature-flag rule, shared-library architecture, hardware invariants, adding a project |
| [docs/arduino-cli-setup.md](docs/arduino-cli-setup.md) | Toolchain install, why it's Arduino-CLI-only, and the ctags workaround |
| [docs/full-port-proof-matrix.md](docs/full-port-proof-matrix.md) | Per-project proof state and what each still owes |
| [docs/hardware-risk-register.md](docs/hardware-risk-register.md) | Things that compile but might fail on a bench, and how they resolved |
| [docs/hardware-bringup-checklist.md](docs/hardware-bringup-checklist.md) | Staged bring-up (Stage 0 toolchain → 7 camera), one flag at a time |
| [docs/troubleshooting.md](docs/troubleshooting.md) | Build and flash failures |

**Hardware reference**

| File | What's in it |
|---|---|
| [docs/crowpanel-hardware-notes.md](docs/crowpanel-hardware-notes.md) | Board snapshot: SoC, memory, panel, touch, audio |
| [docs/hardware-revision-profiles.md](docs/hardware-revision-profiles.md) | The V1.0 / V1.1 / V1.2 profile macros and the wireless pin swap between them |
| [docs/module-selection.md](docs/module-selection.md) | Pin notes for the SX1262 / nRF24 / PN532 / MFRC522 module paths |
| [docs/c6-wifi-handoff.md](docs/c6-wifi-handoff.md) | Deep dive on getting P4↔C6 `esp_hosted` Wi-Fi up — root causes and what was tried. Resolved; kept for the reasoning |
| [docs/official-source-notes.md](docs/official-source-notes.md) | Vendor links used as truth anchors |

**Project-specific and policy**

| File | What's in it |
|---|---|
| [docs/security-notes.md](docs/security-notes.md) | Why UID-only RFID is not access control, and the passive-radio boundary |
| [docs/cypher-flock-three-board.md](docs/cypher-flock-three-board.md) | The P4 + ESP32 + BW16 architecture, wiring, and evidence policy |
| [docs/cypher-flock-test-setup-guide.md](docs/cypher-flock-test-setup-guide.md) | Bench procedure from clean compile to staged hardware evidence |
| [CLAUDE.md](CLAUDE.md) / [AGENTS.md](AGENTS.md) | Conventions for AI coding agents working in this repo |

Each project also carries its own `README.md` (what it does, how to drive it) and
`TECHNICAL.md` (wiring, flags, proof state). `docs/superpowers/` holds design specs
and implementation plans for the larger features; `docs/ideas/` is unbuilt concepts.

---

## Licence

**MIT** — see [LICENSE](LICENSE).

**One carve-out:** `projects/22-cypher-boy/` vendors third-party emulator cores
(gnuboy from [retro-go](https://github.com/ducalex/retro-go), gwenesis, nofrendo)
that are **GPL-licensed**, so *that project folder is GPLv2*. Each vendored tree
keeps its upstream headers and a `VENDORED.md` recording the source commit and
every local patch. The rest of the repo is unaffected.

No ROMs, no game assets, and no third-party sample packs are distributed here.
Where a project can load external content (project 22's ROMs, project 21's
keyboard sample packs), you supply it yourself and the tooling converts it —
built-in synthesized alternatives ship so nothing is required.

Pip-Boy, Fallout, Pokémon and other referenced marks belong to their respective
owners; the fan-prop projects are unofficial and unaffiliated.
