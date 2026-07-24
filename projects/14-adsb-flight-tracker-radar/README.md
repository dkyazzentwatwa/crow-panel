# ADS-B Flight Tracker Radar

An aircraft radar dashboard for the Elecrow CrowPanel Advanced 7-inch display.

It gives you a live-looking radar scope, an aircraft list, touch-friendly
detail cards, and optional local screens for weather, earthquakes, aurora
activity, and air quality. It also includes an offline demo mode, so you can
explore the interface without Wi-Fi or an aircraft receiver.

> This is Project 14 in the [CrowPanel Arduino suite](../../README.md).

## Status

The display, touch interface, and live aircraft view have been observed working
on a tested CrowPanel. Treat that as a reference point, not a guarantee for a
different board revision or a later code change. See the
[technical reference](TECHNICAL.md) for setup instructions and exact proof
boundaries.

## What you get

- A radar view with aircraft markers and range rings
- A nearby-aircraft list with useful flight details
- Touch selection and dashboard navigation
- Optional weather, earthquake, aurora, and air-quality views
- An offline demo with simulated aircraft
- Free public data sources with no API key required

## Data sources

Aircraft data comes from [airplanes.live](https://airplanes.live), with
[adsb.fi](https://adsb.fi) available as a fallback. The optional local data
views use [Open-Meteo](https://open-meteo.com/),
[USGS](https://earthquake.usgs.gov/), and
[NOAA SWPC](https://www.swpc.noaa.gov/).

## Privacy and responsible use

This project displays publicly available aircraft information. It does not
control aircraft, transmit radio traffic, or capture credentials. Keep your
location settings and any locally saved telemetry private, and do not commit
the files containing your Wi-Fi password or real coordinates.

## Technical reference

For installation, live-data setup, build and upload commands, device details,
file layout, troubleshooting, and proof terminology, see
[TECHNICAL.md](TECHNICAL.md).
