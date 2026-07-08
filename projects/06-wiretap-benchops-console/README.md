# CrowPanel WireTap BenchOps Console

Mock-first bench-protocol console inspired by `WireTap-32`.

Default builds never touch real probe pins. They visualize safe pin guidance
and mock bus results so the CrowPanel can become the large touch surface before
any bench wiring.

## Display UI

`USE_DISPLAY=1` now uses a project-local WireTap dashboard instead of the
generic shared tile board. The screen is organized as a bench console:

- Protocol lanes for Mode, Pins, I2C, SPI, UART, and GPIO.
- A visible safety gate that keeps `READ-FIRST`, `NO DEFAULT DRIVE`, and
  `3.3V TTL` in the first viewport.
- A SPI status card that says `BLOCKED` unless
  `WIRETAP_ALLOW_SPI_ID_CLOCKING=1` is compiled in.
- A detail panel that mirrors the latest Serial command result.

This is a display polish upgrade only. It does not change probe behavior or
raise the proof state beyond compile-ready.

`USE_BENCH_PROBES=1` enables project-local read-oriented probe scaffolds:

- GPIO read: configures the requested GPIO as `INPUT` only (high-Z, no pullup,
  no pulldown, no output drive), then reads it.
- I2C scan: sends address-only START/address/STOP probes. It does not write
  registers or payload bytes.
- UART RX: opens `Serial1` with the configured RX pin and no TX pin.

SPI ID read is a separate lab opt-in: set `WIRETAP_ALLOW_SPI_ID_CLOCKING=1`
only after checking the target datasheet. It must drive CS/SCK/MOSI to send the
read-ID opcode and dummy clocks, so the default bench-probe row refuses it.

This flag is compile-verified only until wired and observed on the real bench.
Mock mode remains the default.

## Build Flags

From the repo root:

```sh
arduino-cli compile \
  --fqbn "esp32:esp32:esp32p4:USBMode=hwcdc,PSRAM=enabled,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,UploadSpeed=921600" \
  --libraries "$PWD/shared" \
  --build-property "tools.ctags.cmd.path=/usr/bin/true" \
  projects/06-wiretap-benchops-console
```

Display build:

```sh
arduino-cli compile \
  --fqbn "esp32:esp32:esp32p4:USBMode=hwcdc,PSRAM=enabled,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,UploadSpeed=921600" \
  --libraries "$PWD/shared" \
  --build-property "tools.ctags.cmd.path=/usr/bin/true" \
  --build-property "compiler.cpp.extra_flags=-DUSE_DISPLAY=1" \
  projects/06-wiretap-benchops-console
```

Bench-probe build, no SPI clocking:

```sh
arduino-cli compile \
  --fqbn "esp32:esp32:esp32p4:USBMode=hwcdc,PSRAM=enabled,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,UploadSpeed=921600" \
  --libraries "$PWD/shared" \
  --build-property "tools.ctags.cmd.path=/usr/bin/true" \
  --build-property "compiler.cpp.extra_flags=-DUSE_BENCH_PROBES=1" \
  projects/06-wiretap-benchops-console
```

SPI ID clocking build:

```sh
arduino-cli compile \
  --fqbn "esp32:esp32:esp32p4:USBMode=hwcdc,PSRAM=enabled,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,UploadSpeed=921600" \
  --libraries "$PWD/shared" \
  --build-property "tools.ctags.cmd.path=/usr/bin/true" \
  --build-property "compiler.cpp.extra_flags=-DUSE_BENCH_PROBES=1 -DWIRETAP_ALLOW_SPI_ID_CLOCKING=1" \
  projects/06-wiretap-benchops-console
```

The repo helper can also compile the whole suite:

```sh
CTAGS_WORKAROUND=1 EXTRA_FLAGS="-DUSE_BENCH_PROBES=1" ./scripts/compile-all.sh
```

That proves every project still builds with the flag visible, but Worker 06
changes are intentionally limited to this project folder.

## Wiring

Copy `config/Pins.example.h` to `config/Pins.h` for local bench wiring. Leave
every pin at `-1` until the lead is physically labeled and checked against the
active CrowPanel hardware profile printed at boot.

Safety rules:

- 3.3V targets only. Use level shifting for 5V boards.
- Treat every unknown target as unpowered/untrusted until its logic level is
  confirmed. The CrowPanel GPIOs are not 5V tolerant.
- Share ground before any logic-level read, and remove power before changing
  clip leads.
- Do not use display, touch, or audio pins for GPIO reads unless you explicitly
  set `WIRETAP_ALLOW_PANEL_RESERVED_PINS=1` for a controlled lab diagnostic.
- Remove wireless-socket modules before reusing those pins for SPI or GPIO.
- I2C targets need pullups and a shared ground.
- SPI ID mode is disabled unless `WIRETAP_ALLOW_SPI_ID_CLOCKING=1`; it is for
  JEDEC-like read-ID devices only, and unknown targets stay disconnected until
  their datasheet is checked.
- UART is receive-only in this sketch; cross the target TX into
  `WIRETAP_UART_RX` and share ground.

## Serial Commands

- `help` / `status` / `history`
- `mode <hiz|i2c|spi|uart|gpio>`
- `pins`
- `i2c scan`
- `spi id`
- `uart rx`
- `gpio get <pin>`

## Serial Smoke

Mock build:

```text
status
pins
i2c scan
spi id
uart rx
gpio get 2
history
```

Expected proof line: `status` reports `USE_BENCH_PROBES=0`, and bus commands
return mock values.

Bench-probe build with no `Pins.h`:

```text
status
i2c scan
spi id
uart rx
gpio get 2
```

Expected proof line: `status` reports `USE_BENCH_PROBES=1`; I2C/UART say their
pins are unset, SPI says ID clocking is disabled, and GPIO reads only the
requested input/high-Z pin.

## Proof State

Mock path is compile-ready. `USE_BENCH_PROBES=1` is compile-verified only.
Nothing here is uploaded, bench-proven, or field-proven until a real CrowPanel
port, exact wiring, Serial output, and observed target behavior are captured.
