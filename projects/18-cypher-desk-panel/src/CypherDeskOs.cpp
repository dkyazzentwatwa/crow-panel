#include "CypherDeskOs.h"

#include "DeskTheme.h"
#include "DeskWidgets.h"
#include <CrowPanelShared.h>

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
#include <Arduino_GFX_Library.h>
// smallText/button/card/progressBar/blend565 - one copy for the project.
using namespace DeskUi;
#endif

namespace {
// 4x4 grid: 13 apps with headroom. The old 3x4 was exactly full when Video was
// added. One set of numbers, used by both drawHome() and handleHomeTouch() -
// the two used to carry duplicate arithmetic that had to be kept in step.
constexpr int16_t kGridLeft = 24;
constexpr int16_t kGridTop = 108;
constexpr int16_t kTileW = 232;
constexpr int16_t kTileH = 100;
constexpr int16_t kTilePitchX = 248;
constexpr int16_t kTilePitchY = 112;
constexpr uint8_t kGridCols = 4;
// Status bar: the strip that repaints every second, and the now-playing pill.
constexpr int16_t kStatusX = 360;
constexpr int16_t kPillRight = 1004;
constexpr int16_t kPillY = 10;
constexpr int16_t kPillH = 30;

const DeskAppDescriptor kHomeApps[13] = {
    {kDeskAppWriter, "WRITER", "notes + focus", 0},
    {kDeskAppToday, "TODAY", "daily command desk", 0},
    {kDeskAppCalendar, "CALENDAR", "local events", 0},
    {kDeskAppContacts, "CONTACTS", "people + business", 0},
    {kDeskAppClock, "CLOCK", "timer + alarm", 0},
    {kDeskAppCalculator, "CALCULATOR", "offline utility", 0},
    {kDeskAppFiles, "FILES", "SD workspace", 0},
    {kDeskAppSettings, "SETTINGS", "Wi-Fi + system", 0},
    {kDeskAppRecorder, "RECORDER", "hardware guarded", 0},
    {kDeskAppMusic, "MUSIC", "SD library", 0},
    {kDeskAppPodcasts, "PODCASTS", "local episodes", 0},
    {kDeskAppVideo, "VIDEO", "MJPEG clips", 0},
    {kDeskAppWeather, "WEATHER", "local forecast", 0}};

constexpr uint8_t kHomeAppCount = sizeof(kHomeApps) / sizeof(kHomeApps[0]);

void homeTileOrigin(uint8_t index, int16_t &x, int16_t &y) {
  x = kGridLeft + (index % kGridCols) * kTilePitchX;
  y = kGridTop + (index / kGridCols) * kTilePitchY;
}

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)

void drawHomeIcon(Arduino_GFX *g, DeskAppId id, int16_t x, int16_t y, uint16_t accent,
                  uint16_t ink) {
  g->fillRoundRect(x, y, 52, 58, 12, accent);
  g->drawRoundRect(x + 6, y + 6, 40, 46, 7, ink);
  const int16_t cx = x + 26;
  const int16_t cy = y + 29;
  switch (id) {
    case kDeskAppWriter:
      g->drawLine(x + 15, y + 18, x + 33, y + 18, ink);
      g->drawLine(x + 15, y + 26, x + 37, y + 26, ink);
      g->drawLine(x + 15, y + 34, x + 28, y + 34, ink);
      g->drawLine(x + 31, y + 38, x + 40, y + 29, ink);
      break;
    case kDeskAppToday:
    case kDeskAppCalendar:
      g->drawRoundRect(x + 14, y + 17, 24, 23, 4, ink);
      g->drawLine(x + 14, y + 24, x + 38, y + 24, ink);
      g->drawLine(x + 20, y + 14, x + 20, y + 20, ink);
      g->drawLine(x + 32, y + 14, x + 32, y + 20, ink);
      if (id == kDeskAppToday) { g->drawLine(x + 20, y + 32, x + 24, y + 36, ink); g->drawLine(x + 24, y + 36, x + 33, y + 28, ink); }
      else { g->fillCircle(x + 21, y + 31, 2, ink); g->fillCircle(x + 31, y + 31, 2, ink); }
      break;
    case kDeskAppContacts:
      g->drawCircle(cx, y + 23, 7, ink);
      g->drawRoundRect(x + 15, y + 31, 22, 12, 6, ink);
      break;
    case kDeskAppClock:
      g->drawCircle(cx, cy, 14, ink);
      g->drawLine(cx, cy, cx, y + 20, ink);
      g->drawLine(cx, cy, x + 34, y + 34, ink);
      break;
    case kDeskAppCalculator:
      g->drawRoundRect(x + 15, y + 15, 22, 28, 3, ink);
      g->drawLine(x + 19, y + 21, x + 33, y + 21, ink);
      for (uint8_t row = 0; row < 2; ++row) for (uint8_t col = 0; col < 3; ++col)
        g->fillCircle(x + 20 + col * 6, y + 29 + row * 7, 1, ink);
      break;
    case kDeskAppFiles:
      g->drawRoundRect(x + 13, y + 23, 27, 17, 3, ink);
      g->drawLine(x + 14, y + 23, x + 24, y + 23, ink);
      g->drawLine(x + 17, y + 19, x + 27, y + 19, ink);
      break;
    case kDeskAppSettings:
      g->drawCircle(cx, cy, 10, ink);
      g->fillCircle(cx, cy, 3, ink);
      g->drawLine(cx, y + 14, cx, y + 18, ink); g->drawLine(cx, y + 40, cx, y + 44, ink);
      g->drawLine(x + 11, cy, x + 15, cy, ink); g->drawLine(x + 37, cy, x + 41, cy, ink);
      break;
    case kDeskAppRecorder:
      g->drawRoundRect(x + 21, y + 15, 10, 22, 5, ink);
      g->drawLine(x + 16, y + 29, x + 16, y + 34, ink);
      g->drawLine(x + 36, y + 29, x + 36, y + 34, ink);
      g->drawLine(x + 16, y + 34, x + 36, y + 34, ink);
      g->drawLine(cx, y + 37, cx, y + 43, ink);
      break;
    case kDeskAppMusic:
      g->drawLine(x + 31, y + 16, x + 31, y + 37, ink);
      g->drawLine(x + 31, y + 16, x + 39, y + 19, ink);
      g->fillCircle(x + 25, y + 38, 4, ink);
      break;
    case kDeskAppPodcasts:
      g->fillCircle(cx, cy, 3, ink);
      g->drawCircle(cx, cy, 9, ink);
      g->drawCircle(cx, cy, 14, ink);
      break;
    case kDeskAppVideo:
      g->drawRoundRect(x + 12, y + 19, 28, 21, 3, ink);
      g->drawLine(x + 40, y + 24, x + 44, y + 20, ink);
      g->drawLine(x + 44, y + 20, x + 44, y + 39, ink);
      g->drawLine(x + 44, y + 39, x + 40, y + 35, ink);
      g->drawLine(x + 40, y + 24, x + 40, y + 35, ink);
      break;
    case kDeskAppWeather:
      g->drawCircle(x + 23, y + 25, 7, ink);
      g->drawLine(x + 23, y + 13, x + 23, y + 17, ink);
      g->drawLine(x + 23, y + 33, x + 23, y + 37, ink);
      g->drawLine(x + 11, y + 25, x + 15, y + 25, ink);
      g->drawLine(x + 31, y + 25, x + 35, y + 25, ink);
      g->drawLine(x + 17, y + 39, x + 40, y + 39, ink);
      g->drawCircle(x + 30, y + 37, 5, ink);
      break;
    default: break;
  }
}
#endif
}

CypherDeskOs::CypherDeskOs()
    : today_(kDeskAppToday), calendar_(kDeskAppCalendar), contacts_(kDeskAppContacts),
      clock_(kDeskAppClock), calculator_(kDeskAppCalculator), files_(kDeskAppFiles),
      settingsApp_(kDeskAppSettings), recorder_(kDeskAppRecorder),
#if USE_CYPHER_DESK_MEDIA
      music_(kDeskAppMusic), podcasts_(kDeskAppPodcasts),
#endif
      weather_(kDeskAppWeather) {}

void CypherDeskOs::registerApplications() {
  DeskApplication *apps[] = {&writer_,  &today_,       &calendar_, &contacts_,
                             &clock_,   &calculator_,  &files_,    &settingsApp_,
                             &recorder_,
#if USE_CYPHER_DESK_MEDIA
                             &music_, &podcasts_,
#endif
#if USE_CYPHER_DESK_VIDEO
                             &video_,
#endif
                             &weather_};
  for (DeskApplication *app : apps) {
    router_.registerApp(app);
    app->begin(context_);
  }
  // The launcher shows exactly what registered, so a build without media or
  // video has no dead tiles and the grid closes up behind them.
  homeTileCount_ = 0;
  for (const DeskAppDescriptor &descriptor : kHomeApps) {
    if (router_.has(descriptor.id) && homeTileCount_ < 16) {
      homeTiles_[homeTileCount_++] = &descriptor;
    }
  }
}
void CypherDeskOs::begin() {
  settings_.begin();
  storage_.begin(&events_);
  wifi_.begin(&events_);
  time_.begin(&wifi_, &events_);
  weatherService_.begin(&wifi_, &storage_, &events_);
  audio_.begin(&events_);
  keyboard_.reset();
  context_ = {&events_, &storage_,        &wifi_,   &settings_, &keyboard_, &touch_,
              &time_,   &audio_,          &weatherService_, &router_,   &writer_};
  registerApplications();
  display_.begin();
  DeskSplash::run(deskTheme(settings_.theme()), "a calm place for words");
  events_.publish(kDeskEventInfo, "Cypher Desk OS booted");
  lastActivityMs_ = millis();
  dirty_ = true;
}
void CypherDeskOs::tick() {
  uint32_t now = millis();
  storage_.tick(now);
  wifi_.tick(now);
  time_.tick(now);
  weatherService_.tick(now);
  audio_.tick();

  // One display tick and one touch poll, before anything branches on which app
  // is active. The Writer used to be excluded here and run its own pair.
  display_.tick();
  touch_.tick();

  DeskAppId current = router_.currentId();
  if (current != lastApp_) { lastApp_ = current; dirty_ = true; }
  DeskApplication *active = router_.current();

  // Keys first: they fire on touch-DOWN and draw their own press art, so they
  // must be resolved before any release-edge chrome tap is delivered.
  serviceKeyboard(active);
  if (active != nullptr) active->tick(now);
  if (current == kDeskAppWriter) return;

  deliverTaps(active, current);
  serviceIdleDim(now);
  if (now - lastStatusRefreshMs_ >= 1000) {
    lastStatusRefreshMs_ = now;
    if (current == kDeskAppHome && !dirty_) drawHomeStatus();
  }
  if (current != router_.currentId()) dirty_ = true;
  drawActive();
}

// Dims the backlight after a quiet spell and wakes on the next contact. The
// tap that wakes the panel must not also fire whatever it landed on, so every
// binding is dropped - otherwise a pending release replays as a keystroke.
void CypherDeskOs::serviceIdleDim(uint32_t nowMs) {
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  if (touch_.activeCount() > 0) {
    lastActivityMs_ = nowMs;
    if (dimmed_) {
      dimmed_ = false;
      CrowDisplay::setBacklight(CYPHER_DESK_BRIGHTNESS);
      touch_.forgetBindings();
      dirty_ = true;
    }
    return;
  }
  if (!dimmed_ && nowMs - lastActivityMs_ >= CYPHER_DESK_IDLE_DIM_MS) {
    dimmed_ = true;
    CrowDisplay::setBacklight(CYPHER_DESK_IDLE_DIM_LEVEL);
  }
#else
  (void)nowMs;
#endif
}

void CypherDeskOs::serviceKeyboard(DeskApplication *active) {
  if (active == nullptr || !active->keyboardVisible()) return;
  keyboard_.service(touch_, display_.canvas(), deskTheme(settings_.theme()), &audio_);
  DeskKeyEvent event;
  while (keyboard_.nextEvent(event)) active->handleKey(event);
  if (keyboard_.consumeRedraw()) dirty_ = true;
}

void CypherDeskOs::deliverTaps(DeskApplication *active, DeskAppId current) {
  // App chrome commits on RELEASE, and only for contacts the keyboard did not
  // already claim (owner >= 0 means a key owns that finger).
  for (uint8_t i = 0; i < DeskTouch::kMaxContacts; ++i) {
    const DeskTouch::Contact &contact = touch_.contact(i);
    if (!contact.releasedEdge || contact.owner >= 0) continue;
    DeskTouchEvent event;
    event.x = contact.x;
    event.y = contact.y;
    event.released = true;
    event.atMs = millis();
    if (current == kDeskAppHome) handleHomeTouch(event);
    else if (active != nullptr) active->handleTouch(event);
    dirty_ = true;
  }
}
void CypherDeskOs::drawActive() {
  DeskApplication *active = router_.current();
  const DeskAppId current = router_.currentId();
  if (current == kDeskAppHome) {
    if (dirty_) { drawHome(); flushFrame(); }
  } else if (active != nullptr) {
    // Both app families keep their own dirty flag; ask whichever one owns this
    // id, so a screen that changed without the OS knowing still repaints.
    DeskUtilityApplication *utilityApp = utility(current);
    bool appDirty = utilityApp != nullptr && utilityApp->dirty();
#if USE_CYPHER_DESK_MEDIA
    DeskMusicApplication *musicApp = music(current);
    appDirty = appDirty || (musicApp != nullptr && musicApp->dirty());
#endif
#if USE_CYPHER_DESK_VIDEO
    DeskVideoApplication *videoApplication = videoApp(current);
    appDirty = appDirty || (videoApplication != nullptr && videoApplication->dirty());
#endif
    if (dirty_ || appDirty) {
      active->draw();
#if USE_CYPHER_DESK_MEDIA
      if (musicApp != nullptr) musicApp->clearDirty();
#endif
#if USE_CYPHER_DESK_VIDEO
      if (videoApplication != nullptr) videoApplication->clearDirty();
#endif
      flushFrame();
    }
  }
  dirty_ = false;
}

// The panel is in manual-flush mode, so nothing reaches the glass until this
// runs. That is what makes per-key press art, the scrub bar and the video
// window able to push their own rectangles without the rest of the screen
// being re-synced behind them.
void CypherDeskOs::flushFrame() {
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  CrowDisplay::flush();
#endif
}

#if USE_CYPHER_DESK_MEDIA
DeskMusicApplication *CypherDeskOs::music(DeskAppId id) {
  if (id == kDeskAppMusic) return &music_;
  if (id == kDeskAppPodcasts) return &podcasts_;
  return nullptr;
}
#endif
#if USE_CYPHER_DESK_VIDEO
DeskVideoApplication *CypherDeskOs::videoApp(DeskAppId id) {
  return id == kDeskAppVideo ? &video_ : nullptr;
}
#endif
void CypherDeskOs::drawHome() {
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  Arduino_GFX *g = display_.canvas();
  if (g == nullptr) return;
  const DeskThemePalette &theme = deskTheme(settings_.theme());
  g->fillScreen(theme.background);
  g->fillRect(0, 0, 1024, 56, theme.shell);
  Widgets::text(g, 24, 11, "CYPHER DESK OS", Widgets::fontL(), theme.ink, Widgets::kLeft);
  drawHomeStatus();
  g->fillRect(0, 52, 256, 4, theme.accent); g->fillRect(256, 52, 256, 4, theme.accent2);
  g->fillRect(512, 52, 256, 4, theme.success); g->fillRect(768, 52, 256, 4, theme.accent3);
  smallText(g, 24, 76,
            String("OFFLINE-FIRST CREATOR COMMAND DESK  //  ") + homeTileCount_ + " APPS",
            theme.accent);
  const uint16_t accents[] = {theme.accent, theme.accent2, theme.success, theme.accent3};
  for (uint8_t index = 0; index < homeTileCount_; ++index) {
    int16_t x, y;
    homeTileOrigin(index, x, y);
    const uint16_t accent = accents[index % 4];
    Widgets::panel(g, x + 4, y + 5, kTileW, kTileH, 14, theme.background);
    Widgets::panel(g, x, y, kTileW, kTileH, 14, theme.panel, 2, accent);
    drawHomeIcon(g, homeTiles_[index]->id, x + 14, y + 20, accent, theme.background);
    smallText(g, x + 78, y + 28, homeTiles_[index]->title, theme.ink);
    smallText(g, x + 78, y + 54, homeTiles_[index]->subtitle, theme.muted);
  }
  DeskEvent last = events_.recent();
  smallText(g, 24, 570, last.message.length() ? last.message : "ready", theme.muted);
#endif
}
// Repainted once a second on its own strip, so a ticking clock and a moving
// now-playing label never cost a full-screen redraw.
void CypherDeskOs::drawHomeStatus() {
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  Arduino_GFX *g = display_.canvas();
  if (g == nullptr) return;
  const DeskThemePalette &theme = deskTheme(settings_.theme());
  g->fillRect(kStatusX, 0, 1024 - kStatusX, 52, theme.shell);

  int16_t cursor = 1000;
  // Now-playing pill, tappable straight through to whichever media app owns
  // the audio - the point of a status bar is that it is a shortcut.
  const String nowPlaying = nowPlayingLabel();
  if (nowPlaying.length()) {
    const int16_t width = smallTextWidth(g, nowPlaying) + 34;
    const int16_t x = kPillRight - width;
    Widgets::panel(g, x, kPillY, width, kPillH, kPillH / 2, theme.accent);
    smallText(g, x + width / 2, kPillY + 7, nowPlaying, theme.onAccent, Widgets::kCenter);
    nowPlayingPillX_ = x;
    nowPlayingPillW_ = width;
    cursor = x - 16;
  } else {
    nowPlayingPillW_ = 0;
  }

  smallText(g, cursor, 18,
            time_.timeText() + "  //  WIFI " + wifi_.stateLabel() + "  //  SD " +
                storage_.stateLabel() + "  //  REC " + (audio_.recording() ? "ON" : "OFF") +
                "  //  ALARM " + (clock_.alarmEnabled() ? "ON" : "OFF"),
            theme.muted, Widgets::kRight);
  CrowDisplay::flush(kStatusX, 0, 1024 - kStatusX, 52);
#endif
}
String CypherDeskOs::nowPlayingLabel() const {
#if USE_CYPHER_DESK_MEDIA
  if (music_.playing()) return music_.nowPlayingLabel();
  if (podcasts_.playing()) return podcasts_.nowPlayingLabel();
#endif
  return String();
}
void CypherDeskOs::handleHomeTouch(const DeskTouchEvent &event) {
  if (!event.released) return;
  // The now-playing pill lives in the status bar, above the grid.
#if USE_CYPHER_DESK_MEDIA
  if (nowPlayingPillW_ > 0 && event.y < 52 && event.x >= nowPlayingPillX_ &&
      event.x < nowPlayingPillX_ + nowPlayingPillW_) {
    router_.open(music_.playing() ? kDeskAppMusic : kDeskAppPodcasts);
    return;
  }
#endif
  if (event.y < kGridTop) return;
  for (uint8_t index = 0; index < homeTileCount_; ++index) {
    int16_t x, y;
    homeTileOrigin(index, x, y);
    if (deskInside(event.x, event.y, x, y, kTileW, kTileH)) {
      router_.open(homeTiles_[index]->id);
      return;
    }
  }
}
DeskAppId CypherDeskOs::appIdFromName(String name) const {
  name.toLowerCase(); name.trim();
  if (name == "home") return kDeskAppHome;
  for (const DeskAppDescriptor &app : kHomeApps) {
    String title(app.title); title.toLowerCase();
    if (name == title) return app.id;
  }
  if (name == "notes" || name == "desk") return kDeskAppWriter;
  return kDeskAppCount;
}
DeskUtilityApplication *CypherDeskOs::utility(DeskAppId id) {
  switch (id) {
    case kDeskAppToday: return &today_; case kDeskAppCalendar: return &calendar_;
    case kDeskAppContacts: return &contacts_; case kDeskAppClock: return &clock_;
    case kDeskAppCalculator: return &calculator_; case kDeskAppFiles: return &files_;
    case kDeskAppSettings: return &settingsApp_; case kDeskAppRecorder: return &recorder_;
    case kDeskAppWeather: return &weather_; default: return nullptr;
  }
}
void CypherDeskOs::ensureWriterOpen() { if (router_.currentId() != kDeskAppWriter) router_.open(kDeskAppWriter); }
void CypherDeskOs::printStatus(Print &out) const {
  out.print(F("[os] app=")); out.print(router_.currentTitle());
  out.print(F(" heap=")); out.print(ESP.getFreeHeap());
  out.print(F(" max_alloc=")); out.println(ESP.getMaxAllocHeap());
  storage_.print(out); wifi_.print(out); weatherService_.print(out); audio_.print(out);
  out.println(F("[proof] os=compile target; writer path preserved; SD/Wi-Fi/audio/microphone require feature-specific device proof"));
}
void CypherDeskOs::printFiles(Print &out) { files_.handleSerial("", out); }
void CypherDeskOs::printTouchDiagnostics(Print &out) const {
  out.print(F("[touch] "));
  out.println(touch_.diagnostics());
}
void CypherDeskOs::commandApp(const String &args, Print &out) {
  DeskAppId id = appIdFromName(args);
  if (id == kDeskAppCount || !router_.open(id)) out.println(F("[os] app home|writer|today|calendar|contacts|clock|calculator|files|settings|recorder|music|podcasts|video|weather"));
  else { out.print(F("[os] opened ")); out.println(router_.currentTitle()); dirty_ = true; }
}
void CypherDeskOs::commandApps(Print &out) const { router_.print(out); }
void CypherDeskOs::commandWifi(const String &argsValue, Print &out) {
  String args = argsValue; args.trim();
  if (args == "scan") wifi_.scan();
  else if (args == "offline") wifi_.setOffline(true);
  else if (args == "online") wifi_.setOffline(false);
  else if (args == "disconnect") wifi_.disconnect();
  else if (args.startsWith("saved ")) wifi_.connectSaved(args.substring(6).toInt() - 1);
  else if (args.startsWith("forget ")) wifi_.forget(args.substring(7).toInt() - 1);
  wifi_.print(out); dirty_ = true;
}
void CypherDeskOs::commandEvents(Print &out) const { events_.print(out); }
void CypherDeskOs::commandCalculator(const String &args, Print &out) { calculator_.handleSerial(args, out); }
void CypherDeskOs::commandCalendar(const String &args, Print &out) { calendar_.handleSerial(args, out); }
void CypherDeskOs::commandContacts(const String &args, Print &out) { contacts_.handleSerial(args, out); }
void CypherDeskOs::commandAlarm(const String &args, Print &out) { clock_.handleSerial(args, out); }
void CypherDeskOs::commandVideo(const String &args, Print &out) {
#if USE_CYPHER_DESK_VIDEO
  video_.command(args, out);
  dirty_ = true;
#else
  (void)args;
  out.println(F("[video] not built: compile with -DUSE_CYPHER_DESK_VIDEO=1"));
#endif
}
void CypherDeskOs::commandMedia(const String &args, Print &out) {
  // Routes to whichever media app is open so an injected command drives the
  // same state a tap does; defaults to Music.
#if USE_CYPHER_DESK_MEDIA
  DeskMusicApplication *app = music(router_.currentId());
  if (app == nullptr) app = &music_;
  app->command(args, out);
#else
  (void)args;
  out.println(F("[media] library not built: compile with -DUSE_CYPHER_DESK_MEDIA=1"));
#endif
  out.print(F("[media] recordings="));
  out.println(storage_.countFiles(CYPHER_DESK_RECORDINGS_DIR, ".wav"));
  audio_.print(out);
  dirty_ = true;
}
void CypherDeskOs::commandAudio(const String &args, Print &out) {
  String value = args; value.toLowerCase();
  bool started = false;
  if (value == "speaker" || value == "tone") started = audio_.startSpeakerTest();
  else if (value == "mic" || value == "microphone" || value == "record") started = audio_.startMicrophoneTest();
  else if (value.startsWith("record ")) started = audio_.startRecording(args.substring(7));
  else if (value == "stop") { audio_.stopRecording(); audio_.stopPlayback(); started = true; }
  else if (value.startsWith("play ")) started = audio_.playWav(args.substring(5));
  else if (value.startsWith("volume ")) { audio_.setVolume(constrain(value.substring(7).toInt(), 0, 100)); started = true; }
  else if (value != "status") out.println(F("[audio] use: audio speaker|mic|record [name]|play <path>|stop|volume N|status"));
  out.print(F("[audio] started=")); out.print(started ? "yes" : "no"); out.print(F(" ")); audio_.print(out);
}
void CypherDeskOs::commandRecovery(const String &args, Print &out) {
  if (args == "run") out.print(F("[recovery] restored=")), out.println(storage_.recoverTransactions());
  else out.println(F("[recovery] use: recovery run"));
}
void CypherDeskOs::commandWeather(const String &argsValue, Print &out) {
  String args = argsValue; args.trim();
  if (args == "refresh") weatherService_.requestRefresh();
  else if (args.startsWith("location ")) {
    String rest = args.substring(9); int first = rest.indexOf(' '); int second = first < 0 ? -1 : rest.indexOf(' ', first + 1);
    if (first > 0) {
      float lat = rest.substring(0, first).toFloat();
      float lon = (second < 0 ? rest.substring(first + 1) : rest.substring(first + 1, second)).toFloat();
      weatherService_.setLocation(lat, lon, second < 0 ? "" : rest.substring(second + 1));
    } else out.println(F("[weather] use: weather location <latitude> <longitude> [label]"));
  } else if (args.length() && args != "status") out.println(F("[weather] use: weather status|refresh|location <latitude> <longitude> [label]"));
  weatherService_.print(out); dirty_ = true;
}
void CypherDeskOs::commandNew(const String &args) { ensureWriterOpen(); writer_.writer().commandNew(args); }
void CypherDeskOs::commandOpen(const String &args) { ensureWriterOpen(); writer_.writer().commandOpen(args); }
void CypherDeskOs::commandType(const String &args) { ensureWriterOpen(); writer_.writer().commandType(args); }
void CypherDeskOs::commandSave() { ensureWriterOpen(); writer_.writer().commandSave(); }
void CypherDeskOs::commandBack() { router_.back(); dirty_ = true; }
void CypherDeskOs::commandDemo() { ensureWriterOpen(); writer_.writer().commandDemo(); }
void CypherDeskOs::commandPage(const String &args) { ensureWriterOpen(); writer_.writer().commandPage(args); }
void CypherDeskOs::commandDaily() { ensureWriterOpen(); writer_.writer().commandDaily(); }
void CypherDeskOs::commandScrap() { ensureWriterOpen(); writer_.writer().commandScrap(); }
void CypherDeskOs::commandFocus(const String &args) { ensureWriterOpen(); writer_.writer().commandFocus(args); }
void CypherDeskOs::commandRitual(const String &args) { ensureWriterOpen(); writer_.writer().commandRitual(args); }
void CypherDeskOs::commandTheme(const String &args) { ensureWriterOpen(); writer_.writer().commandTheme(args); settings_.begin(); dirty_ = true; }
void CypherDeskOs::commandSound(const String &args) { ensureWriterOpen(); writer_.writer().commandSound(args); }
void CypherDeskOs::commandStats(Print &out) { ensureWriterOpen(); writer_.writer().commandStats(out); }
void CypherDeskOs::commandSearch(const String &args, Print &out) { ensureWriterOpen(); writer_.writer().commandSearch(args, out); }
void CypherDeskOs::commandTime(const String &argsValue, Print &out) {
  String args = argsValue; args.trim();
  if (args == "sync") time_.requestSync();
  else if (args.startsWith("zone ")) {
    if (!time_.setTimezone(args.substring(5))) out.println(F("[time] invalid POSIX timezone"));
    out.print(F("[time] timezone=")); out.println(time_.timezone());
    return;
  } else if (args == "timezone") { out.print(F("[time] timezone=")); out.println(time_.timezone()); return; }
  ensureWriterOpen(); writer_.writer().commandTime(args, out);
}
void CypherDeskOs::commandStorage(const String &args, Print &out) { files_.handleSerial(args, out); }
