# camprobe — SCCB bus probe

A throwaway sketch that answers one question before any driver is trusted:
**what is actually on the camera header, and at which address?**

The main project's `Sc2336Sensor` will self-correct if the sensor answers at an
unexpected address, but it can only do that among a handful of candidates. This
tells you the ground truth, including the case where nothing answers at all —
which is almost always a ribbon seating problem, not a software one.

## What it does

1. Opens `Wire1` on the camera SCCB bus (SDA = IO12, SCL = IO13).
2. Scans the whole 7-bit address space and lists what answers.
3. Reads sensor ID registers `0x3107` / `0x3108` at each hit. An SC2336 reports
   `0xCB3A`.
4. Prints the verdict.

Results render **on the panel**, not over Serial. With `USBMode=hwcdc` the
native USB serial port drops the moment an app starts running, so serial output
from a running sketch is not something you can rely on here. Serial printing is
kept anyway for the case where a UART adapter is attached.

## Run it

The board must be in download mode first: **hold BOOT, tap RESET, release
BOOT** — then the port reappears.

```bash
CTAGS_WORKAROUND=1 EXTRA_FLAGS="-DUSE_DISPLAY=1" ./scripts/upload-project.sh projects/02-cypher-vision-cam/tools/camprobe /dev/cu.usbmodem101
```

## Reading the result

| Line 5 says | Meaning |
|---|---|
| `SC2336 present` | Good. Note the address; if it is not `0x30`, update `CAMERA_SC2336` in `shared/CrowPanelShared/HardwareProfile.cpp`. |
| `id=0x....  UNKNOWN PART` | Something is there but it is not an SC2336. The register table in `Sc2336Sensor.cpp` will not apply — identify the part before going further. |
| `scan: NO DEVICES` | Nothing on the bus. Reseat the camera ribbon (check orientation and that the connector latch is closed) before suspecting code. |
| `no reply to ID read` at a scanned address | Something ACKs its address but will not answer a 16-bit register read — likely not a camera sensor at all. |

This is a diagnostic, not part of the build. It is excluded from
`scripts/project-registry.sh` on purpose, so `compile-all.sh` and the flag
matrix do not try to build it as a project.
