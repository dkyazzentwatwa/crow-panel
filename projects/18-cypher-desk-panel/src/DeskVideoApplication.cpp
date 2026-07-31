#include "DeskVideoApplication.h"

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

constexpr int16_t kHomeX = 12, kHomeY = 7, kHomeW = 112, kHomeH = 38;
constexpr int16_t kRowX = 40, kRowY = 122, kRowW = 944, kRowH = 60, kRowGap = 10;
constexpr uint8_t kRowsPerPage = 5;
constexpr int16_t kPrevPageX = 40, kPagerY = 502, kPagerW = 150, kPagerH = 48;
constexpr int16_t kNextPageX = 200;

// Player view. The video window is the whole area between the chrome bars, so
// hiding the chrome does not move the picture.
constexpr int16_t kVideoX = 0, kVideoY = 60, kVideoW = 1024, kVideoH = 452;
constexpr int16_t kScrubX = 60, kScrubY = 528, kScrubW = 904, kScrubH = 8;
constexpr int16_t kStopX = 12, kPauseX = 148, kBackX = 872, kBarW = 128, kBarH = 38;
// Tapping anywhere in the picture toggles the chrome.
constexpr uint32_t kChromeAutoHideMs = 4000;

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

void DeskVideoApplication::begin(DeskAppContext &context) { context_ = &context; }

void DeskVideoApplication::onEnter() {
  refresh();
  view_ = kLibrary;
  chromeVisible_ = true;
  dirty_ = true;
}

void DeskVideoApplication::onExit() {
  // Leaving the app must not leave a clip streaming off the card in the
  // background - it owns the audio voice and would block Music.
  player_.stop();
  current_ = 0xFF;
}

void DeskVideoApplication::refresh() {
  if (context_ == nullptr) return;
  library_.scan(context_->storage, CYPHER_DESK_VIDEO_DIR, DeskMediaLibrary::kVideo);
}

bool DeskVideoApplication::play(uint8_t index) {
  if (context_ == nullptr || index >= library_.count()) return false;
  String reason;
  player_.setWindow(kVideoX, kVideoY, kVideoW, kVideoH);
  if (!player_.play(library_.entry(index).path, context_->audio, reason)) {
    status_ = library_.entry(index).name + ": " + reason;
    dirty_ = true;
    return false;
  }
  current_ = index;
  selected_ = index;
  status_ = reason;
  view_ = kPlayer;
  chromeVisible_ = true;
  lastChromeMs_ = millis();
  dirty_ = true;
  return true;
}

void DeskVideoApplication::tick(uint32_t nowMs) {
  player_.tick();
  if (view_ != kPlayer) return;
  // Fade the chrome out of the way on its own so a clip fills the panel
  // without the operator having to remember to hide it.
  if (chromeVisible_ && player_.playing() && nowMs - lastChromeMs_ >= kChromeAutoHideMs) {
    chromeVisible_ = false;
    dirty_ = true;
  }
  if (!player_.playing() && current_ != 0xFF) {
    view_ = kLibrary;
    current_ = 0xFF;
    dirty_ = true;
  }
}

bool DeskVideoApplication::handleBack() {
  if (view_ == kPlayer) {
    player_.stop();
    view_ = kLibrary;
    current_ = 0xFF;
    dirty_ = true;
    return true;
  }
  return false;
}

bool DeskVideoApplication::handleTouch(const DeskTouchEvent &event) {
  if (!event.released || context_ == nullptr) return false;
  const int16_t x = event.x;
  const int16_t y = event.y;

  if (view_ == kLibrary) {
    if (deskInside(x, y, kHomeX, kHomeY, kHomeW, kHomeH)) {
      if (context_->router != nullptr) context_->router->home();
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

  // --- Player ---
  if (!chromeVisible_) {
    chromeVisible_ = true;
    lastChromeMs_ = millis();
    dirty_ = true;
    return true;
  }
  lastChromeMs_ = millis();
  if (deskInside(x, y, kStopX, kHomeY, kBarW, kBarH)) { handleBack(); return true; }
  if (deskInside(x, y, kPauseX, kHomeY, kBarW, kBarH)) {
    player_.setPaused(!player_.paused());
    dirty_ = true;
    return true;
  }
  if (deskInside(x, y, kBackX, kHomeY, kBarW, kBarH)) {
    chromeVisible_ = false;
    dirty_ = true;
    return true;
  }
  // The scrub bar is drawn 8 px tall but the target has to be finger-sized.
  if (deskInside(x, y, kScrubX, kScrubY - 22, kScrubW, kScrubH + 44)) {
    // Seeking an MJPEG stream means re-walking the movi list from the start,
    // which at these bitrates costs more than it is worth mid-playback. The
    // bar is honest about being a position read-out for now.
    status_ = "seek is not available for video yet";
    dirty_ = true;
    return true;
  }
  chromeVisible_ = false;
  dirty_ = true;
  return true;
}

void DeskVideoApplication::command(const String &args, Print &out) {
  String value = args;
  value.trim();
  value.toLowerCase();
  if (value == "list" || value.length() == 0) {
    refresh();
    out.print(F("[video] "));
    out.print(CYPHER_DESK_VIDEO_DIR);
    out.print(F(" // "));
    out.println(library_.summary());
    for (uint8_t i = 0; i < library_.count(); ++i) {
      const DeskMediaLibrary::Entry &entry = library_.entry(i);
      out.print(F("[video] "));
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
  if (value == "stop") { handleBack(); out.println(F("[video] stopped")); return; }
  if (value == "pause") { player_.setPaused(true); out.println(F("[video] paused")); return; }
  if (value == "resume" || value == "play") { player_.setPaused(false); out.println(F("[video] playing")); return; }
  if (value == "status") {
    out.print(F("[video] "));
    out.print(player_.status());
    out.print(F(" presented="));
    out.print(player_.presentedFrames());
    out.print(F(" dropped="));
    out.print(player_.droppedFrames());
    out.print(F(" position="));
    out.println(clockText(player_.positionMs()));
    return;
  }
  const int index = value.toInt();
  if (index >= 0 && index < library_.count() && (index != 0 || value.startsWith("0"))) {
    if (play(static_cast<uint8_t>(index))) out.println(String("[video] playing ") + library_.entry(index).name);
    else out.println(String("[video] ") + status_);
    return;
  }
  out.println(F("[video] list|<n>|play|pause|stop|status"));
}

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)

void DeskVideoApplication::draw() {
  if (view_ == kPlayer) drawPlayer();
  else drawLibrary();
}

void DeskVideoApplication::drawLibrary() {
  Arduino_GFX *g = CrowDisplay::canvas();
  if (g == nullptr || context_ == nullptr) return;
  const DeskThemePalette &theme = deskTheme(context_->settings->theme());
  g->fillScreen(theme.background);
  g->fillRect(0, 0, 1024, 52, theme.shell);
  button(g, theme, kHomeX, kHomeY, kHomeW, kHomeH, "OS HOME");
  smallText(g, 146, 17, "Video", theme.ink);
  g->fillRect(0, 50, 256, 4, theme.accent);
  g->fillRect(256, 50, 256, 4, theme.accent2);
  g->fillRect(512, 50, 256, 4, theme.success);
  g->fillRect(768, 50, 256, 4, theme.accent3);
  smallText(g, 28, 78, String(CYPHER_DESK_VIDEO_DIR) + "  //  " + library_.summary(), theme.muted);

  if (library_.count() == 0) {
    card(g, theme, 160, 190, 704, 200, theme.warning);
    smallText(g, 512, 236, "NO CLIPS ON THE CARD", theme.ink, Widgets::kCenter);
    smallText(g, 512, 272, String("Copy MJPEG .avi files to ") + CYPHER_DESK_VIDEO_DIR,
              theme.muted, Widgets::kCenter);
    smallText(g, 512, 306, "scripts/convert-crowpanel-video.sh converts anything ffmpeg reads.",
              theme.muted, Widgets::kCenter);
    smallText(g, 512, 340, "Project 02's own VID_*.AVI recordings play here too (silent).",
              theme.muted, Widgets::kCenter);
    return;
  }

  for (uint8_t row = 0; row < kRowsPerPage; ++row) {
    const uint8_t index = pageOffset_ + row;
    if (index >= library_.count()) break;
    const DeskMediaLibrary::Entry &entry = library_.entry(index);
    const int16_t y = kRowY + row * (kRowH + kRowGap);
    const bool active = index == current_;
    Widgets::panel(g, kRowX + 3, y + 4, kRowW, kRowH, 12, theme.background);
    Widgets::panel(g, kRowX, y, kRowW, kRowH, 12, active ? theme.panelHighlight : theme.panel,
                   active ? 3 : 1, active ? theme.accent : theme.line);
    smallText(g, kRowX + 22, y + 14, entry.name, theme.ink);
    smallText(g, kRowX + 22, y + 36, String(entry.bytes / 1024) + " KB", theme.muted);
    // Format and duration come from the scan, not from re-opening the clip -
    // this runs on every redraw.
    smallText(g, kRowX + kRowW - 22, y + 14, entry.format,
              entry.playable ? theme.accent : theme.warning, Widgets::kRight);
    if (entry.playable) {
      smallText(g, kRowX + kRowW - 22, y + 36, clockText(entry.durationMs), theme.muted,
                Widgets::kRight);
    }
  }

  button(g, theme, kPrevPageX, kPagerY, kPagerW, kPagerH, "PREV", pageOffset_ > 0);
  button(g, theme, kNextPageX, kPagerY, kPagerW, kPagerH, "NEXT",
         pageOffset_ + kRowsPerPage < library_.count());
  if (status_.length()) smallText(g, 380, kPagerY + 18, status_, theme.warning);
}

void DeskVideoApplication::drawPlayer() {
  Arduino_GFX *g = CrowDisplay::canvas();
  if (g == nullptr || context_ == nullptr) return;
  const DeskThemePalette &theme = deskTheme(context_->settings->theme());
  // Only the letterbox margins are cleared here; the player owns the picture
  // rectangle and flushes it itself every frame.
  g->fillRect(0, 0, 1024, kVideoY, theme.background);
  g->fillRect(0, kVideoY + kVideoH, 1024, 600 - (kVideoY + kVideoH), theme.background);
  if (chromeVisible_) drawChrome();
}

void DeskVideoApplication::drawChrome() {
  Arduino_GFX *g = CrowDisplay::canvas();
  if (g == nullptr || context_ == nullptr) return;
  const DeskThemePalette &theme = deskTheme(context_->settings->theme());
  const DeskAviInfo &info = player_.info();

  button(g, theme, kStopX, kHomeY, kBarW, kBarH, "STOP");
  button(g, theme, kPauseX, kHomeY, kBarW, kBarH, player_.paused() ? "PLAY" : "PAUSE",
         !player_.paused());
  button(g, theme, kBackX, kHomeY, kBarW, kBarH, "HIDE UI");
  smallText(g, 300, kHomeY + 12,
            current_ < library_.count() ? library_.entry(current_).name : String(""), theme.ink);

  const uint32_t duration = info.durationMs();
  const uint32_t position = player_.positionMs();
  const uint16_t fraction =
      duration ? static_cast<uint16_t>((static_cast<uint64_t>(position) * 1000ULL) / duration) : 0;
  smallText(g, kScrubX, kScrubY - 20, clockText(position), theme.muted);
  smallText(g, kScrubX + kScrubW, kScrubY - 20, clockText(duration), theme.muted, Widgets::kRight);
  progressBar(g, theme, kScrubX, kScrubY, kScrubW, kScrubH, fraction, theme.accent);

  // Dropped frames are reported rather than hidden - on this panel it is the
  // number that tells you whether the clip is encoded within budget.
  String line = info.describe();
  line += "  //  presented ";
  line += player_.presentedFrames();
  line += ", dropped ";
  line += player_.droppedFrames();
  smallText(g, 512, 566, line, player_.droppedFrames() > player_.presentedFrames() / 10
                                   ? theme.warning
                                   : theme.muted,
            Widgets::kCenter);
  if (status_.length()) smallText(g, 512, 546, status_, theme.warning, Widgets::kCenter);
}

#else  // USE_DISPLAY

void DeskVideoApplication::draw() {}

#endif
