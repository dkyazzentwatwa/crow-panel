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
                                 uint8_t clients) {
  if (up != streamUp_ || clients != streamClients_) markDirty();
  streamUp_ = up;
  streamSsid_ = ssid;
  streamUrl_ = url;
  streamClients_ = clients;
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

constexpr int16_t kW = 1024;
constexpr int16_t kContentTop = kChromeHeaderH;  // 72
constexpr int16_t kContentBot = kChromeTabY;     // 536

const char *const kTabs[CAM_SCR_COUNT] = {"LIVE", "GALLERY", "STREAM", "SETTINGS"};

// The Live viewfinder is the full content band, edge to edge. The sensor is
// 1024x600 and the panel is 1024x600, so this rectangle is deliberately as
// close to native as the chrome allows - every pixel of scaling costs PPA
// bandwidth for no gain.
constexpr int16_t kViewX = 0;
constexpr int16_t kViewY = kContentTop;
constexpr int16_t kViewW = kW;
constexpr int16_t kViewH = kContentBot - kContentTop;  // 464

// Live HUD controls, floated over the bottom of the image.
constexpr int16_t kHudH = 76;
constexpr int16_t kHudY = kContentBot - kHudH;
constexpr int16_t kBtnW = 150;
constexpr int16_t kBtnH = 52;
constexpr int16_t kBtnY = kHudY + 12;
constexpr int16_t kShutterX = 24;
constexpr int16_t kRecX = kShutterX + kBtnW + 16;
constexpr int16_t kCamX = kRecX + kBtnW + 16;

// Settings rows.
constexpr int16_t kRowH = 64;
constexpr int16_t kRowX = 40;
constexpr int16_t kRowW = kW - 80;
constexpr int16_t kSetTop = kContentTop + 24;
constexpr int16_t kStepW = 90;

int16_t settingsRowY(uint8_t index) { return kSetTop + index * (kRowH + 12); }

// Gallery list geometry.
constexpr uint8_t kGalleryRows = 6;
constexpr int16_t kGalleryRowH = 56;

}  // namespace

void VisionCamUi::begin() {
  ready_ = CrowDisplay::canvas() != nullptr;
  renderer_.begin();
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
  const int16_t x = touch_.releaseX();
  const int16_t y = touch_.releaseY();

  // The tab strip owns navigation on every screen.
  const int8_t tab = tabHit(x, y, CAM_SCR_COUNT);
  if (tab >= 0) {
    showScreen((CamScreen)tab);
    return event;
  }

  switch (screen_) {
    case CAM_SCR_LIVE:
      if (hudVisible_) {
        if (hitRect(x, y, kShutterX, kBtnY, kBtnW, kBtnH)) {
          event.type = CamEventType::Shutter;
          return event;
        }
        if (hitRect(x, y, kRecX, kBtnY, kBtnW, kBtnH)) {
          event.type = CamEventType::RecordToggle;
          return event;
        }
        if (hitRect(x, y, kCamX, kBtnY, kBtnW, kBtnH)) {
          event.type = CamEventType::CameraToggle;
          return event;
        }
      }
      // Anywhere else on the image toggles the HUD, so the viewfinder can be
      // seen unobstructed.
      if (hitRect(x, y, kViewX, kViewY, kViewW, kViewH)) {
        hudVisible_ = !hudVisible_;
        chromeDirty_ = true;
      }
      break;

    case CAM_SCR_GALLERY: {
      if (recorder_ == nullptr) break;
      const uint8_t count = recorder_->mediaCount();
      const uint8_t pages = (uint8_t)((count + kGalleryRows - 1) / kGalleryRows);
      if (pages <= 1) break;
      if (hitRect(x, y, kRowX, kContentBot - 60, 120, 40)) {
        galleryPage_ = galleryPage_ > 0 ? galleryPage_ - 1 : pages - 1;
        chromeDirty_ = true;
      } else if (hitRect(x, y, kRowX + kRowW - 120, kContentBot - 60, 120, 40)) {
        galleryPage_ = (uint8_t)((galleryPage_ + 1) % pages);
        chromeDirty_ = true;
      }
      break;
    }

    case CAM_SCR_STREAM:
      if (hitRect(x, y, kRowX, settingsRowY(0), kRowW, kRowH)) {
        event.type = CamEventType::StreamToggle;
      }
      break;

    case CAM_SCR_SETTINGS:
      // Row 0: exposure, with -/+ at the right edge.
      if (hitRect(x, y, kRowX + kRowW - kStepW, settingsRowY(0), kStepW, kRowH)) {
        event.type = CamEventType::ExposureUp;
      } else if (hitRect(x, y, kRowX + kRowW - kStepW * 2 - 8, settingsRowY(0), kStepW, kRowH)) {
        event.type = CamEventType::ExposureDown;
      } else if (hitRect(x, y, kRowX, settingsRowY(1), kRowW, kRowH)) {
        event.type = CamEventType::AutoExposureToggle;
      } else if (hitRect(x, y, kRowX, settingsRowY(2), kRowW, kRowH)) {
        event.type = CamEventType::FlipToggle;
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

  char pill[24];
  if (CrowCamera::streaming()) {
    snprintf(pill, sizeof(pill), "%.0f FPS", (double)fps_);
  } else {
    snprintf(pill, sizeof(pill), "IDLE");
  }
  const uint16_t pillColor = recording_ ? kRed
                             : CrowCamera::streaming() ? kGreen
                                                       : kTextMut;

  headerBar(g, "CYPHER VISION CAM", camScreenName(screen_), pill, pillColor);
  tabBar(g, kTabs, CAM_SCR_COUNT, (uint8_t)screen_, kAccent);
}

void VisionCamUi::drawLiveHud_(const CrowCamera::Frame *frame) {
  Arduino_GFX *g = CrowDisplay::canvas();
  if (g == nullptr) return;

  panel(g, 0, kHudY, kW, kHudH, 0, kBg);

  touchButton(g, kShutterX, kBtnY, kBtnW, kBtnH, "SHUTTER", false, kAccent);
  touchButton(g, kRecX, kBtnY, kBtnW, kBtnH, recording_ ? "STOP" : "RECORD",
              recording_, recording_ ? kRed : kAccent);
  touchButton(g, kCamX, kBtnY, kBtnW, kBtnH,
              CrowCamera::streaming() ? "PAUSE" : "START",
              CrowCamera::streaming(), kAccent);

  // Right-hand telemetry. Everything here is measured, not nominal - the blit
  // cost and the drop count are the two numbers that explain a bad frame rate,
  // so they are on screen rather than buried in a serial command.
  char line[72];
  const int16_t infoX = kW - 24;
  snprintf(line, sizeof(line), "%s  %lu us/blit",
           renderer_.hardwareAccelerated() ? "PPA" : "CPU",
           (unsigned long)renderer_.averageBlitUs());
  text(g, infoX, kBtnY + 4, line, fontS(), kTextMut, kRight);

  snprintf(line, sizeof(line), "frames %lu   dropped %lu",
           (unsigned long)CrowCamera::frameCount(),
           (unsigned long)CrowCamera::dropCount());
  text(g, infoX, kBtnY + 26, line, fontS(),
       CrowCamera::dropCount() > 0 ? kAmber : kTextMut, kRight);

  if (recording_) {
    snprintf(line, sizeof(line), "REC %lu:%02lu", (unsigned long)(recElapsedSec_ / 60),
             (unsigned long)(recElapsedSec_ % 60));
    text(g, kCamX + kBtnW + 24, kBtnY + 14, line, fontL(), kRed, kLeft);
  }

  if (frame == nullptr) return;
  snprintf(line, sizeof(line), "%ux%u", frame->width, frame->height);
  text(g, kCamX + kBtnW + 24, kBtnY + 40, line, fontS(), kTextMut, kLeft);
}

void VisionCamUi::drawLive_(const CrowCamera::Frame *frame) {
  Arduino_GFX *g = CrowDisplay::canvas();
  if (g == nullptr) return;

  if (frame != nullptr) {
    // The renderer flushes its own rectangle. Nothing else here may flush the
    // whole panel while a frame is on screen.
    renderer_.drawFrame(*frame, kViewX, kViewY, kViewW, kViewH);
    if (hudVisible_) {
      drawLiveHud_(frame);
      CrowDisplay::flush(0, kHudY, kW, kHudH);
    }
    return;
  }

  // No frame. Say why, rather than leaving a stale image that looks live.
  if (!chromeDirty_) return;
  panel(g, kViewX, kViewY, kViewW, kViewH, 0, kBg);
  const char *reason = CrowCamera::ready()
                           ? (CrowCamera::streaming() ? "waiting for the first frame"
                                                      : "camera paused - tap START")
                           : CrowCamera::lastError();
  text(g, kW / 2, kViewY + kViewH / 2 - 30, "NO SIGNAL", fontXL(), kTextMut, kCenter);
  text(g, kW / 2, kViewY + kViewH / 2 + 16, reason, fontM(), kTextMut, kCenter);
  if (hudVisible_) drawLiveHud_(nullptr);
}

void VisionCamUi::drawGallery_() {
  Arduino_GFX *g = CrowDisplay::canvas();
  if (g == nullptr) return;
  panel(g, 0, kContentTop, kW, kContentBot - kContentTop, 0, kBg);

  if (recorder_ == nullptr || !recorder_->storageReady()) {
    text(g, kW / 2, kContentTop + 160, "NO CARD", fontXL(), kTextMut, kCenter);
    text(g, kW / 2, kContentTop + 210,
         recorder_ != nullptr ? recorder_->lastError() : "recorder unavailable", fontM(),
         kTextMut, kCenter);
    return;
  }

  const uint8_t count = recorder_->mediaCount();
  if (count == 0) {
    text(g, kW / 2, kContentTop + 160, "NOTHING YET", fontXL(), kTextMut, kCenter);
    text(g, kW / 2, kContentTop + 210, "Tap SHUTTER on the Live screen", fontM(), kTextMut,
         kCenter);
    return;
  }

  // Text rows, not thumbnails. Decoding every JPEG on the card to build a
  // thumbnail grid would stall the render loop for seconds; the file name, size
  // and type are what you actually need to find a shot.
  const uint8_t firstRow = galleryPage_ * kGalleryRows;
  for (uint8_t i = 0; i < kGalleryRows; i++) {
    const uint8_t index = firstRow + i;
    if (index >= count) break;
    const CamRecorder::MediaEntry &entry = recorder_->mediaAt(index);
    const int16_t y = kContentTop + 16 + i * (kGalleryRowH + 8);

    panel(g, kRowX, y, kRowW, kGalleryRowH, 10, kSurface, 1, kLine);
    text(g, kRowX + 20, y + 14, entry.name, fontL(), kTextHi, kLeft);
    pill(g, kRowX + kRowW - 220, y + 12, entry.isVideo ? "VIDEO" : "STILL", fontS(),
         kBg, entry.isVideo ? kAmber : kAccent);

    char size[24];
    if (entry.bytes >= 1024UL * 1024UL) {
      snprintf(size, sizeof(size), "%.1f MB", (double)entry.bytes / (1024.0 * 1024.0));
    } else {
      snprintf(size, sizeof(size), "%lu KB", (unsigned long)(entry.bytes / 1024UL));
    }
    text(g, kRowX + kRowW - 20, y + 14, size, fontM(), kTextMut, kRight);
  }

  const uint8_t pages = (uint8_t)((count + kGalleryRows - 1) / kGalleryRows);
  char footer[72];
  snprintf(footer, sizeof(footer), "%u file%s   page %u/%u   %lu MB free%s", count,
           count == 1 ? "" : "s", galleryPage_ + 1, pages,
           (unsigned long)(recorder_->freeBytes() / (1024ULL * 1024ULL)),
           recorder_->mediaTruncated() ? "   (list truncated)" : "");
  text(g, kW / 2, kContentBot - 30, footer, fontS(),
       recorder_->mediaTruncated() ? kAmber : kTextMut, kCenter);

  if (pages > 1) {
    touchButton(g, kRowX, kContentBot - 60, 120, 40, "PREV", false, kAccent);
    touchButton(g, kRowX + kRowW - 120, kContentBot - 60, 120, 40, "NEXT", false, kAccent);
  }
}

void VisionCamUi::drawStream_() {
  Arduino_GFX *g = CrowDisplay::canvas();
  if (g == nullptr) return;
  panel(g, 0, kContentTop, kW, kContentBot - kContentTop, 0, kBg);

  panel(g, kRowX, settingsRowY(0), kRowW, kRowH, 12, kSurface, 1, kLine);
  text(g, kRowX + 20, settingsRowY(0) + 20, "Access point", fontL(), kTextHi, kLeft);
  text(g, kRowX + kRowW - 20, settingsRowY(0) + 20, streamUp_ ? "ON" : "OFF", fontL(),
       streamUp_ ? kGreen : kTextMut, kRight);

  panel(g, kRowX, settingsRowY(1), kRowW, kRowH, 12, kSurface, 1, kLine);
  text(g, kRowX + 20, settingsRowY(1) + 20, "Network", fontL(), kTextHi, kLeft);
  text(g, kRowX + kRowW - 20, settingsRowY(1) + 20,
       streamSsid_.length() ? streamSsid_.c_str() : "-", fontM(), kTextMut, kRight);

  panel(g, kRowX, settingsRowY(2), kRowW, kRowH, 12, kSurface, 1, kLine);
  text(g, kRowX + 20, settingsRowY(2) + 20, "Watch at", fontL(), kTextHi, kLeft);
  text(g, kRowX + kRowW - 20, settingsRowY(2) + 20,
       streamUrl_.length() ? streamUrl_.c_str() : "-", fontM(), kAccent, kRight);

  panel(g, kRowX, settingsRowY(3), kRowW, kRowH, 12, kSurface, 1, kLine);
  text(g, kRowX + 20, settingsRowY(3) + 20, "Viewers", fontL(), kTextHi, kLeft);
  char buf[16];
  snprintf(buf, sizeof(buf), "%u", streamClients_);
  text(g, kRowX + kRowW - 20, settingsRowY(3) + 20, buf, fontL(),
       streamClients_ ? kGreen : kTextMut, kRight);

  // The privacy statement belongs on the screen that turns the radio on, not
  // only in the README - this is the control that starts broadcasting video.
  text(g, kW / 2, kContentBot - 34,
       "Anyone on this network can watch the live feed.", fontS(), kAmber, kCenter);
}

void VisionCamUi::drawSettings_() {
  Arduino_GFX *g = CrowDisplay::canvas();
  if (g == nullptr) return;
  panel(g, 0, kContentTop, kW, kContentBot - kContentTop, 0, kBg);

  Sc2336Sensor *s = CrowCamera::sensor();
  char buf[48];

  panel(g, kRowX, settingsRowY(0), kRowW, kRowH, 12, kSurface, 1, kLine);
  text(g, kRowX + 20, settingsRowY(0) + 20, "Exposure", fontL(), kTextHi, kLeft);
  if (s != nullptr) {
    snprintf(buf, sizeof(buf), "%lu / %lu", (unsigned long)s->exposure(),
             (unsigned long)Sc2336Sensor::maxExposure());
  } else {
    snprintf(buf, sizeof(buf), "no sensor");
  }
  text(g, kRowX + kRowW - kStepW * 2 - 28, settingsRowY(0) + 20, buf, fontM(), kTextMut, kRight);
  touchButton(g, kRowX + kRowW - kStepW * 2 - 8, settingsRowY(0) + 6, kStepW, kRowH - 12,
              "-", false, kAccent);
  touchButton(g, kRowX + kRowW - kStepW, settingsRowY(0) + 6, kStepW, kRowH - 12,
              "+", false, kAccent);

  panel(g, kRowX, settingsRowY(1), kRowW, kRowH, 12, kSurface, 1, kLine);
  text(g, kRowX + 20, settingsRowY(1) + 20, "Auto exposure", fontL(), kTextHi, kLeft);
  // "settling" vs "auto" is not decoration: it tells the user the exposure they
  // are looking at is still moving, so a photo taken now may not match it.
  const char *aeState = !autoExposure_ ? "MANUAL"
                        : autoExposureConverged_ ? "AUTO"
                                                 : "AUTO - settling";
  text(g, kRowX + kRowW - 20, settingsRowY(1) + 20, aeState, fontL(),
       !autoExposure_ ? kTextMut : (autoExposureConverged_ ? kGreen : kAmber), kRight);

  panel(g, kRowX, settingsRowY(2), kRowW, kRowH, 12, kSurface, 1, kLine);
  text(g, kRowX + 20, settingsRowY(2) + 20, "Flip image", fontL(), kTextHi, kLeft);
  if (s != nullptr) {
    snprintf(buf, sizeof(buf), "%s%s",
             s->flippedVertically() ? "vertical " : "",
             s->flippedHorizontally() ? "mirrored" : "");
    text(g, kRowX + kRowW - 20, settingsRowY(2) + 20,
         (s->flippedVertically() || s->flippedHorizontally()) ? buf : "normal", fontM(),
         kTextMut, kRight);
  }

  panel(g, kRowX, settingsRowY(3), kRowW, kRowH, 12, kSurface, 1, kLine);
  text(g, kRowX + 20, settingsRowY(3) + 20, "Battery", fontL(), kTextHi, kLeft);
  // Deliberately not a percentage: this board documents no battery ADC, so any
  // number here would be invented. Saying "unmonitored" is the honest answer.
  text(g, kRowX + kRowW - 20, settingsRowY(3) + 20, "unmonitored", fontM(), kTextMut, kRight);
}

CamEvent VisionCamUi::tick(const CrowCamera::Frame *frame) {
  CamEvent event;
  touch_.tick();
  if (!ready_) {
    ready_ = CrowDisplay::canvas() != nullptr;
    if (!ready_) return event;
    chromeDirty_ = true;
  }

  event = handleTouch_();

  // Chrome repaints only when something changed, or on a slow heartbeat so the
  // fps pill and counters stay current without competing with the frame path.
  const uint32_t now = millis();
  const bool chromeTick = (now - lastChromeMs_) >= 500;
  if (chromeDirty_ || chromeTick) {
    drawChrome_();
    lastChromeMs_ = now;
  }

  switch (screen_) {
    case CAM_SCR_LIVE:
      drawLive_(frame);
      break;
    case CAM_SCR_GALLERY:
      // Free space and the file list change when a capture lands, so this
      // follows the heartbeat rather than only the dirty flag.
      if (chromeDirty_ || chromeTick) drawGallery_();
      break;
    case CAM_SCR_STREAM:
      if (chromeDirty_ || chromeTick) drawStream_();
      break;
    case CAM_SCR_SETTINGS:
      if (chromeDirty_ || chromeTick) drawSettings_();
      break;
    default:
      break;
  }

  // Live flushes its own rectangles inside drawLive_; every other screen is a
  // static dashboard, so one full flush when it changes is correct and cheap.
  if (chromeDirty_ || (chromeTick && screen_ != CAM_SCR_LIVE)) {
    CrowDisplay::flush();
  } else if (chromeTick && screen_ == CAM_SCR_LIVE) {
    CrowDisplay::flush(0, 0, kW, kChromeHeaderH);
    CrowDisplay::flush(0, kChromeTabY, kW, kChromeTabH);
  }
  chromeDirty_ = false;
  return event;
}

#else  // headless build

void VisionCamUi::begin() {}
void VisionCamUi::markDirty() {}
void VisionCamUi::showScreen(CamScreen s) { screen_ = s; }
CamEvent VisionCamUi::tick(const CrowCamera::Frame *) { return CamEvent(); }

#endif
