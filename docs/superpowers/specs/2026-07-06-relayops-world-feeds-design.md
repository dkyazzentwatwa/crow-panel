# RelayOps World Feeds — Design Spec

Date: 2026-07-06
Project: `projects/04-relayops-wifi-control-hub`

## Context

Project 04 (RelayOps) is a Wi-Fi control hub: the CrowPanel P4 runs a web
server that nodes POST sensor data to, and sends HTTP GPIO commands out to
toggle remote ESP32 pins. Because the P4 joins the home Wi-Fi in station
mode, it also has internet access while serving. This spec adds live
internet "situational awareness" data to the dashboard: **weather,
earthquakes, and aurora (northern-lights) forecast**, so the panel shows
more than just local sensors/devices.

Decided during brainstorming:
- **Fetch model: direct HTTPS from the panel** (`WiFiClientSecure` +
  `setInsecure()`), parsed on-device. No proxy/helper box — a true
  standalone appliance.
- **Layout: bottom world strip** — three fixed cards in a full-width row
  below the (slightly shortened) temperature sparkline; all three feeds
  visible at once.
- **Location: Eugene, OR** (44.05, -123.09) as the example default.

## Goals

- Show weather, latest significant earthquake, and aurora/Kp on the
  dashboard, refreshed live over the internet.
- Keep the existing mock-first contract: default build (`USE_WIFI=0`)
  renders canned data and compiles/demos fully offline.
- Never block the inbound web server for long — the loop must stay
  responsive to node POSTs and touch.

## Non-goals

- Companion node firmware (sensor node + relay node) — **separate spec**,
  written next.
- A Node/mock-api proxy path — rejected in favor of direct HTTPS.
- Fully async networking — v1 uses staggered blocking fetches with short
  timeouts (see Non-blocking strategy).
- TLS certificate validation — `setInsecure()` is acceptable for public,
  read-only data.

## Data sources (free, no API key, HTTPS)

| Card | Source | Endpoint | Fields used |
|---|---|---|---|
| Weather | Open-Meteo | `api.open-meteo.com/v1/forecast?latitude=<lat>&longitude=<lon>&current=temperature_2m,apparent_temperature,weather_code,wind_speed_10m&daily=temperature_2m_max,temperature_2m_min&wind_speed_unit=kn&temperature_unit=celsius&timezone=auto&forecast_days=1` | temp, feels-like, `weather_code`→text, wind (kt), daily hi/lo |
| Earthquakes | USGS | `earthquake.usgs.gov/earthquakes/feed/v1.0/summary/4.5_day.geojson` | newest `features[0]`: mag, place, `time`, depth (`geometry.coordinates[2]`); `metadata.count` for 24h total |
| Aurora | NOAA SWPC | `services.swpc.noaa.gov/products/noaa-planetary-k-index.json` | last row Kp (most recent); previous row for trend arrow |

All parsed with **ArduinoJson using a `Filter`** so only the shown fields
materialize (responses fit easily in 32 MB PSRAM; the filter is the tidy
default, not a hard requirement).

## Components

### `src/WorldFeed.h` — data types
```cpp
enum AuroraVerdict { kQuiet, kWatch, kLikely };

struct WeatherData { float tempC, feelsC, windKt, hiC, loC; String condition; };
struct QuakeData   { float mag, depthKm; String place; long ageMin; int count24h; };
struct AuroraData  { float kp; String level; AuroraVerdict verdict; int trend; }; // trend -1/0/+1

struct WorldFeeds {
  WeatherData weather;  bool weatherValid = false;  unsigned long weatherMs = 0;
  QuakeData   quake;    bool quakeValid   = false;  unsigned long quakeMs   = 0;
  AuroraData  aurora;   bool auroraValid  = false;  unsigned long auroraMs  = 0;
};
```

### `src/WorldFeedClient.{h,cpp}` — the fetcher (only substantial new unit)
Public API:
```cpp
void begin(float lat, float lon, const char *place, int kpThreshold);
bool poll(WorldFeeds &out);   // non-blocking; true when any feed updated this call
bool refresh(WorldFeeds &out, const String &which); // force one/all (serial cmd)
```
Behavior:
- `USE_WIFI=1`: on each `poll()`, if a feed's throttle is due, fetch **that
  one feed** (weather 15 min, quakes 5 min, aurora 10 min), **staggered**
  so at most one HTTPS request happens per call and the three never align.
  `setConnectTimeout(4000)`, `setTimeout(5000)`. On success set `*Valid`
  and `*Ms`; on failure leave the last-good value and log.
- `begin()` also starts SNTP (`configTime`) so quake "age" / "updated Xm
  ago" can be computed from epoch timestamps once the clock syncs.
- `USE_WIFI=0` or a failed first fetch: fill canned Eugene values
  (`weatherValid=true` etc.) so the strip always has something to draw.
- Aurora verdict: `kp >= kpThreshold` → `kLikely`; within 1 → `kWatch`;
  else `kQuiet`. Documented as an approximation, tunable per latitude.

### `config/Location.example.h` → `Location.h` (gitignored)
```cpp
#define RELAYOPS_LAT 44.05
#define RELAYOPS_LON -123.09
#define RELAYOPS_PLACE "Eugene, OR"
#define RELAYOPS_KP_THRESHOLD 6   // aurora "likely" at/above this Kp
```
Wired through `ProjectConfig.h` with the same `__has_include` + default
pattern as `WiFiSecrets.h`/`Devices.h`, so builds compile without a local
copy.

### Dashboard — `ControlHubDashboard.{h,cpp}`
- Shorten the sparkline panel; add `drawWorldStrip()` rendering three fixed
  cards below it (weather / quakes / aurora), styled to match the roster.
- `onWorldFeeds(const WorldFeeds&)` stores the feeds and marks dirty.
- Staleness: a feed with no successful fetch in ~3× its interval dims to
  gray, so live vs. stale is obvious. (No-op under `USE_DISPLAY=0`.)

### UI + sketch wiring
- `ControlHubUi::renderWorld(const WorldFeeds&)` — Serial log + forward to
  the dashboard.
- `.ino`: add `WorldFeedClient world;`; `begin()` with `RELAYOPS_LAT/LON/
  PLACE/KP_THRESHOLD`; in `loop()` call `world.poll(feeds)` and, when true,
  `ui.renderWorld(feeds)`. Add a `world` serial command to print current
  values and force a refresh.

## Data flow

```
loop():
  router.poll(); network.maintain(); server.handle(); ui.tick();
  drain touch toggle -> DeviceController
  world.poll(feeds)  ── at most one staggered HTTPS fetch when due ──► Open-Meteo / USGS / NOAA
        │ updated?
        ▼
  ui.renderWorld(feeds) ─► dashboard.onWorldFeeds ─► drawWorldStrip()
```

## Error handling & resilience

- Fetch failure: keep last-good data, mark nothing invalid, log, retry next
  interval. Feed dims only via the staleness rule, not on a single miss.
- Malformed JSON: `deserializeJson` error → treated as a failed fetch.
- No clock yet (SNTP not synced): show quake time as "—" until time is
  valid; everything else still renders.
- Short timeouts bound the worst-case loop stall to ~5 s, at most once per
  fetch interval per feed, never overlapping.

## Verification

This repo has no unit-test harness; it is compile-verified + serial-driven,
so verification matches that:
1. `CTAGS_WORKAROUND=1 ./scripts/compile-all.sh` (mock/default).
2. `EXTRA_FLAGS="-DUSE_WIFI=1"` and `="-DUSE_DISPLAY=1 -DUSE_WIFI=1"` builds;
   add matching rows to `scripts/check-flag-matrix.sh` (P4 wifi row already
   needs ArduinoJson).
3. Offline: `world` serial command prints canned Eugene weather / sample
   quake / Kp; strip renders with `-DUSE_DISPLAY=1`.
4. On hardware: copy `Location.h` + `WiFiSecrets.h`, flash
   `-DUSE_DISPLAY=1 -DUSE_WIFI=1`, watch serial for staggered fetch logs and
   the three cards populating and un-dimming.

## Files

New: `src/WorldFeed.h`, `src/WorldFeedClient.{h,cpp}`,
`config/Location.example.h`.
Edit: `ControlHubDashboard.{h,cpp}`, `ControlHubUi.{h,cpp}`,
`04-relayops-wifi-control-hub.ino`, `config/ProjectConfig.h`, `.gitignore`
(add `**/config/Location.h`), `scripts/check-flag-matrix.sh`, project
`README.md` + `docs/`.

## Follow-up (separate spec)

Companion node sketches: a sensor node that POSTs to the hub and a relay
node that serves `/gpio`, so the whole system is flashable end-to-end.
