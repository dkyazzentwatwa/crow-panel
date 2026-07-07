# RelayOps build notes

## Feature flags

| Flag | Gates | Default |
|---|---|---|
| `USE_DISPLAY` | The `ControlHubDashboard` (Arduino_GFX, 1024×600). Off → Serial-only. | 0 |
| `USE_WIFI` | STA connect (`CrowNetworkClient`), the inbound `SensorServer` web server, and the outbound `DeviceController` HTTP GETs. Off → mock source + log-only commands. | 0 |

Both are shared-library-visible for `USE_WIFI` (it gates `CrowNetworkClient`),
so set them as `-D` compiler flags, never in `ProjectConfig.h`:

```sh
EXTRA_FLAGS="-DUSE_DISPLAY=1 -DUSE_WIFI=1" ../../scripts/compile-all.sh
```

## Libraries

- `ArduinoJson` (7.x) — parses `POST /sensor` and `POST /register` bodies. Only
  compiled into the `USE_WIFI` path (`SensorServer.cpp`).
- `GFX Library for Arduino` + `SensorLib` — only for `USE_DISPLAY`.
- `WebServer.h` and `HTTPClient.h` ship with the esp32 core; no extra install.
- The world-feeds path (`WorldFeedClient`, `USE_WIFI`) uses `WiFiClientSecure`
  + `HTTPClient` (esp32 core) and `ArduinoJson` with a `Filter` to parse only
  the shown fields. `setInsecure()` skips cert validation (public read-only
  data). `configTime()` provides UTC for quake age. `USE_WIFI=0` compiles none
  of this and serves canned data.

## Mock vs. real, by class

- `SensorServer` — `USE_WIFI=0` makes every method a no-op (no `WebServer`
  member is even declared). Readings come from `MockSensorSource` instead.
- `DeviceController` — `setPin()` logs the URL and flips local state in a mock
  build; issues a real `HTTPClient` GET under `USE_WIFI`. Members are declared
  unconditionally so the registry behaves identically either way.
- `ControlHubDashboard` — gated on `USE_DISPLAY`; `takePendingToggle()` returns
  false with no display (there is no touch surface to queue a tap).

## Function ordering / ctags

Like the other sketches, the `.ino` defines every function before use
(`nextWord` → `parseSensorCsv` → `seedDevice` → `onSensor`/`onRegister` →
`applyDeviceState` → command handlers → `setup`/`loop`). This keeps the
`CTAGS_WORKAROUND=1` path green, since it skips prototype generation. If you see
"expected constructor, destructor, or type conversion", retry with
`CTAGS_WORKAROUND=1`.

## Config templates

Copied to gitignored files per machine:

- `config/WiFiSecrets.example.h` → `WiFiSecrets.h` (SSID/pass; `USE_WIFI` only)
- `config/Devices.example.h` → `Devices.h` (static device table)
- `config/Pins.example.h` → `Pins.h` (optional on-board status LED)

`ProjectConfig.h` ships default `RELAYOPS_STATIC_DEVICES` pointing at
`127.0.0.1` so every build compiles and the offline `set` demo has devices to
toggle even without `Devices.h`.

## Hardware status

Everything here is **compile-verified** on `esp32:esp32:esp32p4` (core 3.3.8),
**not hardware-verified**. The web server and HTTP client both ride the onboard
ESP32-C6 (esp_hosted); see `docs/hardware-bringup-checklist.md` Stage 5 for the
C6 hosted-firmware caveat.
