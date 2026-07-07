# Shared CrowPanel Library

`shared/CrowPanelShared` is an Arduino library consumed by all three sketches through:

```sh
arduino-cli compile --libraries "$ROOT/shared" ...
```

## Modules

- `AppConfig.h` — feature-flag defaults (`#ifndef`-guarded so projects and `-D` flags can override)
- `HardwareProfile` — board revision profiles (V1.0/V1.1/V1.2): touch, wireless, audio, display pins + DSI timings
- `Logger` — Serial logging (`info`/`warn`/`error`/`diag`)
- `EventLog` — fixed 16-entry timestamped ring buffer; dump with the `history` serial command
- `Throttle` — wraparound-safe interval gate; every timing gate in the repo uses it
- `SerialCommandRouter` — non-blocking line-based command dispatcher (`help` built in)
- `StatusReport.h` — header-only `printSystemStatus()` (header-only so it reports the *sketch's* flag values)
- `CrowNetworkClient` — mock-by-default network client; real Wi-Fi + HTTP behind `USE_WIFI`
- `DisplayBringup` — behind `USE_DISPLAY`: MIPI-DSI panel + GT911 touch, single status screen drawn with the Adafruit-GFX-style API (Arduino_GFX; no LVGL by design)
- `MockData`, `UiTheme`, `StorageManager`, `EventBus`

Reserved for later episodes (defined, currently unused): `EventBus`, `MockData::isoTime()`.

## Flag caveat

Flags set in a project's `config/ProjectConfig.h` reach only the files that include it (the `.ino` and that project's `src/`). The shared library's `.cpp` files compile with `AppConfig.h` defaults — flags that gate shared code (`USE_WIFI`, `USE_DISPLAY`) must be passed as compiler defines:

```sh
EXTRA_FLAGS="-DUSE_WIFI=1" ./scripts/compile-all.sh
```

Keep board-revision assumptions here instead of scattering pins across project code.
