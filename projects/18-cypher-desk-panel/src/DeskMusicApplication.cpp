#include "DeskMusicApplication.h"

#include "DeskAppRouter.h"
#include "DeskSettings.h"
#include "DeskSystemServices.h"
#include "DeskTheme.h"
#include "DeskWidgets.h"

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
#include <Arduino_GFX_Library.h>
#include <CrowPanelShared.h>
using namespace DeskUi;
#endif

namespace {

// --- Layout, shared by draw and hit-test ------------------------------------
constexpr int16_t kHomeX = 12, kHomeY = 7, kHomeW = 112, kHomeH = 38;
constexpr int16_t kRowX = 40, kRowY = 122, kRowW = 944, kRowH = 54, kRowGap = 8;
constexpr int16_t kPrevPageX = 40, kPagerY = 508, kPagerW = 150, kPagerH = 48;
constexpr int16_t kNextPageX = 200;
constexpr int16_t kNowButtonX = 834, kNowButtonW = 150;

// Now-playing view.
constexpr int16_t kScrubX = 60, kScrubY = 286, kScrubW = 904, kScrubH = 10;
constexpr int16_t kTransportY = 340, kTransportH = 66;
constexpr int16_t kPrevX = 236, kPlayX = 402, kNextX = 568, kTransportW = 148;
constexpr int16_t kShuffleX = 60, kRepeatX = 236, kToggleY = 430, kToggleW = 160,
                  kToggleH = 54;
constexpr int16_t kVolDownX = 660, kVolUpX = 830, kVolW = 150;
constexpr int16_t kLibraryButtonX = 834, kLibraryButtonY = 7;

String clockText(uint32_t milliseconds) {
  const uint32_t totalSeconds = milliseconds / 1000;
  const uint32_t minutes = totalSeconds / 60;
  const uint32_t seconds = totalSeconds % 60;
  String text = String(minutes);
  text += ":";
  if (seconds < 10) text += "0";
  text += String(seconds);
  return text;
}

}  // namespace

DeskMusicApplication::DeskMusicApplication(DeskAppId appId) : appId_(appId) {}
DeskAppId DeskMusicApplication::id() const { return appId_; }
const char *DeskMusicApplication::title() const {
  return appId_ == kDeskAppPodcasts ? "Podcasts" : "Music";
}
const char *DeskMusicApplication::directory() const {
  return appId_ == kDeskAppPodcasts ? CYPHER_DESK_PODCASTS_DIR : CYPHER_DESK_MUSIC_DIR;
}

void DeskMusicApplication::begin(DeskAppContext &context) { context_ = &context; }

void DeskMusicApplication::onEnter() {
  refresh();
  view_ = kLibrary;
  dirty_ = true;
}

void DeskMusicApplication::refresh() {
  if (context_ == nullptr) return;
  library_.scan(context_->storage, directory(), DeskMediaLibrary::kAudio);
  if (selected_ >= library_.count()) selected_ = 0;
  if (pageOffset_ >= library_.count()) pageOffset_ = 0;
}

bool DeskMusicApplication::dirty() const { return dirty_; }
void DeskMusicApplication::clearDirty() { dirty_ = false; }
bool DeskMusicApplication::playing() const {
  return context_ != nullptr && context_->audio != nullptr && context_->audio->playing();
}

String DeskMusicApplication::nowPlayingLabel() const {
  if (current_ >= library_.count()) return "";
  return library_.entry(current_).name;
}

bool DeskMusicApplication::play(uint8_t index) {
  if (context_ == nullptr || context_->audio == nullptr) return false;
  if (index >= library_.count()) return false;
  const DeskMediaLibrary::Entry &entry = library_.entry(index);
  if (!entry.playable) {
    status_ = entry.name + ": " + entry.format;
    dirty_ = true;
    return false;
  }
  if (!context_->audio->playWav(entry.path, kDeskAudioOwnerMusic)) {
    status_ = context_->audio->testStatus();
    dirty_ = true;
    return false;
  }
  current_ = index;
  selected_ = index;
  status_ = "";
  view_ = kNowPlaying;
  dirty_ = true;
  return true;
}

void DeskMusicApplication::advance(bool forward) {
  if (library_.playableCount() == 0) return;
  uint8_t next;
  if (shuffle_ && forward) {
    // Seed from the current index so the order is stable within a session but
    // differs between tracks.
    next = library_.shuffleNext(current_, static_cast<uint32_t>(current_) * 2654435761u + 7u);
  } else {
    next = forward ? library_.nextPlayable(current_) : library_.previousPlayable(current_);
  }
  if (next != 0xFF) play(next);
}

void DeskMusicApplication::trackFinished() {
  if (repeat_ == kRepeatOne) {
    play(current_);
    return;
  }
  if (repeat_ == kRepeatAll || shuffle_) {
    advance(true);
    return;
  }
  const uint8_t next = library_.nextPlayable(current_);
  // Repeat off: stop at the end of the list rather than wrapping round to the
  // first track forever.
  if (next != 0xFF && next > current_) play(next);
  else dirty_ = true;
}

void DeskMusicApplication::tick(uint32_t nowMs) {
  if (context_ == nullptr || context_->audio == nullptr) return;
  // The service releases ownership when a file plays out; that is the signal
  // the track ended rather than a timer we would have to keep in sync.
  if (current_ != 0xFF && !context_->audio->playing() && !context_->audio->paused() &&
      context_->audio->owner() == kDeskAudioOwnerNone) {
    trackFinished();
    return;
  }
  if (view_ == kNowPlaying && nowMs - lastProgressMs_ >= 500) {
    lastProgressMs_ = nowMs;
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
    // Repaint only the scrub row, so a moving progress bar does not cost a
    // full-screen redraw twice a second.
    if (!dirty_) drawProgress();
#endif
  }
}

bool DeskMusicApplication::handleBack() {
  if (view_ == kNowPlaying) {
    view_ = kLibrary;
    dirty_ = true;
    return true;
  }
  return false;
}

bool DeskMusicApplication::handleTouch(const DeskTouchEvent &event) {
  if (!event.released || context_ == nullptr) return false;
  const int16_t x = event.x;
  const int16_t y = event.y;
  DeskAudioService *audio = context_->audio;

  if (deskInside(x, y, kHomeX, kHomeY, kHomeW, kHomeH)) {
    if (context_->router != nullptr) context_->router->home();
    return true;
  }

  if (view_ == kLibrary) {
    if (deskInside(x, y, kNowButtonX, kHomeY, kNowButtonW, kHomeH)) {
      if (current_ != 0xFF) { view_ = kNowPlaying; dirty_ = true; }
      return true;
    }
    for (uint8_t row = 0; row < kRowsPerPage; ++row) {
      const uint8_t index = pageOffset_ + row;
      if (index >= library_.count()) break;
      if (deskInside(x, y, kRowX, kRowY + row * (kRowH + kRowGap), kRowW, kRowH)) {
        play(index);
        return true;
      }
    }
    if (deskInside(x, y, kPrevPageX, kPagerY, kPagerW, kPagerH)) {
      pageOffset_ = pageOffset_ >= kRowsPerPage ? pageOffset_ - kRowsPerPage : 0;
      dirty_ = true;
      return true;
    }
    if (deskInside(x, y, kNextPageX, kPagerY, kPagerW, kPagerH)) {
      if (pageOffset_ + kRowsPerPage < library_.count()) pageOffset_ += kRowsPerPage;
      dirty_ = true;
      return true;
    }
    return true;
  }

  // --- Now playing ---
  if (deskInside(x, y, kLibraryButtonX, kLibraryButtonY, kNowButtonW, kHomeH)) {
    view_ = kLibrary;
    dirty_ = true;
    return true;
  }
  // Scrub. The touch target is much taller than the drawn bar - a 10 px strip
  // is not something a finger can hit.
  if (audio != nullptr && deskInside(x, y, kScrubX, kScrubY - 24, kScrubW, kScrubH + 48)) {
    const uint32_t duration = audio->playbackDurationMs();
    if (duration > 0) {
      int32_t offset = x - kScrubX;
      if (offset < 0) offset = 0;
      if (offset > kScrubW) offset = kScrubW;
      audio->seekMs(static_cast<uint32_t>((static_cast<uint64_t>(duration) * offset) / kScrubW));
      dirty_ = true;
    }
    return true;
  }
  if (deskInside(x, y, kPrevX, kTransportY, kTransportW, kTransportH)) { advance(false); return true; }
  if (deskInside(x, y, kNextX, kTransportY, kTransportW, kTransportH)) { advance(true); return true; }
  if (deskInside(x, y, kPlayX, kTransportY, kTransportW, kTransportH)) {
    if (audio != nullptr) {
      if (!audio->playing() && !audio->paused()) play(current_);
      else audio->setPaused(!audio->paused());
      dirty_ = true;
    }
    return true;
  }
  if (deskInside(x, y, kShuffleX, kToggleY, kToggleW, kToggleH)) {
    shuffle_ = !shuffle_;
    dirty_ = true;
    return true;
  }
  if (deskInside(x, y, kRepeatX, kToggleY, kToggleW, kToggleH)) {
    repeat_ = static_cast<Repeat>((repeat_ + 1) % 3);
    dirty_ = true;
    return true;
  }
  if (audio != nullptr && deskInside(x, y, kVolDownX, kToggleY, kVolW, kToggleH)) {
    audio->setVolume(audio->volume() >= 5 ? audio->volume() - 5 : 0);
    if (context_->settings != nullptr) context_->settings->setVolume(audio->volume());
    dirty_ = true;
    return true;
  }
  if (audio != nullptr && deskInside(x, y, kVolUpX, kToggleY, kVolW, kToggleH)) {
    audio->setVolume(audio->volume() + 5);
    if (context_->settings != nullptr) context_->settings->setVolume(audio->volume());
    dirty_ = true;
    return true;
  }
  return true;
}

void DeskMusicApplication::command(const String &args, Print &out) {
  String value = args;
  value.trim();
  value.toLowerCase();
  DeskAudioService *audio = context_ != nullptr ? context_->audio : nullptr;

  if (value == "list" || value.length() == 0) {
    refresh();
    out.print(F("[media] "));
    out.print(directory());
    out.print(F(" // "));
    out.println(library_.summary());
    for (uint8_t i = 0; i < library_.count(); ++i) {
      const DeskMediaLibrary::Entry &entry = library_.entry(i);
      out.print(F("[media] "));
      out.print(i);
      out.print(i == current_ ? F(" * ") : F("   "));
      out.print(entry.name);
      out.print(F("  "));
      out.print(entry.playable ? clockText(entry.durationMs) : String("--:--"));
      out.print(F("  "));
      out.println(entry.format);
    }
    return;
  }
  if (value == "next") { advance(true); out.println(F("[media] next")); return; }
  if (value == "prev" || value == "previous") { advance(false); out.println(F("[media] previous")); return; }
  if (value == "stop") { if (audio) audio->stopPlayback(); current_ = 0xFF; dirty_ = true; out.println(F("[media] stopped")); return; }
  if (value == "pause") { if (audio) audio->setPaused(true); dirty_ = true; out.println(F("[media] paused")); return; }
  if (value == "resume" || value == "play") {
    if (audio && audio->paused()) audio->setPaused(false);
    else if (current_ != 0xFF) play(current_);
    dirty_ = true;
    out.println(F("[media] playing"));
    return;
  }
  if (value == "shuffle") { shuffle_ = !shuffle_; dirty_ = true; out.print(F("[media] shuffle ")); out.println(shuffle_ ? "on" : "off"); return; }
  if (value == "repeat") {
    repeat_ = static_cast<Repeat>((repeat_ + 1) % 3);
    dirty_ = true;
    out.print(F("[media] repeat "));
    out.println(repeat_ == kRepeatOff ? "off" : repeat_ == kRepeatAll ? "all" : "one");
    return;
  }
  if (value.startsWith("seek ")) {
    if (audio) audio->seekMs(static_cast<uint32_t>(value.substring(5).toInt()) * 1000UL);
    dirty_ = true;
    out.println(F("[media] seek"));
    return;
  }
  // A bare number plays that track.
  const int index = value.toInt();
  if (index >= 0 && index < library_.count() && (index != 0 || value.startsWith("0"))) {
    if (play(static_cast<uint8_t>(index))) out.println(String("[media] playing ") + library_.entry(index).name);
    else out.println(String("[media] ") + status_);
    return;
  }
  out.println(F("[media] list|<n>|play|pause|resume|stop|next|prev|shuffle|repeat|seek <seconds>"));
}

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)

void DeskMusicApplication::drawShell(const String &subtitle) {
  Arduino_GFX *g = CrowDisplay::canvas();
  if (g == nullptr || context_ == nullptr) return;
  const DeskThemePalette &theme = deskTheme(context_->settings->theme());
  g->fillScreen(theme.background);
  g->fillRect(0, 0, 1024, 52, theme.shell);
  button(g, theme, kHomeX, kHomeY, kHomeW, kHomeH, "OS HOME");
  smallText(g, 146, 17, title(), theme.ink);
  g->fillRect(0, 50, 256, 4, theme.accent);
  g->fillRect(256, 50, 256, 4, theme.accent2);
  g->fillRect(512, 50, 256, 4, theme.success);
  g->fillRect(768, 50, 256, 4, theme.accent3);
  smallText(g, 28, 78, subtitle, theme.muted);
}

void DeskMusicApplication::draw() {
  if (view_ == kNowPlaying) drawNowPlaying();
  else drawLibrary();
}

void DeskMusicApplication::drawLibrary() {
  Arduino_GFX *g = CrowDisplay::canvas();
  if (g == nullptr || context_ == nullptr) return;
  const DeskThemePalette &theme = deskTheme(context_->settings->theme());
  drawShell(String(directory()) + "  //  " + library_.summary());
  button(g, theme, kNowButtonX, kHomeY, kNowButtonW, kHomeH, "NOW PLAYING", current_ != 0xFF);

  if (library_.count() == 0) {
    card(g, theme, 160, 200, 704, 180, theme.warning);
    smallText(g, 512, 250, "NO PLAYABLE FILES ON THE CARD", theme.ink, Widgets::kCenter);
    smallText(g, 512, 290, String("Copy WAV files to ") + directory(), theme.muted,
              Widgets::kCenter);
    smallText(g, 512, 322, "Any PCM WAV, 8-48 kHz, mono or stereo, 8- or 16-bit.", theme.muted,
              Widgets::kCenter);
    return;
  }

  for (uint8_t row = 0; row < kRowsPerPage; ++row) {
    const uint8_t index = pageOffset_ + row;
    if (index >= library_.count()) break;
    const DeskMediaLibrary::Entry &entry = library_.entry(index);
    const int16_t y = kRowY + row * (kRowH + kRowGap);
    const bool active = index == current_;
    Widgets::panel(g, kRowX + 3, y + 4, kRowW, kRowH, 12, theme.background);
    Widgets::panel(g, kRowX, y, kRowW, kRowH, 12,
                   active ? theme.panelHighlight : theme.panel, active ? 3 : 1,
                   entry.playable ? (active ? theme.accent : theme.line) : theme.warning);
    smallText(g, kRowX + 22, y + 12, entry.name, entry.playable ? theme.ink : theme.muted);
    // The reason an unplayable file will not play sits where its duration
    // would be, so the list explains itself instead of just failing on tap.
    smallText(g, kRowX + kRowW - 22,
              y + 12, entry.playable ? clockText(entry.durationMs) : entry.format,
              entry.playable ? theme.accent : theme.warning, Widgets::kRight);
    smallText(g, kRowX + 22, y + 32, entry.format, theme.muted);
  }

  const bool hasPrevious = pageOffset_ > 0;
  const bool hasNext = pageOffset_ + kRowsPerPage < library_.count();
  button(g, theme, kPrevPageX, kPagerY, kPagerW, kPagerH, "PREV", hasPrevious);
  button(g, theme, kNextPageX, kPagerY, kPagerW, kPagerH, "NEXT", hasNext);
  if (status_.length()) smallText(g, 380, kPagerY + 18, status_, theme.warning);
}

void DeskMusicApplication::drawNowPlaying() {
  Arduino_GFX *g = CrowDisplay::canvas();
  if (g == nullptr || context_ == nullptr || context_->audio == nullptr) return;
  const DeskThemePalette &theme = deskTheme(context_->settings->theme());
  DeskAudioService *audio = context_->audio;
  drawShell("NOW PLAYING");
  button(g, theme, kLibraryButtonX, kLibraryButtonY, kNowButtonW, kHomeH, "LIBRARY");

  const String name = current_ < library_.count() ? library_.entry(current_).name : String("nothing loaded");
  card(g, theme, 60, 120, 904, 130, theme.accent);
  Widgets::text(g, 92, 156, name.c_str(), Widgets::fontXL(), theme.ink, Widgets::kLeft);
  smallText(g, 92, 208, audio->playbackFormat().length() ? audio->playbackFormat() : String("--"),
            theme.muted);
  smallText(g, 936, 208, audio->status(), theme.accent, Widgets::kRight);

  drawProgress();

  const bool isPlaying = audio->playing();
  button(g, theme, kPrevX, kTransportY, kTransportW, kTransportH, "PREV");
  button(g, theme, kPlayX, kTransportY, kTransportW, kTransportH, isPlaying ? "PAUSE" : "PLAY",
         isPlaying);
  button(g, theme, kNextX, kTransportY, kTransportW, kTransportH, "NEXT");

  button(g, theme, kShuffleX, kToggleY, kToggleW, kToggleH, "SHUFFLE", shuffle_);
  button(g, theme, kRepeatX, kToggleY, kToggleW, kToggleH,
         repeat_ == kRepeatOff ? "REPEAT OFF" : repeat_ == kRepeatAll ? "REPEAT ALL" : "REPEAT ONE",
         repeat_ != kRepeatOff);
  button(g, theme, kVolDownX, kToggleY, kVolW, kToggleH, "VOL -");
  button(g, theme, kVolUpX, kToggleY, kVolW, kToggleH, "VOL +");
  smallText(g, 512, kToggleY + 20, String("VOLUME ") + audio->volume() + "%", theme.ink,
            Widgets::kCenter);

  if (audio->underruns() > 0) {
    smallText(g, 512, 560, String("underruns: ") + audio->underruns(), theme.warning,
              Widgets::kCenter);
  }
}

void DeskMusicApplication::drawProgress() {
  Arduino_GFX *g = CrowDisplay::canvas();
  if (g == nullptr || context_ == nullptr || context_->audio == nullptr) return;
  const DeskThemePalette &theme = deskTheme(context_->settings->theme());
  DeskAudioService *audio = context_->audio;
  const uint32_t duration = audio->playbackDurationMs();
  const uint32_t position = audio->playbackPositionMs();
  const uint16_t fraction =
      duration ? static_cast<uint16_t>((static_cast<uint64_t>(position) * 1000ULL) / duration) : 0;

  // Clear only this strip, then repaint it. The knob overhangs the bar, so the
  // cleared band has to be taller than kScrubH.
  g->fillRect(kScrubX - 16, kScrubY - 30, kScrubW + 32, kScrubH + 56, theme.background);
  smallText(g, kScrubX, kScrubY - 26, clockText(position), theme.muted);
  smallText(g, kScrubX + kScrubW, kScrubY - 26, clockText(duration), theme.muted, Widgets::kRight);
  progressBar(g, theme, kScrubX, kScrubY, kScrubW, kScrubH, fraction, theme.accent);
  CrowDisplay::flush(kScrubX - 16, kScrubY - 30, kScrubW + 32, kScrubH + 56);
}

#else  // USE_DISPLAY

void DeskMusicApplication::draw() {}

#endif
