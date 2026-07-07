# RelayOps tutorial

RelayOps is the first project in this suite where the panel is a **server**, not
a client. This guide takes you from the offline mock to a real two-way Wi-Fi
demo using only your dev machine as the "node".

## 1. Offline first

```sh
../../scripts/compile-all.sh
../../scripts/upload-project.sh projects/04-relayops-wifi-control-hub <PORT>
```

Open Serial (115200, Newline). Try `devices`, `set shop-light on`, and
`feed SENSOR,ATTIC,29.5,40,88,0,-58`. Everything works with no network — the
mock source and log-only commands stand in for the real thing.

## 2. Turn on Wi-Fi + display

```sh
cp config/WiFiSecrets.example.h config/WiFiSecrets.h   # fill in SSID/pass
cp config/Devices.example.h    config/Devices.h        # edit hosts/pins
EXTRA_FLAGS="-DUSE_DISPLAY=1 -DUSE_WIFI=1" \
  ../../scripts/upload-project.sh projects/04-relayops-wifi-control-hub <PORT>
```

The boot log prints `wifi connecting...` then `wifi connected, ip=<hub-ip>`.
The hub now listens on port `RELAYOPS_SERVER_PORT` (default 80).

## 3. Feed the hub from your laptop

Run the mock API on the same LAN so it can act as a fake device, then POST a
reading straight to the hub (see `mock-api/README.md` for both recipes):

```sh
curl -X POST http://<hub-ip>/sensor -H 'Content-Type: application/json' \
  -d '{"nodeId":"attic","temperatureC":29.5,"humidityPct":40,"batteryPct":88,"rssi":-58}'
```

Watch the reading appear on the dashboard and in the Serial log.

## 4. Command a device

Point a `config/Devices.h` row at your laptop (`host` = laptop IP, `path` =
`/gpio`), start `mock-api`, then tap the tile (or `set <id> on`). The command
lands in the mock API console as `[gpio] pin=<n> state=ON`.

## 5. Self-registration (optional)

Instead of a static list, a node can register itself:

```sh
curl -X POST http://<hub-ip>/register -H 'Content-Type: application/json' \
  -d '{"deviceId":"bench-led","host":"192.168.1.99","path":"/gpio","pin":2}'
```

or fold it into a sensor POST with `"control_url":"http://192.168.1.99/gpio"`
and `"pin":2`. A new device tile appears on the roster.

## How it fits together

- `SensorServer` — the inbound `WebServer` (`/sensor`, `/register`, `/health`).
- `DeviceController` — the outbound HTTP controller + device registry.
- `ControlHubDashboard` — the FieldOps-style UI, extended with actuator tiles.
- `MockSensorSource` — the offline stand-in for real node POSTs.

The `.ino` wires them together: `onSensor` is the single inbound pipeline;
`applyDeviceState` is the single outbound path (shared by touch and `set`).

See `docs/codex-build-notes.md` for flag/build details.
