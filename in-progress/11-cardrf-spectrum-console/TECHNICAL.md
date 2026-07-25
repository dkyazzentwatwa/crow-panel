# CrowPanel CardRF Spectrum Console Technical Reference

## AI setup prompt

Copy and paste this prompt into an AI coding assistant from the repository root:

```text
Set up and verify the project at in-progress/11-cardrf-spectrum-console.

Read the repository AGENTS.md first. Preserve this project's existing behavior, safety boundaries, mock-first defaults, and proof-state requirements. Start by inspecting the current source, configuration, and the rest of this technical reference. Do not edit unrelated worktree changes.

Use the documented build and upload commands for this project. Keep credentials, local device settings, and other ignored files out of Git. Do not claim an upload or runtime result unless the exact command succeeded and the behavior was observed on the intended hardware. Report results precisely as compile-ready, uploaded, or field-proven.

At the end, summarize files changed, commands run, and remaining proof gaps. Keep the project README user-facing and put implementation details in in-progress/11-cardrf-spectrum-console/TECHNICAL.md.
```

---

Receive-first HackRF dashboard inspired by `cardputer-hackrf`.

V1 consumes mock `SCANROW` and `POWER` lines, renders spectrum and heatmap
status, and can ingest the same receive-only lines from a host/HackRF bridge
when explicitly compiled with `USE_RF_UART_BRIDGE=1`. No RF transmit controls
are included.

Proof state: compile-ready only until the exact CrowPanel revision, UART pins,
bridge host, HackRF behavior, upload, and runtime display behavior are verified.

## Serial Commands

- `help` / `status` / `history`
- `scan` - parse one local mock `SCANROW`
- `feed <SCANROW...>` - parse a Serial Monitor scan row
- `feed <POWER...>` - parse a Serial Monitor power sample
- `power` - parse one local mock `POWER`
- `preset <name>`
- `heatmap`
- `bridge`
- `stop`

Smoke commands:

```text
status
scan
power
feed SCANROW START=433000000 STEP=100000 BINS=16 MIN=12 MAX=188 DATA=1028446688AACCEE
feed POWER RAW=142 CLIP=0 SAMPLES=128
heatmap
bridge
stop
```

## Bridge Protocol

The optional bridge is line-oriented ASCII over `Serial1`. Each line ends with
`\n`; `\r\n` is accepted. The panel only reads lines and never writes RF
commands back to the bridge.

```text
SCANROW START=433000000 STEP=100000 BINS=16 MIN=12 MAX=188 DATA=1028446688AACCEE
POWER RAW=142 CLIP=0 SAMPLES=128
```

Fields:

- `SCANROW START` - first bin center/start frequency in Hz.
- `SCANROW STEP` - Hz between bins.
- `SCANROW BINS` - 1 to 32 bins.
- `SCANROW MIN` / `MAX` - uncalibrated relative power range for the row.
- `SCANROW DATA` - two hex characters per bin.
- `POWER RAW` - uncalibrated receive power estimate.
- `POWER CLIP` - `0` or `1`.
- `POWER SAMPLES` - sample count behind the estimate.

## Build Flags

From the repo root, default build is mock/receive-only:

```sh
FQBN="${FQBN:-esp32:esp32:esp32p4:USBMode=hwcdc,PSRAM=enabled,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,UploadSpeed=921600}" \
arduino-cli compile \
  --fqbn "$FQBN" \
  --libraries "$PWD/shared" \
  --build-property "tools.ctags.cmd.path=/usr/bin/true" \
  in-progress/11-cardrf-spectrum-console
```

Display build:

```sh
FQBN="${FQBN:-esp32:esp32:esp32p4:USBMode=hwcdc,PSRAM=enabled,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,UploadSpeed=921600}" \
arduino-cli compile \
  --fqbn "$FQBN" \
  --libraries "$PWD/shared" \
  --build-property "tools.ctags.cmd.path=/usr/bin/true" \
  --build-property "compiler.cpp.extra_flags=-DUSE_DISPLAY=1" \
  in-progress/11-cardrf-spectrum-console
```

Project-local bridge build:

```sh
FQBN="${FQBN:-esp32:esp32:esp32p4:USBMode=hwcdc,PSRAM=enabled,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,UploadSpeed=921600}" \
arduino-cli compile \
  --fqbn "$FQBN" \
  --libraries "$PWD/shared" \
  --build-property "tools.ctags.cmd.path=/usr/bin/true" \
  --build-property "compiler.cpp.extra_flags=-DUSE_RF_UART_BRIDGE=1" \
  in-progress/11-cardrf-spectrum-console
```

Optional bridge pin overrides:

```sh
--build-property "compiler.cpp.extra_flags=-DUSE_RF_UART_BRIDGE=1 -DCARDRF_BRIDGE_UART_RX=18 -DCARDRF_BRIDGE_UART_TX=17 -DCARDRF_BRIDGE_UART_BAUD=115200"
```

## Safety

Receive-only in v1. No TX, replay, spoofing, jamming, mutation controls, or raw
IQ streaming. UID-only or RF-power-only conclusions are demo-grade; do not claim
field-proven HackRF or CrowPanel hardware support until runtime proof exists.
