# SurveyOps Wardriver Panel

A passive site-survey dashboard for the Elecrow CrowPanel Advanced 7-inch
display.

It pairs a GPS fix with a list of the Wi-Fi access points around you, draws them
on an animated survey scope, and can log everything to SD in WiGLE-style CSV with
automatic file rotation. The default build is fully simulated, so the entire
dashboard works with no GPS receiver, no card, and no scanning.

> This is Project 13 in the [CrowPanel Arduino suite](../../README.md).

## Status

Compile-ready. The display, GPS, passive scan, and SD logging paths all build for
the real ESP32-P4 target, and none of them has been observed on hardware — no GPS
fix, no live scan rows, and no storage logs have been captured. See the
[technical reference](TECHNICAL.md).

## What you get

- An animated passive survey scope with the highlighted AP called out
- An access-point row list with SSID, RSSI, channel, and auth mode
- A GPS fix card with the current position and fix quality
- A logging and storage panel showing file state, rotation, and health
- WiGLE-style CSV output with a standard header and row rotation
- Touch row selection on the scope and list
- An offline demo with simulated GPS, AP, and CSV state, plus an `nmea` command
  for testing the parser without a receiver

## Passive only

This panel listens and records. It does not join networks, capture credentials,
inject packets, deauthenticate clients, or run active tests. Touch selects and
highlights rows — it cannot start a scan, start logging, or take any network
action.

Nothing is configured by default: the GPS pins and the SD chip-select pin all
start unset, and the drivers refuse to start rather than drive a guessed pin on
your panel.

## Privacy and responsible use

A real scan captures the SSIDs and BSSIDs of networks belonging to people around
you, alongside your own coordinates. Treat the CSV logs as local lab artifacts.
Do not publish them raw, and think carefully before uploading a survey of a place
where the people living there did not choose to be mapped.

## Technical reference

For installation, build flags, configuration, upload commands, device details,
file layout, troubleshooting, safety boundaries, and proof terminology, see
[TECHNICAL.md](TECHNICAL.md).
