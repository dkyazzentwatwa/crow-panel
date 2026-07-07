# CrowPanel RelayOps WiFi Control Hub

A Wi-Fi control hub on the CrowPanel Advanced 7-inch ESP32-P4, reusing the
FieldOps dashboard but with **no radio module** — just Wi-Fi. Two directions:

- **In:** remote ESP32 "nodes" POST sensor data to a web server running on the
  panel (`POST /sensor`). Readings land on the dashboard roster exactly like
  FieldOps.
- **Out:** the hub sends HTTP commands to other ESP32s to toggle their GPIO
  pins — lights, relays, fans (`GET http://<node>/gpio?pin=<n>&state=<0|1>`).

No LoRa, no ESP-NOW. Default mode is a Serial-only mock demo:

- A synthetic source feeds sensor cards every few seconds
- Two demo devices you can toggle from Serial (log-only, no network)
- Event log + footer ticker

The real web server and HTTP controller live behind `USE_WIFI` — compile-verified, not hardware-verified.

## Dashboard

The left roster mixes two kinds of card:

- **Sensor nodes** — temp / humidity / battery / motion. Tap one to pin its
  telemetry into the ring gauges and temperature sparkline (like FieldOps).
- **Actuator devices** — an ON/OFF chip and the target GPIO/host. Tap one to
  toggle it; the tap is queued and executed through `DeviceController::setPin()`.

## Serial Commands

115200 baud, line ending **Newline**:

- `help` / `status` — shared commands
- `devices` — list controllable devices and their HTTP targets
- `set <deviceId> <on|off|toggle>` — command a device's GPIO. `set shop-light on`
  (mock build logs the request; `USE_WIFI` build sends the real HTTP GET).
- `feed <csv>` — inject a sensor reading through the same pipeline a real
  `POST /sensor` uses. `feed SENSOR,ATTIC,29.5,40,88,0,-58` adds a telemetry
  node; `feed PRESENCE,GARAGE,-70,heartbeat` adds a presence tile.

## Compile

```sh
../../scripts/compile-all.sh
```

Enable the real web server + HTTP controller and the display:

```sh
EXTRA_FLAGS="-DUSE_DISPLAY=1 -DUSE_WIFI=1" ../../scripts/compile-all.sh
```

Everything is compile-verified; nothing is hardware-verified until it runs on your CrowPanel.

## Upload

```sh
arduino-cli board list
../../scripts/upload-project.sh projects/04-relayops-wifi-control-hub /dev/cu.usbmodem101
```

## Wi-Fi mode

The panel has no radio of its own — Wi-Fi rides the onboard ESP32-C6
(esp_hosted). With `-DUSE_WIFI=1`:

1. Copy `config/WiFiSecrets.example.h` → `WiFiSecrets.h`, fill in your SSID/pass.
2. Copy `config/Devices.example.h` → `Devices.h`, set each device's `host`,
   `path`, and `pin`. Nodes can also self-register at runtime (`POST /register`,
   or a `control_url` field on `POST /sensor`).
3. Build with `EXTRA_FLAGS="-DUSE_DISPLAY=1 -DUSE_WIFI=1"` and flash.
4. Find the hub's IP in the boot log (`wifi connected, ip=...`), then have a
   node POST to `http://<hub-ip>/sensor`. See `mock-api/README.md` for a
   hardware-free way to try both directions.

Follow `docs/hardware-bringup-checklist.md` Stage 5 for the C6 hosted-firmware caveats.

## What To Film

- Serial boot log, then mock sensor cards filling the roster.
- `set shop-light on` flipping a device tile ON live, `devices` listing targets.
- With `USE_WIFI`: a `curl` POST to `/sensor` appearing on the dashboard, and a
  tile tap logging `[gpio] pin=.. state=ON` on the fake device.
