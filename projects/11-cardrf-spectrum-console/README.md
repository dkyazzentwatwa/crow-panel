# CrowPanel CardRF Spectrum Console

Receive-first HackRF dashboard inspired by `cardputer-hackrf`.

V1 consumes mock `SCANROW` and `POWER` lines, renders spectrum status, and keeps
real HackRF UART integration as future gated work. No RF transmit controls are
included.

## Serial Commands

- `help` / `status` / `history`
- `scan`
- `feed <SCANROW...>`
- `power`
- `preset <name>`
- `stop`

## Safety

Receive-only in v1. No TX, replay, spoofing, jamming, or raw IQ streaming.
