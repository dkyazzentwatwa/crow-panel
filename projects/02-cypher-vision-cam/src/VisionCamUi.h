#ifndef VISION_CAM_UI_H
#define VISION_CAM_UI_H

#include "../config/ProjectConfig.h"
#include <Arduino.h>
#include <CrowPanelShared.h>
#include "CamRecorder.h"
#include "CamRenderer.h"
#include "ImageViewer.h"

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
  StreamToggle,       // soft-AP + MJPEG server on/off
  OrientationToggle,  // landscape <-> portrait
  VolumeUp,           // cue volume step; 0 is off
  VolumeDown,
};

struct CamEvent {
  CamEventType type = CamEventType::None;
  int16_t index = -1;
};

const char *camScreenName(CamScreen s);

// How the device is being held. Portrait means the panel stands on its short
// edge, phone-style.
//
// The VIEWFINDER needs no pixel rotation for this: the camera and the panel are
// bolted to the same device, so they turn together and the preview stays
// correct. What does change is the CHROME (which would otherwise read sideways)
// and the SAVED FILES (which are stored long-axis-first and would open rotated
// on a computer).
enum class CamOrientation : uint8_t { Landscape = 0, Portrait = 1 };

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

  // White-patch count from the last AWB update. Zero means the loop found no
  // neutral reference and its gains are frozen - the Settings screen shows it
  // in red, because that is the difference between "correcting badly" and
  // "never ran", and there is no serial port to ask.
  void setWhitePatches(uint32_t patches) { whitePatches_ = patches; }
  void setSound(bool on, uint8_t volume) {
    if (on != soundEnabled_ || volume != soundVolume_) markDirty();
    soundEnabled_ = on;
    soundVolume_ = volume;
  }

  // Called by the sketch while the web server has a file transfer in flight.
  void setServing(bool serving, const String &name) {
    if (serving != serving_) markDirty();
    serving_ = serving;
    servingName_ = name;
  }

  // Physical shutter state, for the Settings readout. `altLevel` is the other
  // boot strapping pin, shown only so the correct button GPIO can be confirmed
  // by watching which one moves when the button is pressed - the schematic PDF
  // is not clear enough to settle it by reading.
  void setShutterButton(bool level, bool altLevel, uint32_t presses,
                        uint32_t altPresses) {
    shutterLevel_ = level;
    shutterAltLevel_ = altLevel;
    shutterPresses_ = presses;
    shutterAltPresses_ = altPresses;
  }

  // Recording state, owned by the sketch's recorder.
  void setRecording(bool on, uint32_t elapsedSec, uint32_t droppedFrames);

  // The Gallery screen reads the media list straight off the recorder rather
  // than keeping a copy - the list is already a fixed-size array there, and two
  // copies would only be two things to get out of sync.
  void setRecorder(const CamRecorder *recorder) { recorder_ = recorder; }

  // Switching re-lays out every screen and re-rotates the GFX canvas. Safe to
  // call before begin(); the layout is recomputed there too.
  void setOrientation(CamOrientation orientation, bool flipped = false);
  CamOrientation orientation() const { return orientation_; }
  bool portraitFlipped() const { return portraitFlipped_; }

  // Draws a crosshair wherever the UI believes a tap landed. The fastest way to
  // tell a wrong touch mapping from a wrong hit rectangle.
  void setShowTouchMark(bool on) {
    showTouchMark_ = on;
    markDirty();
  }
  bool showTouchMark() const { return showTouchMark_; }

  // Stream state, owned by the sketch's stream server.
  void setStreamState(bool up, const String &ssid, const String &url, uint8_t clients,
                      uint8_t stations = 0, const String &stationUrl = String());

  void printTouchDiagnostics(Print &out) const;
  void renderSerial(Print &out) const;

 private:
  CamScreen screen_ = CAM_SCR_LIVE;
  CrowTouch touch_;
  CamRenderer renderer_;
  CamOrientation orientation_ = CamOrientation::Landscape;
  // Which way the panel was physically turned. Software cannot know, so this is
  // a user choice - see mapTouch_.
  bool portraitFlipped_ = false;
  // Last mapped touch, drawn as a crosshair while set. Turns "touch is off"
  // into something you can see rather than describe.
  bool showTouchMark_ = false;
  int16_t markX_ = -1, markY_ = -1;
  uint32_t markMs_ = 0;
  const CamRecorder *recorder_ = nullptr;
  ImageViewer viewer_;
  uint8_t galleryPage_ = 0;

  float fps_ = 0.0f;
  // Measured loop iterations per second, counted here rather than passed in:
  // tick() is called exactly once per loop(), so this is the loop rate by
  // definition and needs no cooperation from the sketch.
  uint32_t loopHz_ = 0;
  uint32_t loopCount_ = 0;
  uint32_t loopWindowMs_ = 0;
  bool autoExposure_ = true;
  bool autoExposureConverged_ = false;
  uint32_t whitePatches_ = 0;
  bool soundEnabled_ = true;
  uint8_t soundVolume_ = 90;
  bool shutterLevel_ = true;
  bool shutterAltLevel_ = true;
  uint32_t shutterPresses_ = 0;
  uint32_t shutterAltPresses_ = 0;
  bool recording_ = false;
  uint32_t recElapsedSec_ = 0;
  uint32_t recDropped_ = 0;
  bool streamUp_ = false;
  String streamSsid_;
  String streamUrl_;
  uint8_t streamClients_ = 0;
  uint8_t streamStations_ = 0;
  String stationUrl_;

  // Live-screen control bar visibility. Tapping the image toggles it, because a
  // viewfinder with permanent overlay chrome is a worse viewfinder.
  bool barVisible_ = true;

  // Set while the web server is streaming a file off the SD card. That transfer
  // owns the loop for its duration, so the panel says so instead of appearing
  // to have crashed.
  bool serving_ = false;
  String servingName_;

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  bool ready_ = false;
  bool chromeDirty_ = true;   // chrome only; the frame path never sets this
  uint32_t lastChromeMs_ = 0;

  CamEvent handleTouch_();
  void drawChrome_();
  void drawLive_(const CrowCamera::Frame *frame);
  void drawLiveHud_(const CrowCamera::Frame *frame);
  void drawLiveMinimal_();
  void drawServingOverlay_();
  void mapTouch_(int16_t &x, int16_t &y) const;

  // Content-change detection for the static screens. Repainting them on a timer
  // flickers, because the panel is single-framebuffer and the DSI scans it while
  // the redraw is still landing.
  bool takeIfChanged_(uint32_t sig);
  uint32_t settingsSignature_() const;
  uint32_t streamSignature_() const;
  uint32_t gallerySignature_() const;
  uint32_t lastSig_ = 0;
  bool drewThisFrame_ = false;
  void drawGallery_();
  void drawStream_();
  void drawSettings_();
#endif
};

#endif
