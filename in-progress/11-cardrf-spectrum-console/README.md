# CardRF Spectrum Console

A receive-only spectrum dashboard for the Elecrow CrowPanel Advanced 7-inch
display.

It renders scan rows as a spectrum view with a rolling heatmap, tracks receive
power, and holds band presets — a large readable front end for a HackRF or
similar receiver running on a host nearby. Everything works offline on simulated
rows, so the console is fully explorable with no radio and no host attached.

> This is Project 11 in the [CrowPanel Arduino suite](../../README.md).

## Status

Compile-ready only. The mock, display, and bridge builds compile for the real
ESP32-P4 target, but no bridge feed has been captured and nothing has been
observed on a physical CrowPanel. The exact UART pins, the bridge host, and the
runtime display behavior all still need proof. See the
[technical reference](TECHNICAL.md).

## What you get

- A spectrum view built from scan rows, up to 32 bins per row
- A rolling heatmap of recent rows
- Receive power with a clipping indicator and sample count
- Named band presets
- A line-oriented ASCII bridge for feeding real rows in over UART
- An offline demo where every command produces simulated data, plus a `feed`
  command so you can paste a row straight into Serial

## How the bridge works

The panel does not own a radio. A host running the actual receiver writes plain
newline-terminated text lines to the panel over UART — one for spectrum rows, one
for power samples. The link is strictly one-way: the panel reads lines and never
writes commands back to the bridge.

## Responsible use

This console is receive-only, by design and by omission. There is no transmit,
no replay, no spoofing, no jamming, no injection, and no raw IQ streaming — those
controls do not exist in the code, not merely disabled.

Power readings here are uncalibrated and relative. They are useful for spotting
activity, not for measurement claims. Listen only where receiving is legal for
you, and remember that legality varies by band and by country.

## Technical reference

For installation, build flags, configuration, upload commands, device details,
file layout, troubleshooting, safety boundaries, and proof terminology, see
[TECHNICAL.md](TECHNICAL.md).
