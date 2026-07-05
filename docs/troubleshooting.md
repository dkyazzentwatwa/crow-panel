# Troubleshooting

## Arduino CLI Cannot Find The Board

Run:

```sh
arduino-cli board list
```

On macOS, check both `/dev/cu.usbmodem*` and `/dev/cu.usbserial*`. Re-detect after reset or upload.

## Wrong FQBN

The default FQBN is generic:

```sh
esp32:esp32:esp32
```

It is only a mock compile default. Install the correct ESP32 Arduino core and any Elecrow-supported board package before claiming CrowPanel hardware support.

## ctags Fails Before Compile

Retry with:

```sh
CTAGS_WORKAROUND=1 ./scripts/compile-all.sh
```

This passes:

```text
--build-property tools.ctags.cmd.path=/usr/bin/true
```

## Hardware Include Fails

If enabling a real driver causes a missing header error, install the matching library and verify the include name. Hardware libraries are not installed by default.

## Touch Address Does Not Respond

The GT911 address can be `0x5D` or `0x14` depending on INT level during reset. Check the reset sequence in the official Elecrow example before changing pins.
