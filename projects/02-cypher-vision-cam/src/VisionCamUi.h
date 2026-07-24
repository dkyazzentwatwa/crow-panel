#ifndef VISION_CAM_UI_H
#define VISION_CAM_UI_H

#include "../config/ProjectConfig.h"
#include <Arduino.h>
#include <CrowPanelShared.h>
#include "CamRecorder.h"
#include "CamRenderer.h"

// Four-tab touch console for the Cypher Vision Cam, built on the shared
// CrowDisplay bring-up and the Widgets:: toolkit (dark "ops" palette, FreeSans
// fonts, cards/pills, headerBar/tabBar chrome). No LVGL.
//
// The Live screen is unlike every other screen in this repo: it is video, not a
// dashboard. The camera frame is blitted straight into the DSI framebuffer by
// CamRenderer and only its rectangle is flushed, so this class must NEVER
// repaint the whole screen while Live is showing - that is what would tear.
// Chrome therefore has its own dirty flag, separate from the frame path.
//
// tick() reads touch, draws, and returns a typed CamEvent for the sketch to
// execute. The UI renders state and reports intent; it does not own the camera,
// so every touch action has a 1:1 serial command.

enum CamScreen : uint8_t {
  CAM_SCR_LIVE = 0,
  CAM_SCR_GALLERY,
  CAM_SCR_STREAM,
  CAM_SCR_SETTINGS,
  CAM_SCR_COUNT,
};

enum class CamEventType : uint8_t {
  None = 0,
  Shutter,        // capture a still
  RecordToggle,   // start/stop a clip
  CameraToggle,   // start/stop streaming from the sensor
  ExposureUp,     // manual exposure step
  ExposureDown,
  AutoExposureToggle,
  FlipToggle,
  StreamToggle,   // soft-AP + MJPEG server on/off
};

struct CamEvent {
  CamEventType type = CamEventType::None;
  int16_t index = -1;
};

const char *camScreenName(CamScreen s);

class VisionCamUi {
 public:
  void begin();

  // Call once per loop(). `frame` may be null when no capture is available,
  // in which case Live shows why rather than a frozen last image.
  CamEvent tick(const CrowCamera::Frame *frame);

  void showScreen(CamScreen s);
  CamScreen screen() const { return screen_; }
  const char *screenName() const { return camScreenName(screen_); }

  // Forces a full chrome repaint. Cheap to call; the frame path is unaffected.
  void markDirty();

  // Measured capture rate, fed from the sketch so the UI does not have to time
  // the pipeline itself. Displayed rather than a nominal "30 fps" claim.
  void setMeasuredFps(float fps) { fps_ = fps; }

  // Auto-exposure state, owned by CamPipeline. `converged` is shown separately
  // from `on` so the Settings row can distinguish "auto, and settled" from
  // "auto, still hunting" instead of implying the exposure is final.
  void setAutoExposure(bool on, bool converged);

  // Recording state, owned by the sketch's recorder.
  void setRecording(bool on, uint32_t elapsedSec, uint32_t droppedFrames);

  // The Gallery screen reads the media list straight off the recorder rather
  // than keeping a copy - the list is already a fixed-size array there, and two
  // copies would only be two things to get out of sync.
  void setRecorder(const CamRecorder *recorder) { recorder_ = recorder; }

  // Stream state, owned by the sketch's stream server.
  void setStreamState(bool up, const String &ssid, const String &url, uint8_t clients);

  void printTouchDiagnostics(Print &out) const;
  void renderSerial(Print &out) const;

 private:
  CamScreen screen_ = CAM_SCR_LIVE;
  CrowTouch touch_;
  CamRenderer renderer_;
  const CamRecorder *recorder_ = nullptr;
  uint8_t galleryPage_ = 0;

  float fps_ = 0.0f;
  bool autoExposure_ = true;
  bool autoExposureConverged_ = false;
  bool recording_ = false;
  uint32_t recElapsedSec_ = 0;
  uint32_t recDropped_ = 0;
  bool streamUp_ = false;
  String streamSsid_;
  String streamUrl_;
  uint8_t streamClients_ = 0;

  // Live-screen HUD visibility. Tapping the image toggles it, because a
  // viewfinder with permanent overlay chrome is a worse viewfinder.
  bool hudVisible_ = true;

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  bool ready_ = false;
  bool chromeDirty_ = true;   // chrome only; the frame path never sets this
  uint32_t lastChromeMs_ = 0;

  CamEvent handleTouch_();
  void drawChrome_();
  void drawLive_(const CrowCamera::Frame *frame);
  void drawLiveHud_(const CrowCamera::Frame *frame);
  void drawGallery_();
  void drawStream_();
  void drawSettings_();
#endif
};

#endif
