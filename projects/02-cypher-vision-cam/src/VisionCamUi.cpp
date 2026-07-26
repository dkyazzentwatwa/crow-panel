// Cypher Vision Cam touch console. COMPILE-VERIFIED on esp32:esp32:esp32p4
// (core 3.3.8). NOT HARDWARE-VERIFIED - no screen or touch has been observed.

#include "VisionCamUi.h"

const char *camScreenName(CamScreen s) {
  switch (s) {
    case CAM_SCR_LIVE: return "live";
    case CAM_SCR_GALLERY: return "gallery";
    case CAM_SCR_STREAM: return "stream";
    case CAM_SCR_SETTINGS: return "settings";
    default: return "?";
  }
}

void VisionCamUi::setAutoExposure(bool on, bool converged) {
  if (on != autoExposure_ || converged != autoExposureConverged_) markDirty();
  autoExposure_ = on;
  autoExposureConverged_ = converged;
}

void VisionCamUi::setRecording(bool on, uint32_t elapsedSec, uint32_t droppedFrames) {
  if (on != recording_) markDirty();
  recording_ = on;
  recElapsedSec_ = elapsedSec;
  recDropped_ = droppedFrames;
}

void VisionCamUi::setStreamState(bool up, const String &ssid, const String &url,
                                 uint8_t clients, uint8_t stations,
                                 const String &stationUrl) {
  if (up != streamUp_ || clients != streamClients_ || stations != streamStations_ ||
      stationUrl != stationUrl_) {
    markDirty();
  }
  streamUp_ = up;
  streamSsid_ = ssid;
  streamUrl_ = url;
  streamClients_ = clients;
  streamStations_ = stations;
  stationUrl_ = stationUrl;
}

void VisionCamUi::printTouchDiagnostics(Print &out) const {
  out.print(F("[touch] raw=("));
  out.print(touch_.rawX());
  out.print(',');
  out.print(touch_.rawY());
  out.print(F(") mapped=("));
  out.print(touch_.x());
  out.print(',');
  out.print(touch_.y());
  out.print(F(") down="));
  out.print(touch_.down() ? F("yes") : F("no"));
  out.print(F(" taps="));
  out.print(touch_.count());
  out.print(F(" screen="));
  out.println(screenName());
}

void VisionCamUi::renderSerial(Print &out) const {
  out.print(F("[screen] "));
  out.println(screenName());
  switch (screen_) {
    case CAM_SCR_LIVE:
      out.print(F("  camera="));
      out.print(CrowCamera::streaming() ? F("streaming") : F("idle"));
      out.print(F(" fps="));
      out.print(fps_, 1);
      out.print(F(" frames="));
      out.print(CrowCamera::frameCount());
      out.print(F(" dropped="));
      out.println(CrowCamera::dropCount());
      out.print(F("  recording="));
      out.println(recording_ ? F("yes") : F("no"));
      break;
    case CAM_SCR_GALLERY:
      if (recorder_ == nullptr || !recorder_->storageReady()) {
        out.println(F("  gallery: no card"));
      } else {
        out.print(F("  gallery: "));
        out.print(recorder_->mediaCount());
        out.print(F(" file(s), "));
        out.print((uint32_t)(recorder_->freeBytes() / (1024ULL * 1024ULL)));
        out.println(F(" MB free"));
      }
      break;
    case CAM_SCR_STREAM:
      out.print(F("  ap="));
      out.print(streamUp_ ? F("up") : F("down"));
      out.print(F(" ssid="));
      out.print(streamSsid_.length() ? streamSsid_ : String("-"));
      out.print(F(" url="));
      out.print(streamUrl_.length() ? streamUrl_ : String("-"));
      out.print(F(" clients="));
      out.println(streamClients_);
      break;
    case CAM_SCR_SETTINGS: {
      Sc2336Sensor *s = CrowCamera::sensor();
      if (s != nullptr) {
        s->printStatus(out);
      } else {
        out.println(F("  no sensor"));
      }
      break;
    }
    default: break;
  }
}

// ===========================================================================
//  Rendering + touch - display builds only.
// ===========================================================================
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)

#include <Arduino_GFX_Library.h>

namespace {
using namespace Widgets;

// Physical panel, which never changes. Rotation changes the LOGICAL canvas the
// UI draws into, not the hardware.
constexpr int16_t kPanelW = 1024;
constexpr int16_t kPanelH = 600;

const char *const kTabs[CAM_SCR_COUNT] = {"LIVE", "GALLERY", "STREAM", "SETTINGS"};

constexpr uint8_t kNavCount = 3;
const char *const kNavLabels[kNavCount] = {"GALLERY", "STREAM", "SETTINGS"};
const CamScreen kNavTargets[kNavCount] = {CAM_SCR_GALLERY, CAM_SCR_STREAM,
                                          CAM_SCR_SETTINGS};

// --- Layout ------------------------------------------------------------------
//
// Every position is a runtime value rather than a constant, because the device
// supports being held either way up. In portrait the logical canvas is 600x1024
// and Arduino_GFX's setRotation maps draws into the same 1024x600 framebuffer.
//
// The shared headerBar/tabBar helpers CANNOT be used in portrait: they hardcode
// kChromeW=1024/kChromeH=600 (DashboardWidgets.h), so they would draw off the
// edge of a 600-wide canvas. Portrait therefore uses project-local chrome, which
// is also the reason those two helpers are only called on landscape screens.
struct Layout {
  bool portrait;
  int16_t w, h;              // logical canvas
  int16_t headerH, tabH;
  int16_t contentTop, contentBot;

  // Live control bar. In portrait this is TWO rows - 600 px cannot hold three
  // actions, a status cluster and three nav targets side by side without
  // everything becoming unreadably narrow.
  int16_t barH, barY;
  int16_t btnH, btnY;      // shutter row
  int16_t actionY;         // REC / PAUSE row (== btnY in landscape)
  int16_t shutterX, shutterW;
  int16_t recX, camX, actionW;
  int16_t navX, navY, navW;  // navY == btnY in landscape
  int16_t statusX, statusY;

  // List rows, shared by Gallery / Stream / Settings.
  int16_t rowX, rowW, rowH;
  int16_t setTop;
  int16_t stepW;
  uint8_t galleryRows;
  int16_t galleryRowH;
};

Layout gL;

void computeLayout(bool portrait) {
  Layout &l = gL;
  l.portrait = portrait;
  l.w = portrait ? kPanelH : kPanelW;   // 600 : 1024
  l.h = portrait ? kPanelW : kPanelH;   // 1024 : 600
  l.headerH = kChromeHeaderH;           // 72, same either way
  l.tabH = kChromeTabH;                 // 64
  l.contentTop = l.headerH;
  l.contentBot = l.h - l.tabH;

  l.btnH = 52;
  if (portrait) {
    // THREE rows. 600 px will not hold shutter + two actions + three nav
    // targets side by side at a size worth aiming at, and the shutter is the
    // one control that must never be missed - so it gets a full-width row of
    // its own, actions get the second, navigation the third.
    constexpr int16_t kGap = 8;
    l.barH = kGap + 52 + kGap + 52 + kGap + 52 + kGap;  // 188
    l.barY = l.h - l.barH;
    l.btnY = l.barY + kGap;
    l.actionY = l.btnY + l.btnH + kGap;
    l.navY = l.actionY + l.btnH + kGap;

    l.shutterX = 14;
    l.shutterW = l.w - 2 * l.shutterX;
    // Two equal actions across the width.
    l.actionW = (l.w - 3 * 14) / 2;
    l.recX = 14;
    l.camX = l.recX + l.actionW + 14;
    // Three equal nav targets.
    l.navW = (l.w - 28) / kNavCount;
    l.navX = 14;
    // No room for a status line in the bar; the header pill carries frame rate
    // in portrait, so repeating it here would only cost a row.
    l.statusX = -1;
    l.statusY = -1;
  } else {
    l.barH = 76;
    l.barY = l.h - l.barH;
    l.btnY = l.barY + 12;
    l.shutterW = 132;
    l.actionW = 108;
    l.shutterX = 20;
    l.recX = l.shutterX + l.shutterW + 12;
    l.camX = l.recX + l.actionW + 12;
    l.navW = 122;
    l.navX = l.w - (l.navW * kNavCount) - 20;
    l.navY = l.btnY;
    l.actionY = l.btnY;  // one row: everything shares it
    l.statusX = l.camX + l.actionW + 20;
    l.statusY = l.btnY;
  }

  l.rowH = 64;
  l.rowX = portrait ? 20 : 40;
  l.rowW = l.w - 2 * l.rowX;
  l.setTop = l.contentTop + 24;
  l.stepW = portrait ? 76 : 90;
  l.galleryRowH = 56;
  // Portrait has far more vertical room, so it lists more per page.
  l.galleryRows = portrait ? 12 : 6;
}

// Settings has eight rows, which do NOT fit in one landscape column: eight
// times (64 + 12) from y=96 ends at 692, well past the tab bar at 536. Landscape
// therefore uses two columns of four, which the 1024 px width easily affords.
// Portrait has 1024 px of height and keeps a single column.
// Eight rows, 0-7: exposure, auto-exposure, flip, white balance, BOOT shutter,
// orientation, sound volume, battery. Landscape splits them across two columns
// of at most five (five rows end at y=464, inside the content area that stops
// at 536); portrait keeps one column, where eight rows end at 692 with 960
// available. Both were checked arithmetically before flashing.
constexpr uint8_t kSettingsRows = 8;
constexpr uint8_t kSettingsPerCol = 5;

uint8_t settingsCol(uint8_t index) {
  return gL.portrait ? 0 : (uint8_t)(index / kSettingsPerCol);
}

int16_t settingsRowY(uint8_t index) {
  const uint8_t row = gL.portrait ? index : (uint8_t)(index % kSettingsPerCol);
  return gL.setTop + row * (gL.rowH + 12);
}

int16_t settingsRowW() {
  return gL.portrait ? gL.rowW : (int16_t)((gL.rowW - 16) / 2);
}

int16_t settingsRowX(uint8_t index) {
  return gL.rowX + settingsCol(index) * (settingsRowW() + 16);
}

// Cheap order-sensitive mixer for building a screen signature. Not a hash in
// any cryptographic sense - it only has to change when the inputs change.
inline uint32_t mix(uint32_t h, uint32_t v) {
  h ^= v + 0x9E3779B9u + (h << 6) + (h >> 2);
  return h;
}

}  // namespace

// Returns true when `sig` differs from the last one recorded, and records it.
// One-shot by design: asking twice in a frame would report "unchanged" the
// second time and skip the draw.
bool VisionCamUi::takeIfChanged_(uint32_t sig) {
  if (sig == lastSig_ && !chromeDirty_) return false;
  lastSig_ = sig;
  return true;
}

// Every value the Settings screen renders. Anything shown but absent here would
// stop updating on screen, so this list has to track drawSettings_.
uint32_t VisionCamUi::settingsSignature_() const {
  uint32_t h = 0x5E7F1u;
  Sc2336Sensor *s = CrowCamera::sensor();
  if (s != nullptr) {
    h = mix(h, s->exposure());
    h = mix(h, (uint32_t)s->flippedVertically() | ((uint32_t)s->flippedHorizontally() << 1));
  }
  float r = 1.0f, g = 1.0f, b = 1.0f;
  CrowCamera::colorGains(r, g, b);
  h = mix(h, (uint32_t)(r * 100.0f));
  h = mix(h, (uint32_t)(b * 100.0f));
  h = mix(h, whitePatches_);
  h = mix(h, (uint32_t)autoExposure_ | ((uint32_t)autoExposureConverged_ << 1));
  h = mix(h, shutterPresses_);
  h = mix(h, shutterAltPresses_);
  h = mix(h, (uint32_t)shutterLevel_ | ((uint32_t)shutterAltLevel_ << 1));
  h = mix(h, (uint32_t)gL.portrait | ((uint32_t)soundEnabled_ << 1));
  h = mix(h, soundVolume_);
  return h;
}

uint32_t VisionCamUi::streamSignature_() const {
  uint32_t h = 0x57DEAu;
  h = mix(h, (uint32_t)streamUp_);
  h = mix(h, streamClients_);
  h = mix(h, streamStations_);
  // Strings change rarely; length plus first character is enough to notice.
  h = mix(h, streamSsid_.length() * 131u + (streamSsid_.length() ? streamSsid_[0] : 0));
  h = mix(h, streamUrl_.length() * 131u + (streamUrl_.length() ? streamUrl_[0] : 0));
  h = mix(h, stationUrl_.length() * 131u + (stationUrl_.length() ? stationUrl_[0] : 0));
  return h;
}

uint32_t VisionCamUi::gallerySignature_() const {
  uint32_t h = 0x6A11Eu;
  if (recorder_ == nullptr) return h;
  h = mix(h, (uint32_t)recorder_->storageReady());
  h = mix(h, recorder_->mediaCount());
  h = mix(h, galleryPage_);
  // Free space moves as captures land, which is the main reason this screen
  // needs to refresh at all.
  h = mix(h, (uint32_t)(recorder_->freeBytes() / (1024ULL * 1024ULL)));
  return h;
}

void VisionCamUi::setOrientation(CamOrientation orientation, bool flipped) {
  if (showTouchMark_) {
    orientation = CamOrientation::Portrait;
    flipped = false;
  }
  orientation_ = orientation;
  portraitFlipped_ = flipped;
  const bool portrait = orientation == CamOrientation::Portrait;
  computeLayout(portrait);

  // Rotate the DRAWING coordinate system only. The framebuffer stays 1024x600
  // and the PPA keeps writing it directly in physical coordinates, which is
  // exactly what the viewfinder wants - camera and panel turn together, so the
  // image is already correct and rotating it would put it back on its side.
  Arduino_GFX *g = CrowDisplay::canvas();
  if (g != nullptr) {
    // Rotation 1 and 3 are the two portrait orientations, 180 degrees apart.
    // Which is correct depends on which way the panel was physically turned.
    g->setRotation(portrait ? (flipped ? 3 : 1) : 0);
    g->fillScreen(kBg);  // old layout's pixels are meaningless now
  }
  chromeDirty_ = true;
}

void VisionCamUi::begin() {
  ready_ = CrowDisplay::canvas() != nullptr;
  // Temporary hardware-measurement build: boot directly into rotation 1 and
  // keep every diagnostic on the panel because USB serial disappears at
  // runtime on this board. Normal hit testing is suppressed by handleTouch_.
  showTouchMark_ = true;
  setOrientation(CamOrientation::Portrait, false);
  renderer_.begin();
  // Sized for the largest image this camera writes - a full-resolution still.
  viewer_.begin(Sc2336Sensor::kWidth, Sc2336Sensor::kHeight);
  chromeDirty_ = true;
  Logger::info("ui", renderer_.hardwareAccelerated()
                         ? "console ready (PPA blit)"
                         : "console ready (CPU blit - expect low fps)");
}

void VisionCamUi::markDirty() { chromeDirty_ = true; }

void VisionCamUi::showScreen(CamScreen s) {
  if (s == screen_) return;
  screen_ = s;
  chromeDirty_ = true;
}

CamEvent VisionCamUi::handleTouch_() {
  CamEvent event;
  if (!touch_.releasedEdge()) return event;
  int16_t x = touch_.releaseX();
  int16_t y = touch_.releaseY();
  mapTouch_(x, y);

  if (showTouchMark_) {
    if (touchProbeCount_ < 5) {
      TouchProbeSample &sample = touchProbeSamples_[touchProbeCount_++];
      sample.rawX = touch_.rawX();
      sample.rawY = touch_.rawY();
      sample.mappedX = touch_.releaseX();
      sample.mappedY = touch_.releaseY();
      sample.portraitX = x;
      sample.portraitY = y;
    }
    markX_ = x;
    markY_ = y;
    markMs_ = millis();
    chromeDirty_ = true;
    return event;
  }

  // A displayed photo is a modal state: it covers everything, so it must
  // swallow the tap that dismisses it before any other hit-test runs.
  if (viewer_.showing()) {
    viewer_.close();
    chromeDirty_ = true;
    return event;
  }

  // The tab strip owns navigation on every screen EXCEPT Live, which is
  // full-bleed video and carries its own nav inside the control bar.
  if (screen_ != CAM_SCR_LIVE) {
    const int8_t tab = tabHit(x, y, CAM_SCR_COUNT);
    if (tab >= 0) {
      showScreen((CamScreen)tab);
      return event;
    }
  }

  switch (screen_) {
    case CAM_SCR_LIVE:
      if (barVisible_) {
        // Hit rects use the same layout fields as drawLiveHud_, so the two can
        // only disagree if a field is wrong for both - which is the point of
        // routing every position through Layout rather than duplicating it.
        if (hitRect(x, y, gL.shutterX, gL.btnY, gL.shutterW, gL.btnH)) {
          event.type = CamEventType::Shutter;
          return event;
        }
        if (hitRect(x, y, gL.recX, gL.actionY, gL.actionW, gL.btnH)) {
          event.type = CamEventType::RecordToggle;
          return event;
        }
        if (hitRect(x, y, gL.camX, gL.actionY, gL.actionW, gL.btnH)) {
          event.type = CamEventType::CameraToggle;
          return event;
        }
        for (uint8_t i = 0; i < kNavCount; i++) {
          if (hitRect(x, y, gL.navX + i * gL.navW, gL.navY, gL.navW, gL.btnH)) {
            showScreen(kNavTargets[i]);
            return event;
          }
        }
        // A tap that lands on the bar but misses every control is swallowed
        // here rather than falling through to the hide-the-bar gesture below,
        // which would make near-misses feel like the UI is fighting you.
        if (hitRect(x, y, 0, gL.barY, gL.w, gL.barH)) return event;
      }
      // Anywhere on the image toggles the bar, so the viewfinder can be seen
      // unobstructed. This is the whole reason the bar is an overlay.
      barVisible_ = !barVisible_;
      chromeDirty_ = true;
      break;

    case CAM_SCR_GALLERY: {
      if (recorder_ == nullptr) break;
      const uint8_t count = recorder_->mediaCount();
      const uint8_t pages = (uint8_t)((count + gL.galleryRows - 1) / gL.galleryRows);

      // Paging first, so a tap on PREV/NEXT is never mistaken for a row.
      if (pages > 1) {
        if (hitRect(x, y, gL.rowX, gL.contentBot - 60, 120, 40)) {
          galleryPage_ = galleryPage_ > 0 ? galleryPage_ - 1 : pages - 1;
          chromeDirty_ = true;
          break;
        }
        if (hitRect(x, y, gL.rowX + gL.rowW - 120, gL.contentBot - 60, 120, 40)) {
          galleryPage_ = (uint8_t)((galleryPage_ + 1) % pages);
          chromeDirty_ = true;
          break;
        }
      }

      // Tapping a still opens it full screen. Clips are skipped rather than
      // silently doing nothing that looks like a missed tap - there is no
      // video player here, and pretending otherwise would be worse.
      for (uint8_t i = 0; i < gL.galleryRows; i++) {
        const uint8_t index = galleryPage_ * gL.galleryRows + i;
        if (index >= count) break;
        const int16_t rowY = gL.contentTop + 16 + i * (gL.galleryRowH + 8);
        if (!hitRect(x, y, gL.rowX, rowY, gL.rowW, gL.galleryRowH)) continue;

        const CamRecorder::MediaEntry &entry = recorder_->mediaAt(index);
        if (entry.isVideo) break;
        char path[48];
        snprintf(path, sizeof(path), "/DCIM/%s", entry.name);
        if (!viewer_.show(path)) {
          Logger::warn("ui", String("preview failed: ") + viewer_.lastError());
          chromeDirty_ = true;
        }
        break;
      }
      break;
    }

    case CAM_SCR_STREAM:
      if (hitRect(x, y, gL.rowX, settingsRowY(0), gL.rowW, gL.rowH)) {
        event.type = CamEventType::StreamToggle;
      }
      break;

    case CAM_SCR_SETTINGS:
      // Row 0: exposure, with -/+ at the right edge.
      if (hitRect(x, y, settingsRowX(0) + settingsRowW() - gL.stepW, settingsRowY(0), gL.stepW, gL.rowH)) {
        event.type = CamEventType::ExposureUp;
      } else if (hitRect(x, y, settingsRowX(0) + settingsRowW() - gL.stepW * 2 - 8, settingsRowY(0), gL.stepW, gL.rowH)) {
        event.type = CamEventType::ExposureDown;
      } else if (hitRect(x, y, settingsRowX(1), settingsRowY(1), settingsRowW(), gL.rowH)) {
        event.type = CamEventType::AutoExposureToggle;
      } else if (hitRect(x, y, settingsRowX(2), settingsRowY(2), settingsRowW(), gL.rowH)) {
        event.type = CamEventType::FlipToggle;
      } else if (hitRect(x, y, settingsRowX(5), settingsRowY(5), settingsRowW(), gL.rowH)) {
        event.type = CamEventType::OrientationToggle;
      } else if (hitRect(x, y, settingsRowX(6) + settingsRowW() - gL.stepW,
                         settingsRowY(6), gL.stepW, gL.rowH)) {
        event.type = CamEventType::VolumeUp;
      } else if (hitRect(x, y, settingsRowX(6) + settingsRowW() - gL.stepW * 2,
                         settingsRowY(6), gL.stepW, gL.rowH)) {
        event.type = CamEventType::VolumeDown;
      }
      break;

    default:
      break;
  }
  return event;
}

void VisionCamUi::drawChrome_() {
  Arduino_GFX *g = CrowDisplay::canvas();
  if (g == nullptr) return;

  // Named pillText, not pill: a local called `pill` would shadow the
  // Widgets::pill helper the portrait path below needs to call.
  char pillText[24];
  if (CrowCamera::streaming()) {
    snprintf(pillText, sizeof(pillText), "%.0f FPS", (double)fps_);
  } else {
    snprintf(pillText, sizeof(pillText), "IDLE");
  }
  const uint16_t pillColor = recording_ ? kRed
                             : CrowCamera::streaming() ? kGreen
                                                       : kTextMut;

  if (!gL.portrait) {
    headerBar(g, "CYPHER VISION CAM", camScreenName(screen_), pillText, pillColor);
    tabBar(g, kTabs, CAM_SCR_COUNT, (uint8_t)screen_, kAccent);
    return;
  }

  // Portrait chrome, drawn locally. The shared helpers hardcode a 1024-wide
  // canvas (kChromeW in DashboardWidgets.h), so calling them here would draw
  // most of the header and every tab off the right-hand edge.
  panel(g, 0, 0, gL.w, gL.headerH, 0, kSurface);
  g->drawFastHLine(0, gL.headerH - 1, gL.w, kLine);
  text(g, 16, 14, "CYPHER VISION CAM", fontM(), kTextHi, kLeft);
  text(g, 16, 42, camScreenName(screen_), fontS(), kTextMut, kLeft);
  pill(g, gL.w - 108, 18, pillText, fontS(), kBg, pillColor);

  // Tab strip across the bottom. Four tabs in 600 px is 150 each - tight but
  // legible at fontS, and the labels are short.
  panel(g, 0, gL.contentBot, gL.w, gL.tabH, 0, kSurface);
  g->drawFastHLine(0, gL.contentBot, gL.w, kLine);
  const int16_t tabW = gL.w / CAM_SCR_COUNT;
  for (uint8_t i = 0; i < CAM_SCR_COUNT; i++) {
    const bool on = (i == (uint8_t)screen_);
    if (on) {
      panel(g, i * tabW + 6, gL.contentBot + 8, tabW - 12, gL.tabH - 16, 8, kSurfaceHi);
      g->drawFastHLine(i * tabW + 6, gL.contentBot + 2, tabW - 12, kAccent);
    }
    text(g, i * tabW + tabW / 2, gL.contentBot + 24, kTabs[i], fontS(),
         on ? kAccent : kTextMut, kCenter);
  }
}

// Maps a touch point from the panel's native landscape frame into the current
// logical canvas.
//
// CrowTouch reports in panel coordinates (0..1023, 0..599) and knows nothing
// about rotation, so in portrait the axes must be exchanged here. Rotating the
// drawing with setRotation but leaving touch alone would give a UI where every
// control is visibly in one place and tappable in another - which is worse than
// no rotation at all.
void VisionCamUi::mapTouch_(int16_t &x, int16_t &y) const {
  if (orientation_ != CamOrientation::Portrait) return;
  const int16_t rawX = x;
  const int16_t rawY = y;

  // Inverse of Arduino_DSI_Display's rotation, transcribed from its
  // writePixelPreclipped rather than derived - the two must agree exactly or
  // controls are visibly in one place and tappable in another.
  //
  //   rotation 1:  phys_y = logical_x,        phys_x = 1023 - logical_y
  //   rotation 3:  phys_y = 599 - logical_x,  phys_x = logical_y
  //
  // Which one is correct depends on WHICH WAY the panel was physically turned,
  // and that is not something the software can know. Both are offered; the
  // Settings row cycles LANDSCAPE -> PORTRAIT -> PORTRAIT FLIPPED so the right
  // one can be chosen by trying it.
  if (portraitFlipped_) {
    x = (int16_t)(kPanelH - 1 - rawY);
    y = rawX;
  } else {
    x = rawY;
    y = (int16_t)(kPanelW - 1 - rawX);
  }
}

void VisionCamUi::drawTouchProbe_() {
  Arduino_GFX *g = CrowDisplay::canvas();
  if (g == nullptr) return;

  static const int16_t kTargets[5][2] = {
      {60, 120}, {540, 120}, {540, 860}, {60, 860}, {300, 490}};
  static const char *const kNames[5] = {"TL", "TR", "BR", "BL", "CTR"};

  g->fillScreen(kBg);
  text(g, 20, 20, "PORTRAIT TOUCH MEASURE", fontL(), kAccent, kLeft);
  text(g, 20, 58, "Tap the red target, then release", fontM(), kTextHi, kLeft);
  text(g, 20, 88, "RAW = GT911   MAP = CrowTouch   P = current", fontS(), kTextMut, kLeft);

  if (touchProbeCount_ < 5) {
    const int16_t tx = kTargets[touchProbeCount_][0];
    const int16_t ty = kTargets[touchProbeCount_][1];
    g->drawFastHLine(tx - 28, ty, 56, kRed);
    g->drawFastVLine(tx, ty - 28, 56, kRed);
    g->drawCircle(tx, ty, 18, kRed);
    char prompt[32];
    snprintf(prompt, sizeof(prompt), "%u/5  %s  target=%d,%d",
             (unsigned)(touchProbeCount_ + 1), kNames[touchProbeCount_], tx, ty);
    text(g, 20, 910, prompt, fontM(), kRed, kLeft);
  } else {
    text(g, 20, 910, "DONE - photograph the five rows below", fontM(), kGreen, kLeft);
  }

  panel(g, 14, 950, 572, 60, 8, kSurface, 1, kLine);
  text(g, 24, 970, "label  raw-x raw-y | map-x map-y | P-x P-y", fontS(), kTextHi, kLeft);

  for (uint8_t i = 0; i < touchProbeCount_; i++) {
    const TouchProbeSample &s = touchProbeSamples_[i];
    char row[80];
    snprintf(row, sizeof(row), "%s  %4d %4d | %4d %4d | %4d %4d",
             kNames[i], s.rawX, s.rawY, s.mappedX, s.mappedY,
             s.portraitX, s.portraitY);
    const int16_t rowY = 690 + i * 40;
    panel(g, 14, rowY, 572, 34, 6, kSurface, 1, kLine);
    text(g, 24, rowY + 8, row, fontS(), kTextHi, kLeft);
  }
}

void VisionCamUi::drawLiveHud_(const CrowCamera::Frame *frame) {
  Arduino_GFX *g = CrowDisplay::canvas();
  if (g == nullptr) return;

  // Solid backing rather than a translucent wash: the framebuffer is RGB565
  // with no alpha channel, so real transparency would mean reading back and
  // blending 76k pixels every frame. A flat bar costs one fill.
  panel(g, 0, gL.barY, gL.w, gL.barH, 0, kBg);
  // Hairline along the top edge to separate bar from image without a border.
  g->drawFastHLine(0, gL.barY, gL.w, kLine);

  // Portrait puts SHUTTER on its own full-width row above REC and PAUSE: 600 px
  // will not hold three buttons wide enough to hit confidently, and the shutter
  // is the one that must never be missed.
  touchButton(g, gL.shutterX, gL.btnY, gL.shutterW, gL.btnH, "SHUTTER", false, kAccent);
  touchButton(g, gL.recX, gL.actionY, gL.actionW, gL.btnH, recording_ ? "STOP" : "REC",
              recording_, recording_ ? kRed : kAccent);
  touchButton(g, gL.camX, gL.actionY, gL.actionW, gL.btnH,
              CrowCamera::streaming() ? "PAUSE" : "START", CrowCamera::streaming(),
              kAccent);

  for (uint8_t i = 0; i < kNavCount; i++) {
    touchButton(g, gL.navX + i * gL.navW, gL.navY, gL.navW - 8, gL.btnH, kNavLabels[i],
                false, kSurfaceHi);
  }

  // Status cluster. Two compact lines rather than the old four-line telemetry
  // dump - the numbers that matter at a glance are the frame rate and whether
  // anything is being dropped. The rest moved to Settings, which is where you
  // go when something is actually wrong.
  // Portrait has no room for a status cluster in the bar - the header pill
  // already carries frame rate there, so a second copy would cost a row for
  // nothing.
  if (gL.statusX >= 0) {
    char line[64];
    if (recording_) {
      snprintf(line, sizeof(line), "REC %lu:%02lu", (unsigned long)(recElapsedSec_ / 60),
               (unsigned long)(recElapsedSec_ % 60));
      statusDot(g, gL.statusX + 8, gL.statusY + 16, 6, kRed);
      text(g, gL.statusX + 22, gL.statusY + 6, line, fontL(), kRed, kLeft);
    } else {
      snprintf(line, sizeof(line), "%.0f fps", (double)fps_);
      text(g, gL.statusX, gL.statusY + 6, line, fontL(),
           CrowCamera::streaming() ? kTextHi : kTextMut, kLeft);
    }

    const uint32_t drops = CrowCamera::dropCount();
    snprintf(line, sizeof(line), "%s  %u drop%s",
             renderer_.hardwareAccelerated() ? "PPA" : "CPU", (unsigned)drops,
             drops == 1 ? "" : "s");
    text(g, gL.statusX, gL.statusY + 34, line, fontS(),
         drops > 0 ? kAmber : (renderer_.hardwareAccelerated() ? kTextMut : kRed), kLeft);
  }

  (void)frame;
}

// Minimal always-on indicator for when the bar is hidden. A clean viewfinder
// should not also be a blind one - recording state especially must never be
// invisible, or you end up with a camera that is quietly still rolling.
void VisionCamUi::drawLiveMinimal_() {
  Arduino_GFX *g = CrowDisplay::canvas();
  if (g == nullptr) return;

  char line[32];
  if (recording_) {
    statusDot(g, gL.w - 132, 24, 7, kRed);
    snprintf(line, sizeof(line), "REC %lu:%02lu", (unsigned long)(recElapsedSec_ / 60),
             (unsigned long)(recElapsedSec_ % 60));
    text(g, gL.w - 20, 14, line, fontL(), kRed, kRight);
  } else {
    snprintf(line, sizeof(line), "%.0f fps", (double)fps_);
    text(g, gL.w - 20, 14, line, fontS(), kTextMut, kRight);
  }
}

// Shown while a file transfer owns the loop. Without this a download of a large
// clip looks exactly like a crash: video stops, touch stops, nothing explains
// why. Saying "busy" turns a frightening freeze into an understood one.
void VisionCamUi::drawServingOverlay_() {
  Arduino_GFX *g = CrowDisplay::canvas();
  if (g == nullptr) return;

  constexpr int16_t kW2 = 560, kH2 = 132;
  const int16_t x = (gL.w - kW2) / 2;
  const int16_t y = (kChromeH - kH2) / 2;

  panel(g, x, y, kW2, kH2, 16, kSurface, 2, kAccent);
  text(g, gL.w / 2, y + 26, "SERVING FILE", fontL(), kAccent, kCenter);
  text(g, gL.w / 2, y + 58, servingName_.c_str(), fontM(), kTextHi, kCenter);
  text(g, gL.w / 2, y + 90, "viewfinder resumes when the transfer ends", fontS(), kTextMut,
       kCenter);
}

void VisionCamUi::drawLive_(const CrowCamera::Frame *frame) {
  Arduino_GFX *g = CrowDisplay::canvas();
  if (g == nullptr) return;

  if (frame != nullptr) {
    // Blit WITHOUT flushing, then draw chrome, then flush each region exactly
    // once. Order matters and is the whole fix for a visible flicker: letting
    // drawFrame flush the full panel first pushed video into the bar's
    // rectangle, and the bar was pushed over it a moment later - every frame,
    // 21 times a second, which reads as the status line strobing. Now the bar's
    // rows are never pushed carrying anything but the bar.
    renderer_.drawFrame(*frame, 0, 0, gL.w, gL.h, /*autoFlush=*/false);

    if (serving_) {
      drawServingOverlay_();
      CrowDisplay::flush();
    } else if (barVisible_) {
      drawLiveHud_(frame);
      CrowDisplay::flush(0, 0, gL.w, gL.barY);        // image above the bar
      CrowDisplay::flush(0, gL.barY, gL.w, gL.barH);    // the bar itself
    } else {
      drawLiveMinimal_();
      CrowDisplay::flush();
    }
    return;
  }

  // No frame. Say why, rather than leaving a stale image that looks live.
  if (!chromeDirty_) return;
  panel(g, 0, 0, gL.w, gL.h, 0, kBg);
  const char *reason = CrowCamera::ready()
                           ? (CrowCamera::streaming() ? "waiting for the first frame"
                                                      : "camera paused - tap START")
                           : CrowCamera::lastError();
  text(g, gL.w / 2, 0 + gL.h / 2 - 60, "NO SIGNAL", fontXL(), kTextMut, kCenter);
  text(g, gL.w / 2, 0 + gL.h / 2 - 14, reason, fontM(), kTextMut, kCenter);
  // Always draw the bar here regardless of barVisible_: with no image there is
  // nothing to tap to bring it back, so hiding it would strand the user on a
  // dead screen with no way to reach START or the other tabs.
  drawLiveHud_(nullptr);
}

void VisionCamUi::drawGallery_() {
  Arduino_GFX *g = CrowDisplay::canvas();
  if (g == nullptr) return;
  panel(g, 0, gL.contentTop, gL.w, gL.contentBot - gL.contentTop, 0, kBg);

  if (recorder_ == nullptr || !recorder_->storageReady()) {
    text(g, gL.w / 2, gL.contentTop + 160, "NO CARD", fontXL(), kTextMut, kCenter);
    text(g, gL.w / 2, gL.contentTop + 210,
         recorder_ != nullptr ? recorder_->lastError() : "recorder unavailable", fontM(),
         kTextMut, kCenter);
    return;
  }

  const uint8_t count = recorder_->mediaCount();
  if (count == 0) {
    text(g, gL.w / 2, gL.contentTop + 160, "NOTHING YET", fontXL(), kTextMut, kCenter);
    text(g, gL.w / 2, gL.contentTop + 210, "Tap SHUTTER on the Live screen", fontM(), kTextMut,
         kCenter);
    return;
  }

  // Text rows, not thumbnails. Decoding every JPEG on the card to build a
  // thumbnail grid would stall the render loop for seconds; the file name, size
  // and type are what you actually need to find a shot.
  const uint8_t firstRow = galleryPage_ * gL.galleryRows;
  for (uint8_t i = 0; i < gL.galleryRows; i++) {
    const uint8_t index = firstRow + i;
    if (index >= count) break;
    const CamRecorder::MediaEntry &entry = recorder_->mediaAt(index);
    const int16_t y = gL.contentTop + 16 + i * (gL.galleryRowH + 8);

    // Stills get a highlighted fill to signal they are tappable; clips do not,
    // because there is no video player here and an affordance that does nothing
    // is worse than none.
    panel(g, gL.rowX, y, gL.rowW, gL.galleryRowH, 10, entry.isVideo ? kSurface : kSurfaceHi, 1,
          kLine);
    text(g, gL.rowX + 20, y + 14, entry.name, fontL(), kTextHi, kLeft);
    pill(g, gL.rowX + gL.rowW - 220, y + 12, entry.isVideo ? "VIDEO" : "STILL", fontS(),
         kBg, entry.isVideo ? kAmber : kAccent);

    char size[24];
    if (entry.bytes >= 1024UL * 1024UL) {
      snprintf(size, sizeof(size), "%.1f MB", (double)entry.bytes / (1024.0 * 1024.0));
    } else {
      snprintf(size, sizeof(size), "%lu KB", (unsigned long)(entry.bytes / 1024UL));
    }
    text(g, gL.rowX + gL.rowW - 20, y + 14, size, fontM(), kTextMut, kRight);
  }

  const uint8_t pages = (uint8_t)((count + gL.galleryRows - 1) / gL.galleryRows);
  char footer[96];
  snprintf(footer, sizeof(footer),
           "%u file%s   page %u/%u   %lu MB free%s   -   tap a photo to view", count,
           count == 1 ? "" : "s", galleryPage_ + 1, pages,
           (unsigned long)(recorder_->freeBytes() / (1024ULL * 1024ULL)),
           recorder_->mediaTruncated() ? "   (list truncated)" : "");
  text(g, gL.w / 2, gL.contentBot - 30, footer, fontS(),
       recorder_->mediaTruncated() ? kAmber : kTextMut, kCenter);

  if (pages > 1) {
    touchButton(g, gL.rowX, gL.contentBot - 60, 120, 40, "PREV", false, kAccent);
    touchButton(g, gL.rowX + gL.rowW - 120, gL.contentBot - 60, 120, 40, "NEXT", false, kAccent);
  }
}

void VisionCamUi::drawStream_() {
  Arduino_GFX *g = CrowDisplay::canvas();
  if (g == nullptr) return;
  panel(g, 0, gL.contentTop, gL.w, gL.contentBot - gL.contentTop, 0, kBg);

  panel(g, gL.rowX, settingsRowY(0), gL.rowW, gL.rowH, 12, kSurface, 1, kLine);
  text(g, gL.rowX + 20, settingsRowY(0) + 20, "Access point", fontL(), kTextHi, kLeft);
  text(g, gL.rowX + gL.rowW - 20, settingsRowY(0) + 20, streamUp_ ? "ON" : "OFF", fontL(),
       streamUp_ ? kGreen : kTextMut, kRight);

  panel(g, gL.rowX, settingsRowY(1), gL.rowW, gL.rowH, 12, kSurface, 1, kLine);
  text(g, gL.rowX + 20, settingsRowY(1) + 20, "Network", fontL(), kTextHi, kLeft);
  text(g, gL.rowX + gL.rowW - 20, settingsRowY(1) + 20,
       streamSsid_.length() ? streamSsid_.c_str() : "-", fontM(), kTextMut, kRight);

  panel(g, gL.rowX, settingsRowY(2), gL.rowW, gL.rowH, 12, kSurface, 1, kLine);
  text(g, gL.rowX + 20, settingsRowY(2) + 20, "Watch at (AP)", fontL(), kTextHi, kLeft);
  text(g, gL.rowX + gL.rowW - 20, settingsRowY(2) + 20,
       streamUrl_.length() ? streamUrl_.c_str() : "-", fontM(), kAccent, kRight);

  // Station-mode address, shown separately because it is the path that is
  // actually proven on this board. When present it is the one to use.
  panel(g, gL.rowX, settingsRowY(3), gL.rowW, gL.rowH, 12, kSurface, 1, kLine);
  text(g, gL.rowX + 20, settingsRowY(3) + 20, "Watch at (LAN)", fontL(), kTextHi, kLeft);
  text(g, gL.rowX + gL.rowW - 20, settingsRowY(3) + 20,
       stationUrl_.length() ? stationUrl_.c_str() : "no LAN credentials", fontM(),
       stationUrl_.length() ? kGreen : kTextMut, kRight);

  // Joined devices and active viewers are DIFFERENT numbers, and showing both
  // is the point: a phone that associates but cannot load the page gives
  // joined=1 viewers=0, which separates a radio problem from an HTTP one.
  // Collapsing them into a single "clients" figure would hide exactly the
  // distinction worth having.
  panel(g, gL.rowX, settingsRowY(4), gL.rowW, gL.rowH, 12, kSurface, 1, kLine);
  text(g, gL.rowX + 20, settingsRowY(4) + 20, "Joined / watching", fontL(), kTextHi, kLeft);
  char buf[24];
  snprintf(buf, sizeof(buf), "%u  /  %u", streamStations_, streamClients_);
  text(g, gL.rowX + gL.rowW - 20, settingsRowY(4) + 20, buf, fontL(),
       streamClients_ ? kGreen : (streamStations_ ? kAmber : kTextMut), kRight);

  // The privacy statement belongs on the screen that turns the radio on, not
  // only in the README - this is the control that starts broadcasting video.
  text(g, gL.w / 2, gL.contentBot - 34,
       "Anyone on this network can watch the live feed.", fontS(), kAmber, kCenter);
}

void VisionCamUi::drawSettings_() {
  Arduino_GFX *g = CrowDisplay::canvas();
  if (g == nullptr) return;
  panel(g, 0, gL.contentTop, gL.w, gL.contentBot - gL.contentTop, 0, kBg);

  Sc2336Sensor *s = CrowCamera::sensor();
  char buf[48];

  panel(g, settingsRowX(0), settingsRowY(0), settingsRowW(), gL.rowH, 12, kSurface, 1, kLine);
  text(g, settingsRowX(0) + 20, settingsRowY(0) + 20, "Exposure", fontL(), kTextHi, kLeft);
  if (s != nullptr) {
    snprintf(buf, sizeof(buf), "%lu / %lu", (unsigned long)s->exposure(),
             (unsigned long)Sc2336Sensor::maxExposure());
  } else {
    snprintf(buf, sizeof(buf), "no sensor");
  }
  text(g, settingsRowX(0) + settingsRowW() - gL.stepW * 2 - 28, settingsRowY(0) + 20, buf, fontM(), kTextMut, kRight);
  touchButton(g, settingsRowX(0) + settingsRowW() - gL.stepW * 2 - 8, settingsRowY(0) + 6, gL.stepW, gL.rowH - 12,
              "-", false, kAccent);
  touchButton(g, settingsRowX(0) + settingsRowW() - gL.stepW, settingsRowY(0) + 6, gL.stepW, gL.rowH - 12,
              "+", false, kAccent);

  panel(g, settingsRowX(1), settingsRowY(1), settingsRowW(), gL.rowH, 12, kSurface, 1, kLine);
  text(g, settingsRowX(1) + 20, settingsRowY(1) + 20, "Auto exposure", fontL(), kTextHi, kLeft);
  // "settling" vs "auto" is not decoration: it tells the user the exposure they
  // are looking at is still moving, so a photo taken now may not match it.
  const char *aeState = !autoExposure_ ? "MANUAL"
                        : autoExposureConverged_ ? "AUTO"
                                                 : "AUTO - settling";
  text(g, settingsRowX(1) + settingsRowW() - 20, settingsRowY(1) + 20, aeState, fontL(),
       !autoExposure_ ? kTextMut : (autoExposureConverged_ ? kGreen : kAmber), kRight);

  panel(g, settingsRowX(2), settingsRowY(2), settingsRowW(), gL.rowH, 12, kSurface, 1, kLine);
  text(g, settingsRowX(2) + 20, settingsRowY(2) + 20, "Flip image", fontL(), kTextHi, kLeft);
  if (s != nullptr) {
    snprintf(buf, sizeof(buf), "%s%s",
             s->flippedVertically() ? "vertical " : "",
             s->flippedHorizontally() ? "mirrored" : "");
    text(g, settingsRowX(2) + settingsRowW() - 20, settingsRowY(2) + 20,
         (s->flippedVertically() || s->flippedHorizontally()) ? buf : "normal", fontM(),
         kTextMut, kRight);
  }

  // Image-pipeline telemetry. This row exists because the serial port is dead
  // once the app runs, so without it there is no way to tell "the AWB loop is
  // correcting badly" from "the AWB loop never ran" - and those need opposite
  // fixes. white=0 means no white patches were found and the gains are frozen
  // at whatever they were.
  panel(g, settingsRowX(3), settingsRowY(3), settingsRowW(), gL.rowH, 12, kSurface, 1, kLine);
  text(g, settingsRowX(3) + 20, settingsRowY(3) + 20, "White balance", fontL(), kTextHi, kLeft);
  {
    float r = 1.0f, gg = 1.0f, b = 1.0f;
    CrowCamera::colorGains(r, gg, b);
    snprintf(buf, sizeof(buf), "white %lu   R%.2f B%.2f", (unsigned long)whitePatches_,
             (double)r, (double)b);
    text(g, settingsRowX(3) + settingsRowW() - 20, settingsRowY(3) + 20, buf, fontM(),
         whitePatches_ == 0 ? kRed : kTextMut, kRight);
  }

  // Shutter button. Shows the live level of BOTH boot strapping pins so the
  // right one can be identified by pressing the button and seeing which moves.
  // Once confirmed, the alt reading is noise and this row can shrink.
  panel(g, settingsRowX(4), settingsRowY(4), settingsRowW(), gL.rowH, 12, kSurface, 1, kLine);
  text(g, settingsRowX(4) + 20, settingsRowY(4) + 20, "BOOT shutter", fontL(), kTextHi, kLeft);
  // Per-pin press counts, not just levels: whichever counter moves is the pin
  // the tactile switch actually reaches, which is the fact worth recording.
  snprintf(buf, sizeof(buf), "io%d=%s/%lu  io%d=%s/%lu", VISIONCAM_SHUTTER_PIN,
           shutterLevel_ ? "hi" : "LO", (unsigned long)shutterPresses_,
           VISIONCAM_SHUTTER_ALT_PIN, shutterAltLevel_ ? "hi" : "LO",
           (unsigned long)shutterAltPresses_);
  text(g, settingsRowX(4) + settingsRowW() - 20, settingsRowY(4) + 20, buf, fontM(),
       shutterPresses_ > 0 ? kGreen : kTextMut, kRight);

  panel(g, settingsRowX(5), settingsRowY(5), settingsRowW(), gL.rowH, 12, kSurface, 1, kLine);
  text(g, settingsRowX(5) + 20, settingsRowY(5) + 20, "Orientation", fontL(), kTextHi, kLeft);
  // Three states, not two: which of the two portrait rotations is correct
  // depends on which way the panel was physically turned, and nothing in
  // software can tell. Cycling through both lets it be chosen by trying.
  const char *orientLabel = !gL.portrait          ? "LANDSCAPE"
                            : portraitFlipped_    ? "PORTRAIT  flipped"
                                                  : "PORTRAIT";
  text(g, settingsRowX(5) + settingsRowW() - 20, settingsRowY(5) + 20, orientLabel,
       fontL(), gL.portrait ? kAccent : kTextMut, kRight);

  // Volume as a stepped control rather than an on/off: the onboard speaker is
  // small, and "too quiet to hear" is the failure mode worth being able to fix
  // without a rebuild. OFF is the bottom of the same scale, so one row does both.
  panel(g, settingsRowX(6), settingsRowY(6), settingsRowW(), gL.rowH, 12, kSurface, 1, kLine);
  text(g, settingsRowX(6) + 20, settingsRowY(6) + 20, "Shutter sound", fontL(), kTextHi,
       kLeft);
  {
    const int16_t stepX = settingsRowX(6) + settingsRowW() - gL.stepW;
    touchButton(g, stepX, settingsRowY(6) + 6, gL.stepW - 8, gL.rowH - 12, "+", false,
                kSurfaceHi);
    touchButton(g, stepX - gL.stepW, settingsRowY(6) + 6, gL.stepW - 8, gL.rowH - 12, "-",
                false, kSurfaceHi);
    char vol[16];
    if (!soundEnabled_ || soundVolume_ == 0) {
      snprintf(vol, sizeof(vol), "OFF");
    } else {
      snprintf(vol, sizeof(vol), "%u%%", soundVolume_);
    }
    text(g, stepX - gL.stepW - 16, settingsRowY(6) + 20, vol, fontL(),
         (soundEnabled_ && soundVolume_ > 0) ? kGreen : kTextMut, kRight);
  }

  panel(g, settingsRowX(7), settingsRowY(7), settingsRowW(), gL.rowH, 12, kSurface, 1, kLine);
  text(g, settingsRowX(7) + 20, settingsRowY(7) + 20, "Battery", fontL(), kTextHi, kLeft);
  // Deliberately not a percentage: this board documents no battery ADC, so any
  // number here would be invented. Saying "unmonitored" is the honest answer.
  text(g, settingsRowX(5) + settingsRowW() - 20, settingsRowY(5) + 20, "unmonitored", fontM(), kTextMut, kRight);
}

CamEvent VisionCamUi::tick(const CrowCamera::Frame *frame) {
  CamEvent event;
  touch_.tick();

  // Loop-rate counter. tick() runs exactly once per loop(), so counting calls
  // here measures the render loop directly.
  {
    const uint32_t nowMs = millis();
    loopCount_++;
    if (loopWindowMs_ == 0) loopWindowMs_ = nowMs;
    const uint32_t span = nowMs - loopWindowMs_;
    if (span >= 1000) {
      loopHz_ = loopCount_ * 1000UL / span;
      loopCount_ = 0;
      loopWindowMs_ = nowMs;
    }
  }

  if (!ready_) {
    ready_ = CrowDisplay::canvas() != nullptr;
    if (!ready_) return event;
    chromeDirty_ = true;
  }

  event = handleTouch_();

  if (showTouchMark_) {
    drawTouchProbe_();
    CrowDisplay::flush();
    chromeDirty_ = false;
    return event;
  }

  // A displayed photo owns the whole screen. Drawing anything underneath would
  // paint over it, so every render path is skipped until it is dismissed.
  if (viewer_.showing()) return event;

  // Chrome repaints only when something changed, or on a slow heartbeat so the
  // counters stay current without competing with the frame path.
  //
  // Live is excluded: it is full-bleed video with no headerBar and no tabBar,
  // and drawing either would paint over the image. Its bar is drawn inside
  // drawLive_ after the frame blit, which is the only correct order.
  const uint32_t now = millis();
  const bool chromeTick = (now - lastChromeMs_) >= 500;
  if ((chromeDirty_ || chromeTick) && screen_ != CAM_SCR_LIVE) {
    drawChrome_();
    drewThisFrame_ = true;
    lastChromeMs_ = now;
  } else if (chromeTick) {
    lastChromeMs_ = now;
  }

  // Static screens repaint ONLY when something they display has changed.
  //
  // They used to repaint on the 500 ms heartbeat regardless, which flickered
  // visibly: the panel is single-framebuffer and the DSI scans it continuously,
  // so a clear-then-redraw is on screen in its half-finished state for as long
  // as the redraw takes. Twice a second, forever, on a screen whose contents
  // were usually identical.
  //
  // The heartbeat still drives the CHECK - so a change appears within 500 ms -
  // but the draw itself is now conditional on the content actually differing.
  const bool poll = chromeDirty_ || chromeTick;
  switch (screen_) {
    case CAM_SCR_LIVE:
      drawLive_(frame);
      break;
    case CAM_SCR_GALLERY:
      if (poll && takeIfChanged_(gallerySignature_())) {
        drawGallery_();
        drewThisFrame_ = true;
      }
      break;
    case CAM_SCR_STREAM:
      if (poll && takeIfChanged_(streamSignature_())) {
        drawStream_();
        drewThisFrame_ = true;
      }
      break;
    case CAM_SCR_SETTINGS:
      if (poll && takeIfChanged_(settingsSignature_())) {
        drawSettings_();
        drewThisFrame_ = true;
      }
      break;
    default:
      break;
  }

  // Live flushes its own regions inside drawLive_ - the frame rect from the
  // renderer, then the bar. Flushing again here would double the cache-sync
  // cost per frame for nothing. Every other screen is a static dashboard, so
  // one full flush when it changes is correct and cheap.
  // Touch crosshair, drawn last so it sits above whatever screen is showing.
  // Fades after 2 s so it does not become permanent clutter.
  if (showTouchMark_ && markX_ >= 0 && (millis() - markMs_) < 2000) {
    Arduino_GFX *g = CrowDisplay::canvas();
    if (g != nullptr) {
      g->drawFastHLine(markX_ - 22, markY_, 44, kRed);
      g->drawFastVLine(markX_, markY_ - 22, 44, kRed);
      g->drawCircle(markX_, markY_, 12, kRed);
      char pos[24];
      snprintf(pos, sizeof(pos), "%d,%d", markX_, markY_);
      text(g, markX_ + 18, markY_ + 16, pos, fontS(), kRed, kLeft);
      drewThisFrame_ = true;
    }
  }

  // Flush only when something was actually drawn. Pushing an unchanged
  // framebuffer twice a second costs a 1.2 MB cache sync for no visible effect.
  if (screen_ != CAM_SCR_LIVE && drewThisFrame_) {
    CrowDisplay::flush();
  }
  drewThisFrame_ = false;
  chromeDirty_ = false;
  return event;
}

#else  // headless build

void VisionCamUi::begin() {}
void VisionCamUi::markDirty() {}
void VisionCamUi::showScreen(CamScreen s) { screen_ = s; }
CamEvent VisionCamUi::tick(const CrowCamera::Frame *) { return CamEvent(); }

// Orientation still has meaning without a screen: the sketch uses it to decide
// how saved files are rotated, so it records the choice rather than ignoring it.
void VisionCamUi::setOrientation(CamOrientation orientation, bool flipped) {
  orientation_ = orientation;
  portraitFlipped_ = flipped;
}

#endif
