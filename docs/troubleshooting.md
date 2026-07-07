# Troubleshooting

## Arduino CLI Cannot Find The Board

Run:

```sh
arduino-cli board list
```

On macOS, check both `/dev/cu.usbmodem*` and `/dev/cu.usbserial*`. Re-detect after reset or upload.

## Wrong FQBN

The default FQBN targets the real board:

```text
esp32:esp32:esp32p4:USBMode=hwcdc,PSRAM=enabled,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,UploadSpeed=921600
```

It needs esp32 core 3.3.x (`./scripts/install-cores.sh` pins 3.3.8). If flashing fails, see the USB-mode and ChipVariant fallbacks in `docs/hardware-bringup-checklist.md`, Stage 0.

## Serial Commands Not Responding

The command router dispatches on newline. In your Serial monitor set the
line ending to **Newline** (not "No line ending") and the baud rate to
115200. Lines longer than 95 characters are discarded with a warning.
Type `help` to list commands.

## Broken ctags (mangled prototype errors)

If a compile fails with errors like `expected constructor, destructor, or
type conversion` or `'cmdStatus' was not declared in this scope` pointing
at otherwise-valid sketch functions, your local ctags is emitting mangled
prototypes during Arduino's sketch preprocessing. Retry with:

```sh
CTAGS_WORKAROUND=1 ./scripts/compile-all.sh
```

This passes:

```text
--build-property tools.ctags.cmd.path=/usr/bin/true
```

which skips prototype generation entirely. All sketches in this repo
define functions before use, so no prototypes are needed. The same
variable works for `upload-project.sh` and `check-flag-matrix.sh`.

## Hardware Include Fails

If enabling a real driver causes a missing header error, install the matching library and verify the include name. Hardware libraries are not installed by default.

## Touch Address Does Not Respond

The GT911 address can be `0x5D` or `0x14` depending on INT level during reset. Check the reset sequence in the official Elecrow example before changing pins.
