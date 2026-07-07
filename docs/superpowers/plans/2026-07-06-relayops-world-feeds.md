# RelayOps World Feeds Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add live weather, earthquake, and aurora cards to the project 04 (RelayOps) dashboard, fetched on-device over HTTPS, with a mock-first offline fallback.

**Architecture:** A new `WorldFeedClient` fetches three public no-key HTTPS JSON APIs directly from the panel (`WiFiClientSecure` + `setInsecure()`), one staggered non-blocking request per `loop()` pass. It exposes a `WorldFeeds` snapshot that `ControlHubDashboard` renders as a bottom "world strip" of three cards. `USE_WIFI=0` fills canned Eugene, OR data so everything compiles and demos offline.

**Tech Stack:** Arduino / ESP32-P4 (esp32 core 3.3.8), Arduino_GFX, ArduinoJson 7, WiFiClientSecure + HTTPClient (esp32 core), arduino-cli.

**Note on testing:** This repo has no unit-test harness; it is compile-verified + serial-driven. Each task's "test" step is an `arduino-cli` compile with expected output. Always prefix compiles with `CTAGS_WORKAROUND=1` (see the crow-panel build notes). All paths are under `projects/04-relayops-wifi-control-hub/` unless noted.

**FQBN (used in every compile below):**
```
esp32:esp32:esp32p4:USBMode=hwcdc,PSRAM=enabled,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,UploadSpeed=921600
```

---

## File structure

- Create `config/Location.example.h` — location config template (gitignored `Location.h`).
- Create `src/WorldFeed.h` — `WeatherData`/`QuakeData`/`AuroraData`/`WorldFeeds` structs + `AuroraVerdict`.
- Create `src/WorldFeedClient.{h,cpp}` — the fetcher (mock + HTTPS).
- Modify `config/ProjectConfig.h` — include `Location.h` + defaults.
- Modify `src/ControlHubDashboard.{h,cpp}` — `onWorldFeeds`, `drawWorldStrip`, layout, staleness.
- Modify `src/ControlHubUi.{h,cpp}` — `renderWorld`.
- Modify `04-relayops-wifi-control-hub.ino` — `WorldFeedClient` instance, `loop()` poll, `world` serial command.
- Modify `.gitignore` (repo root) — ignore `**/config/Location.h`.
- Modify `README.md` + `docs/codex-build-notes.md` — document the feature.

---

## Task 1: Location config + ProjectConfig wiring

**Files:**
- Create: `projects/04-relayops-wifi-control-hub/config/Location.example.h`
- Modify: `projects/04-relayops-wifi-control-hub/config/ProjectConfig.h`
- Modify: `.gitignore` (repo root)

- [ ] **Step 1: Create `config/Location.example.h`**

```cpp
#ifndef RELAYOPS_LOCATION_H
#define RELAYOPS_LOCATION_H

// Copy this file to Location.h (gitignored) and set your location. The
// weather card and the aurora "visible?" verdict use it. Defaults are
// Eugene, OR. Only meaningful with -DUSE_WIFI=1; mock mode ignores it.

#define RELAYOPS_LAT 44.05
#define RELAYOPS_LON -123.09
#define RELAYOPS_PLACE "Eugene, OR"

// Aurora is called "likely" at/above this planetary Kp for your latitude.
// Rough guide: ~7-8 for a 45N overhead show; lower to catch it on the
// northern horizon. This is an approximation, not a guarantee.
#define RELAYOPS_KP_THRESHOLD 6

#endif
```

- [ ] **Step 2: Wire it into `config/ProjectConfig.h`**

In `config/ProjectConfig.h`, insert this block immediately BEFORE the existing `#include <AppConfig.h>` line:

```cpp
// Location for the weather + aurora feeds (gitignored copy of
// Location.example.h). Defaults keep every build compiling.
#if __has_include("Location.h")
#include "Location.h"
#endif
#ifndef RELAYOPS_LAT
#define RELAYOPS_LAT 44.05
#endif
#ifndef RELAYOPS_LON
#define RELAYOPS_LON -123.09
#endif
#ifndef RELAYOPS_PLACE
#define RELAYOPS_PLACE "Eugene, OR"
#endif
#ifndef RELAYOPS_KP_THRESHOLD
#define RELAYOPS_KP_THRESHOLD 6
#endif

```

- [ ] **Step 3: Ignore the local copy** — in the repo-root `.gitignore`, under the `# Local Arduino config and secrets` section, add a line after `**/config/Devices.h`:

```
**/config/Location.h
```

- [ ] **Step 4: Compile to verify (mock/baseline)**

Run:
```bash
cd /Users/cypher/Documents/GitHub/crow-panel
CTAGS_WORKAROUND=1 arduino-cli compile \
  --fqbn "esp32:esp32:esp32p4:USBMode=hwcdc,PSRAM=enabled,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,UploadSpeed=921600" \
  --libraries "$(pwd)/shared" \
  --build-property "tools.ctags.cmd.path=/usr/bin/true" \
  projects/04-relayops-wifi-control-hub
```
Expected: ends with `Sketch uses ... bytes` (green). Config-only change; behavior unchanged.

- [ ] **Step 5: Commit**

```bash
git add projects/04-relayops-wifi-control-hub/config/Location.example.h \
        projects/04-relayops-wifi-control-hub/config/ProjectConfig.h .gitignore
git commit -m "feat(relayops): add Location config for world feeds"
```

---

## Task 2: World feed data types

**Files:**
- Create: `projects/04-relayops-wifi-control-hub/src/WorldFeed.h`

- [ ] **Step 1: Create `src/WorldFeed.h`**

```cpp
#ifndef RELAYOPS_WORLD_FEED_H
#define RELAYOPS_WORLD_FEED_H

#include <Arduino.h>

enum AuroraVerdict { kAuroraQuiet = 0, kAuroraWatch = 1, kAuroraLikely = 2 };

struct WeatherData {
  float tempC = NAN;
  float feelsC = NAN;
  float windKt = NAN;
  float hiC = NAN;
  float loC = NAN;
  String condition = "";
};

struct QuakeData {
  float mag = NAN;
  float depthKm = NAN;
  String place = "";
  long ageMin = -1;   // -1 = unknown (clock not synced yet)
  int count24h = -1;  // -1 = unknown
};

struct AuroraData {
  float kp = NAN;
  String level = "";           // NOAA G-scale text, e.g. "G1"
  AuroraVerdict verdict = kAuroraQuiet;
  int trend = 0;               // -1 falling, 0 flat, +1 rising
};

// One snapshot of all three feeds, with per-feed validity + last-update
// millis() for staleness dimming.
struct WorldFeeds {
  WeatherData weather;  bool weatherValid = false;  unsigned long weatherMs = 0;
  QuakeData   quake;    bool quakeValid   = false;  unsigned long quakeMs   = 0;
  AuroraData  aurora;   bool auroraValid  = false;  unsigned long auroraMs  = 0;
};

#endif
```

- [ ] **Step 2: Compile to verify** — same compile command as Task 1 Step 4.
Expected: green `Sketch uses ...` (header is unused so far, but must parse when included later).

- [ ] **Step 3: Commit**

```bash
git add projects/04-relayops-wifi-control-hub/src/WorldFeed.h
git commit -m "feat(relayops): add WorldFeeds data types"
```

---

## Task 3: WorldFeedClient — scaffolding + mock path

Fetch functions are stubs here (return false); Task 6 fills them in. This task makes the offline path fully work.

**Files:**
- Create: `projects/04-relayops-wifi-control-hub/src/WorldFeedClient.h`
- Create: `projects/04-relayops-wifi-control-hub/src/WorldFeedClient.cpp`

- [ ] **Step 1: Create `src/WorldFeedClient.h`**

```cpp
#ifndef RELAYOPS_WORLD_FEED_CLIENT_H
#define RELAYOPS_WORLD_FEED_CLIENT_H

#include "../config/ProjectConfig.h"
#include <Arduino.h>
#include <CrowPanelShared.h>   // Throttle, Logger
#include "WorldFeed.h"

// Pulls weather / earthquake / aurora data from public HTTPS JSON APIs and
// exposes it as a WorldFeeds snapshot for the dashboard. Direct on-device
// fetch (WiFiClientSecure + setInsecure). Non-blocking: poll() issues at
// most ONE HTTPS request per call, and only when that feed is due; the
// three are rotated so they never fire together. With USE_WIFI=0 it fills
// canned values so the strip renders offline.
class WorldFeedClient {
 public:
  void begin(float lat, float lon, const char *place, int kpThreshold);
  bool poll(WorldFeeds &out);                          // true if a feed changed this call
  bool refresh(WorldFeeds &out, const String &which);  // "all"|"weather"|"quake"|"aurora"

 private:
  void fillMock();
  AuroraVerdict verdictFor(float kp) const;
#if USE_WIFI
  bool fetchWeather();
  bool fetchQuake();
  bool fetchAurora();
#endif

  WorldFeeds feeds_;
  float lat_ = 0.0f, lon_ = 0.0f;
  String place_ = "";
  int kpThreshold_ = 6;
  bool primed_ = false;

  // Normal cadence per feed once primed.
  Throttle weatherGate_{15UL * 60UL * 1000UL};
  Throttle quakeGate_{5UL * 60UL * 1000UL};
  Throttle auroraGate_{10UL * 60UL * 1000UL};
  // Until a feed's first success, attempts are spaced by this instead (so a
  // feed appears seconds after boot, not one full interval later).
  Throttle startupGate_{5000};
  uint8_t turn_ = 0;  // rotates which feed is eligible, so at most one/call
};

#endif
```

- [ ] **Step 2: Create `src/WorldFeedClient.cpp` (mock + stubs)**

```cpp
#include "WorldFeedClient.h"

// Fetch path is COMPILE-VERIFIED, NOT HARDWARE-VERIFIED. HTTPS rides the
// onboard ESP32-C6 over esp_hosted. With USE_WIFI=0 the whole thing is a
// canned data source so the dashboard strip renders offline.

void WorldFeedClient::begin(float lat, float lon, const char *place, int kpThreshold) {
  lat_ = lat;
  lon_ = lon;
  place_ = (place != nullptr) ? place : "";
  kpThreshold_ = kpThreshold;
#if USE_WIFI
  // UTC epoch so quake "age" can be computed once the clock syncs.
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  Logger::info("world", "feeds: direct HTTPS, loc=" + place_);
#else
  Logger::info("world", "feeds: mock (USE_WIFI=0), loc=" + place_);
#endif
}

AuroraVerdict WorldFeedClient::verdictFor(float kp) const {
  if (kp >= (float)kpThreshold_) return kAuroraLikely;
  if (kp >= (float)kpThreshold_ - 1.0f) return kAuroraWatch;
  return kAuroraQuiet;
}

void WorldFeedClient::fillMock() {
  feeds_.weather = WeatherData();
  feeds_.weather.tempC = 14.0f;
  feeds_.weather.feelsC = 12.5f;
  feeds_.weather.windKt = 6.0f;
  feeds_.weather.hiC = 18.0f;
  feeds_.weather.loC = 7.0f;
  feeds_.weather.condition = "Partly cloudy";
  feeds_.weatherValid = true;
  feeds_.weatherMs = millis();

  feeds_.quake = QuakeData();
  feeds_.quake.mag = 4.6f;
  feeds_.quake.depthKm = 33.0f;
  feeds_.quake.place = "near Coquimbo, Chile";
  feeds_.quake.ageMin = 12;
  feeds_.quake.count24h = 18;
  feeds_.quakeValid = true;
  feeds_.quakeMs = millis();

  feeds_.aurora = AuroraData();
  feeds_.aurora.kp = 5.0f;
  feeds_.aurora.level = "G1";
  feeds_.aurora.verdict = verdictFor(5.0f);
  feeds_.aurora.trend = 1;
  feeds_.auroraValid = true;
  feeds_.auroraMs = millis();
}

bool WorldFeedClient::poll(WorldFeeds &out) {
#if USE_WIFI
  uint8_t which = turn_ % 3;
  turn_++;
  bool changed = false;
  switch (which) {
    case 0:
      if (feeds_.weatherValid ? weatherGate_.ready() : startupGate_.ready()) changed = fetchWeather();
      break;
    case 1:
      if (feeds_.quakeValid ? quakeGate_.ready() : startupGate_.ready()) changed = fetchQuake();
      break;
    case 2:
      if (feeds_.auroraValid ? auroraGate_.ready() : startupGate_.ready()) changed = fetchAurora();
      break;
  }
  if (changed) out = feeds_;
  return changed;
#else
  if (!primed_) {
    fillMock();
    primed_ = true;
    out = feeds_;
    return true;
  }
  return false;
#endif
}

bool WorldFeedClient::refresh(WorldFeeds &out, const String &which) {
#if USE_WIFI
  bool any = false;
  if (which == "all" || which == "weather") any = fetchWeather() || any;
  if (which == "all" || which == "quake")   any = fetchQuake() || any;
  if (which == "all" || which == "aurora")  any = fetchAurora() || any;
  out = feeds_;
  return any;
#else
  (void)which;
  fillMock();
  out = feeds_;
  return true;
#endif
}

#if USE_WIFI
// Real implementations land in Task 6. Stubs keep USE_WIFI builds linking.
bool WorldFeedClient::fetchWeather() { return false; }
bool WorldFeedClient::fetchQuake() { return false; }
bool WorldFeedClient::fetchAurora() { return false; }
#endif
```

- [ ] **Step 3: Compile to verify (baseline)** — same compile command as Task 1 Step 4.
Expected: green `Sketch uses ...`. (Not wired into the sketch yet, but the file must compile as part of the sketch's `src/`.)

- [ ] **Step 4: Commit**

```bash
git add projects/04-relayops-wifi-control-hub/src/WorldFeedClient.h \
        projects/04-relayops-wifi-control-hub/src/WorldFeedClient.cpp
git commit -m "feat(relayops): WorldFeedClient scaffolding + mock data"
```

---

## Task 4: Wire feeds into the UI and sketch (offline end-to-end)

**Files:**
- Modify: `src/ControlHubUi.h`, `src/ControlHubUi.cpp`
- Modify: `src/ControlHubDashboard.h`, `src/ControlHubDashboard.cpp`
- Modify: `04-relayops-wifi-control-hub.ino`

- [ ] **Step 1: Add `onWorldFeeds` to the dashboard header**

In `src/ControlHubDashboard.h`, add the include near the top (after `#include "HubTypes.h"`):
```cpp
#include "WorldFeed.h"
```
Add this public method declaration right after the existing `void onEvent(const String &message);` line:
```cpp
  void onWorldFeeds(const WorldFeeds &feeds);   // latest weather/quake/aurora
```
Inside the `#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)` private section, add a member near `String banner_`:
```cpp
  WorldFeeds world_;
```

- [ ] **Step 2: Implement `onWorldFeeds` (store + stub, both build variants)**

In `src/ControlHubDashboard.cpp`, in the DISPLAY-enabled section add this method (place it right after `onEvent`):
```cpp
void ControlHubDashboard::onWorldFeeds(const WorldFeeds &feeds) {
  world_ = feeds;
  dirty_ = true;
}
```
In the `#else` no-op section at the bottom, add next to the other stubs:
```cpp
void ControlHubDashboard::onWorldFeeds(const WorldFeeds &) {}
```

- [ ] **Step 3: Add `renderWorld` to the UI wrapper**

In `src/ControlHubUi.h`, add after `#include "ControlHubDashboard.h"`:
```cpp
#include "WorldFeed.h"
```
Add after `void renderEvent(const String &message);`:
```cpp
  void renderWorld(const WorldFeeds &feeds);
```
In `src/ControlHubUi.cpp`, add this method at the end (before the final closing of the file):
```cpp
void ControlHubUi::renderWorld(const WorldFeeds &feeds) {
  Serial.print(F("[screen:world]"));
  if (feeds.weatherValid) {
    Serial.print(F(" wx="));
    Serial.print(feeds.weather.tempC, 1);
    Serial.print(F("C "));
    Serial.print(feeds.weather.condition);
  }
  if (feeds.quakeValid) {
    Serial.print(F(" | quake=M"));
    Serial.print(feeds.quake.mag, 1);
    Serial.print(F(" "));
    Serial.print(feeds.quake.place);
  }
  if (feeds.auroraValid) {
    Serial.print(F(" | Kp="));
    Serial.print(feeds.aurora.kp, 0);
    Serial.print(F(" "));
    Serial.print(feeds.aurora.level);
  }
  Serial.println();
  dashboard_.onWorldFeeds(feeds);
}
```

- [ ] **Step 4: Instantiate + poll in the sketch, add `world` command**

In `04-relayops-wifi-control-hub.ino`:

(a) Add the include after `#include "src/MockSensorSource.h"`:
```cpp
#include "src/WorldFeedClient.h"
```
(b) Add globals after `SerialCommandRouter router;`:
```cpp
WorldFeedClient world;
WorldFeeds worldFeeds;
```
(c) Add this command handler after `cmdFeed`:
```cpp
void cmdWorld(const String &args) {
  String which = args;
  which.trim();
  if (which.length() == 0) which = "all";
  Logger::info("cmd", "world refresh " + which);
  if (world.refresh(worldFeeds, which)) {
    ui.renderWorld(worldFeeds);
  }
}
```
(d) In `setup()`, after `ui.begin();` and its device-paint loop, add:
```cpp
  world.begin(RELAYOPS_LAT, RELAYOPS_LON, RELAYOPS_PLACE, RELAYOPS_KP_THRESHOLD);
```
(e) In `setup()`, register the command after the `router.on("feed", ...)` line:
```cpp
  router.on("world", "print/refresh weather, quakes, aurora", cmdWorld);
```
(f) In `loop()`, after the `#if !USE_WIFI ... mockSource.poll ... #endif` block and before `delay(20);`, add:
```cpp
  if (world.poll(worldFeeds)) {
    ui.renderWorld(worldFeeds);
  }
```

- [ ] **Step 5: Compile to verify (baseline)** — same compile command as Task 1 Step 4.
Expected: green `Sketch uses ...`. In mock mode, `world.poll` fills canned data once → `ui.renderWorld` logs `[screen:world] wx=14.0C Partly cloudy | quake=M4.6 ... | Kp=5 G1`.

- [ ] **Step 6: Commit**

```bash
git add projects/04-relayops-wifi-control-hub/src/ControlHubUi.h \
        projects/04-relayops-wifi-control-hub/src/ControlHubUi.cpp \
        projects/04-relayops-wifi-control-hub/src/ControlHubDashboard.h \
        projects/04-relayops-wifi-control-hub/src/ControlHubDashboard.cpp \
        projects/04-relayops-wifi-control-hub/04-relayops-wifi-control-hub.ino
git commit -m "feat(relayops): wire world feeds through UI + world serial command"
```

---

## Task 5: Draw the world strip on the dashboard

**Files:**
- Modify: `src/ControlHubDashboard.h`, `src/ControlHubDashboard.cpp`

- [ ] **Step 1: Declare the draw helpers**

In `src/ControlHubDashboard.h`, inside the DISPLAY private section, add after `void drawSparkline();`:
```cpp
  void drawWorldStrip();
  void drawWorldCard(int16_t x, int16_t w, const char *label, const String &big,
                     const String &sub, bool valid, unsigned long ms, uint16_t accent);
```

- [ ] **Step 2: Shrink the sparkline and add strip layout constants**

In `src/ControlHubDashboard.cpp`, in the anonymous `namespace` layout block, REPLACE the sparkline constant line:
```cpp
constexpr int16_t kSparkH = 104;
```
with:
```cpp
constexpr int16_t kSparkH = 60;             // shortened to make room for the world strip
constexpr int16_t kWorldY = 472;
constexpr int16_t kWorldH = 60;
constexpr unsigned long kWorldStaleMs = 45UL * 60UL * 1000UL;  // dim if no update in 45 min
```

- [ ] **Step 3: Call `drawWorldStrip` from repaint + periodic refresh**

In `repaint()`, add after the `drawSparkline();` call:
```cpp
  drawWorldStrip();
```
In `tick()`, inside the `if (clockRefresh.ready())` block, add `drawWorldStrip();` after `drawFooter();` so staleness dims over time:
```cpp
    drawHeaderStatus();
    drawRoster();
    drawFooter();
    drawWorldStrip();
```

- [ ] **Step 4: Implement the strip (DISPLAY section)**

In `src/ControlHubDashboard.cpp`, add these two methods right after `drawSparkline()`'s definition:
```cpp
void ControlHubDashboard::drawWorldCard(int16_t x, int16_t w, const char *label,
                                        const String &big, const String &sub, bool valid,
                                        unsigned long ms, uint16_t accent) {
  Arduino_GFX *g = CrowDisplay::canvas();
  if (!g) return;
  bool stale = !valid || (millis() - ms) > kWorldStaleMs;
  uint16_t bodyColor = stale ? kTextMut : kTextHi;
  uint16_t accentColor = stale ? kLine : accent;

  panel(g, x, kWorldY, w, kWorldH, 8, kSurface);
  text(g, x + 10, kWorldY + 8, label, fontS(), accentColor, kLeft);
  text(g, x + 10, kWorldY + 22, fit(g, big, fontL(), w - 20).c_str(), fontL(), bodyColor, kLeft);
  text(g, x + 10, kWorldY + 44, fit(g, sub, fontS(), w - 20).c_str(), fontS(), kTextMut, kLeft);
}

void ControlHubDashboard::drawWorldStrip() {
  Arduino_GFX *g = CrowDisplay::canvas();
  if (!g) return;
  g->fillRect(kRX, kWorldY, kRW, kWorldH, kBg);

  const int16_t gap = 8;
  const int16_t cw = (kRW - 2 * gap) / 3;

  String wxBig = world_.weatherValid ? (String(world_.weather.tempC, 0) + "C") : "--";
  drawWorldCard(kRX, cw, "WEATHER", wxBig, world_.weather.condition,
                world_.weatherValid, world_.weatherMs, kAccent);

  String qBig = world_.quakeValid ? ("M" + String(world_.quake.mag, 1)) : "--";
  drawWorldCard(kRX + (cw + gap), cw, "QUAKES", qBig, world_.quake.place,
                world_.quakeValid, world_.quakeMs, kAmber);

  String aBig = world_.auroraValid ? ("Kp " + String(world_.aurora.kp, 0)) : "--";
  uint16_t aColor = world_.aurora.verdict == kAuroraLikely ? kGreen
                    : world_.aurora.verdict == kAuroraWatch ? kAmber : kTextMut;
  drawWorldCard(kRX + 2 * (cw + gap), cw, "AURORA", aBig, world_.aurora.level,
                world_.auroraValid, world_.auroraMs, aColor);
}
```

- [ ] **Step 5: Compile to verify (display on)**

Run:
```bash
cd /Users/cypher/Documents/GitHub/crow-panel
CTAGS_WORKAROUND=1 arduino-cli compile \
  --fqbn "esp32:esp32:esp32p4:USBMode=hwcdc,PSRAM=enabled,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,UploadSpeed=921600" \
  --libraries "$(pwd)/shared" \
  --build-property "compiler.cpp.extra_flags=-DUSE_DISPLAY=1" \
  --build-property "tools.ctags.cmd.path=/usr/bin/true" \
  projects/04-relayops-wifi-control-hub
```
Expected: green `Sketch uses ...`. Also re-run the baseline compile (Task 1 Step 4) to confirm the no-display stub still builds.

- [ ] **Step 6: Commit**

```bash
git add projects/04-relayops-wifi-control-hub/src/ControlHubDashboard.h \
        projects/04-relayops-wifi-control-hub/src/ControlHubDashboard.cpp
git commit -m "feat(relayops): render weather/quake/aurora world strip"
```

---

## Task 6: Real HTTPS fetch implementations

Replaces the three stubs from Task 3 with live fetches and adds the HTTPS/JSON includes.

**Files:**
- Modify: `src/WorldFeedClient.cpp`

- [ ] **Step 1: Add the USE_WIFI includes + helpers**

In `src/WorldFeedClient.cpp`, replace the line `#include "WorldFeedClient.h"` and the comment block below it with:
```cpp
#include "WorldFeedClient.h"

#if USE_WIFI
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>

namespace {
const char *weatherText(int code) {
  switch (code) {
    case 0: return "Clear sky";
    case 1: case 2: return "Partly cloudy";
    case 3: return "Overcast";
    case 45: case 48: return "Fog";
    case 51: case 53: case 55: return "Drizzle";
    case 61: case 63: case 65: return "Rain";
    case 66: case 67: return "Freezing rain";
    case 71: case 73: case 75: case 77: return "Snow";
    case 80: case 81: case 82: return "Showers";
    case 85: case 86: return "Snow showers";
    case 95: case 96: case 99: return "Thunderstorm";
    default: return "--";
  }
}
const char *gLevel(float kp) {
  int k = (int)(kp + 0.5f);
  if (k >= 9) return "G5";
  if (k == 8) return "G4";
  if (k == 7) return "G3";
  if (k == 6) return "G2";
  if (k == 5) return "G1";
  return "calm";
}
}  // namespace
#endif
```

- [ ] **Step 2: Replace the three stub definitions**

At the bottom of `src/WorldFeedClient.cpp`, REPLACE this block:
```cpp
#if USE_WIFI
// Real implementations land in Task 6. Stubs keep USE_WIFI builds linking.
bool WorldFeedClient::fetchWeather() { return false; }
bool WorldFeedClient::fetchQuake() { return false; }
bool WorldFeedClient::fetchAurora() { return false; }
#endif
```
with:
```cpp
#if USE_WIFI
bool WorldFeedClient::fetchWeather() {
  String url = String("https://api.open-meteo.com/v1/forecast?latitude=") + String(lat_, 4) +
               "&longitude=" + String(lon_, 4) +
               "&current=temperature_2m,apparent_temperature,weather_code,wind_speed_10m" +
               "&daily=temperature_2m_max,temperature_2m_min&wind_speed_unit=kn" +
               "&temperature_unit=celsius&timezone=auto&forecast_days=1";
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setConnectTimeout(4000);
  http.setTimeout(5000);
  if (!http.begin(client, url)) return false;
  int code = http.GET();
  if (code != 200) {
    http.end();
    Logger::warn("world", "weather HTTP " + String(code));
    return false;
  }

  JsonDocument filter;
  filter["current"]["temperature_2m"] = true;
  filter["current"]["apparent_temperature"] = true;
  filter["current"]["weather_code"] = true;
  filter["current"]["wind_speed_10m"] = true;
  filter["daily"]["temperature_2m_max"] = true;
  filter["daily"]["temperature_2m_min"] = true;

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, http.getStream(),
                                             DeserializationOption::Filter(filter));
  http.end();
  if (err) {
    Logger::warn("world", String("weather json ") + err.c_str());
    return false;
  }

  feeds_.weather.tempC = doc["current"]["temperature_2m"] | NAN;
  feeds_.weather.feelsC = doc["current"]["apparent_temperature"] | NAN;
  feeds_.weather.windKt = doc["current"]["wind_speed_10m"] | NAN;
  feeds_.weather.condition = weatherText(doc["current"]["weather_code"] | -1);
  feeds_.weather.hiC = doc["daily"]["temperature_2m_max"][0] | NAN;
  feeds_.weather.loC = doc["daily"]["temperature_2m_min"][0] | NAN;
  feeds_.weatherValid = true;
  feeds_.weatherMs = millis();
  Logger::info("world", "weather " + String(feeds_.weather.tempC, 1) + "C " + feeds_.weather.condition);
  return true;
}

bool WorldFeedClient::fetchQuake() {
  const char *url = "https://earthquake.usgs.gov/earthquakes/feed/v1.0/summary/4.5_day.geojson";
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setConnectTimeout(4000);
  http.setTimeout(5000);
  if (!http.begin(client, url)) return false;
  int code = http.GET();
  if (code != 200) {
    http.end();
    Logger::warn("world", "quake HTTP " + String(code));
    return false;
  }

  // Filter keeps only newest-event fields + the 24h count. In a GeoJSON
  // "past day" feed features are ordered newest-first, so features[0] is the
  // most recent. The filter's [0] pattern applies to every element, which is
  // fine - the feed is small.
  JsonDocument filter;
  filter["metadata"]["count"] = true;
  filter["features"][0]["properties"]["mag"] = true;
  filter["features"][0]["properties"]["place"] = true;
  filter["features"][0]["properties"]["time"] = true;
  filter["features"][0]["geometry"]["coordinates"] = true;

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, http.getStream(),
                                             DeserializationOption::Filter(filter));
  http.end();
  if (err) {
    Logger::warn("world", String("quake json ") + err.c_str());
    return false;
  }

  feeds_.quake.count24h = doc["metadata"]["count"] | -1;
  JsonArray feats = doc["features"].as<JsonArray>();
  if (feats.isNull() || feats.size() == 0) {
    Logger::warn("world", "quake feed empty");
    return false;
  }
  JsonObject f0 = feats[0];
  feeds_.quake.mag = f0["properties"]["mag"] | NAN;
  feeds_.quake.place = (const char *)(f0["properties"]["place"] | "");
  feeds_.quake.depthKm = f0["geometry"]["coordinates"][2] | NAN;

  long long tms = f0["properties"]["time"] | 0LL;  // epoch milliseconds
  time_t now = time(nullptr);
  if (now > 1600000000L && tms > 0) {
    feeds_.quake.ageMin = (long)((now - (time_t)(tms / 1000)) / 60);
  } else {
    feeds_.quake.ageMin = -1;
  }
  feeds_.quakeValid = true;
  feeds_.quakeMs = millis();
  Logger::info("world", "quake M" + String(feeds_.quake.mag, 1) + " " + feeds_.quake.place);
  return true;
}

bool WorldFeedClient::fetchAurora() {
  const char *url = "https://services.swpc.noaa.gov/products/noaa-planetary-k-index.json";
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setConnectTimeout(4000);
  http.setTimeout(5000);
  if (!http.begin(client, url)) return false;
  int code = http.GET();
  if (code != 200) {
    http.end();
    Logger::warn("world", "aurora HTTP " + String(code));
    return false;
  }

  // Array of ["time_tag","Kp","a_running","station_count"] rows; row 0 is a
  // header, the last row is the most recent reading.
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, http.getStream());
  http.end();
  if (err) {
    Logger::warn("world", String("aurora json ") + err.c_str());
    return false;
  }
  JsonArray rows = doc.as<JsonArray>();
  if (rows.isNull() || rows.size() < 2) {
    Logger::warn("world", "aurora feed short");
    return false;
  }
  JsonArray last = rows[rows.size() - 1];
  JsonArray prev = rows[rows.size() - 2];
  float kp = String((const char *)(last[1] | "0")).toFloat();
  float kpPrev = String((const char *)(prev[1] | "0")).toFloat();

  feeds_.aurora.kp = kp;
  feeds_.aurora.level = gLevel(kp);
  feeds_.aurora.verdict = verdictFor(kp);
  feeds_.aurora.trend = (kp > kpPrev) ? 1 : (kp < kpPrev) ? -1 : 0;
  feeds_.auroraValid = true;
  feeds_.auroraMs = millis();
  Logger::info("world", "aurora Kp " + String(kp, 1) + " " + feeds_.aurora.level);
  return true;
}
#endif
```

- [ ] **Step 3: Compile to verify (wifi, then display+wifi)**

Run (wifi only):
```bash
cd /Users/cypher/Documents/GitHub/crow-panel
CTAGS_WORKAROUND=1 arduino-cli compile \
  --fqbn "esp32:esp32:esp32p4:USBMode=hwcdc,PSRAM=enabled,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,UploadSpeed=921600" \
  --libraries "$(pwd)/shared" \
  --build-property "compiler.cpp.extra_flags=-DUSE_WIFI=1" \
  --build-property "tools.ctags.cmd.path=/usr/bin/true" \
  projects/04-relayops-wifi-control-hub
```
Then display+wifi (change the flags line to `-DUSE_DISPLAY=1 -DUSE_WIFI=1`).
Expected: both green `Sketch uses ...`.

- [ ] **Step 4: Commit**

```bash
git add projects/04-relayops-wifi-control-hub/src/WorldFeedClient.cpp
git commit -m "feat(relayops): live weather/USGS/NOAA fetches over HTTPS"
```

---

## Task 7: Docs + full flag matrix

**Files:**
- Modify: `projects/04-relayops-wifi-control-hub/README.md`
- Modify: `projects/04-relayops-wifi-control-hub/docs/codex-build-notes.md`

- [ ] **Step 1: Document in the project README**

In `projects/04-relayops-wifi-control-hub/README.md`, under the `## Serial Commands` list, add a bullet after the `feed` entry:
```markdown
- `world [all|weather|quake|aurora]` — print / force-refresh the internet feeds. Mock mode returns canned Eugene, OR data; `USE_WIFI` fetches live.
```
Add a new section after the "Wi-Fi mode" section:
```markdown
## World feeds (weather / earthquakes / aurora)

The bottom strip shows live internet data pulled directly by the panel over
HTTPS (`WiFiClientSecure`, no key): weather (Open-Meteo), the newest M4.5+
earthquake (USGS), and the planetary Kp / aurora verdict (NOAA SWPC). Set
your location in `config/Location.h` (copied from `Location.example.h`).
Fetches are staggered and non-blocking; with `USE_WIFI=0` the strip shows
canned data so the demo runs offline. Compile-verified, not hardware-verified.
```

- [ ] **Step 2: Note it in the build notes**

In `projects/04-relayops-wifi-control-hub/docs/codex-build-notes.md`, under the `## Libraries` section, add:
```markdown
- The world-feeds path (`WorldFeedClient`, `USE_WIFI`) uses `WiFiClientSecure`
  + `HTTPClient` (esp32 core) and `ArduinoJson` with a `Filter` to parse only
  the shown fields. `setInsecure()` skips cert validation (public read-only
  data). `configTime()` provides UTC for quake age. `USE_WIFI=0` compiles none
  of this and serves canned data.
```

- [ ] **Step 3: Run the full flag matrix**

The existing `P4|wifi` and `P4|kitchen-sink` rows already require `ArduinoJson`, and the world feeds add no new library, so no matrix edit is needed. Verify nothing regressed:
```bash
cd /Users/cypher/Documents/GitHub/crow-panel
CTAGS_WORKAROUND=1 ./scripts/check-flag-matrix.sh
```
Expected: final summary lists every row as `PASS` (exit 0).

- [ ] **Step 4: Commit**

```bash
git add projects/04-relayops-wifi-control-hub/README.md \
        projects/04-relayops-wifi-control-hub/docs/codex-build-notes.md
git commit -m "docs(relayops): document world feeds"
```

---

## Manual verification (after all tasks)

1. **Offline:** flash `-DUSE_DISPLAY=1` (no wifi). The strip shows canned Eugene weather / M4.6 Chile / Kp 5. `world` over serial reprints it.
2. **Live:** copy `config/Location.h` + `config/WiFiSecrets.h`, flash `-DUSE_DISPLAY=1 -DUSE_WIFI=1`. Within ~15 s of Wi-Fi connecting, watch serial for staggered `world` fetch logs (`weather ...`, `quake M...`, `aurora Kp ...`) and the three cards populate and un-dim. `world weather` forces a single refresh.
3. Confirm inbound `POST /sensor` and device toggles still respond promptly (the staggered fetches should not visibly stall the UI).
