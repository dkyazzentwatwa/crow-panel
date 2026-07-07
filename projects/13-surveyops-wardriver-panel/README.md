# CrowPanel SurveyOps Wardriver Panel

Passive GPS/Wi-Fi survey dashboard inspired by `esp32-gps-wifi-wigle`.

V1 is mock-first. It visualizes GPS fix state, Wi-Fi AP rows, logging state,
rotation, and SD health. Real GPS/Wi-Fi remains future gated hardware work.

## Serial Commands

- `help` / `status` / `history`
- `gps`
- `scan`
- `log on`
- `log off`
- `feed ap <ssid> <rssi>`
- `rotate`

This port is passive survey/logging only. It does not join networks or perform
active attacks.
