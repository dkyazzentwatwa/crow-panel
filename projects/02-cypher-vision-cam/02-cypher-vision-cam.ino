#include "config/ProjectConfig.h"
#include <CrowPanelShared.h>
#include <Preferences.h>
#include "src/CamAudio.h"
#include "src/CamPipeline.h"
#include "src/CamRecorder.h"
#include "src/CamStreamServer.h"
#include "src/JpegEncoder.h"
#include "src/VisionCamUi.h"

// Project 02: Cypher Vision Cam. A portable touch camera for the CrowPanel
// Advanced ESP32-P4 - live viewfinder on the 1024x600 panel, stills and video
// clips to SD, and a Wi-Fi live feed served off the onboard ESP32-C6.
//
// Division of labour follows the board: the P4 owns the MIPI-CSI camera and the
// MIPI-DSI panel; the C6 owns the radio. The P4 has no radio of its own and the
// C6 never sees a pixel.
//
// This replaces the former Vision Guard inspection kiosk, which stubbed the
// camera out on the belief that the P4 had no Arduino camera path. That belief
// was wrong: `esp32-camera` does not support the P4, but the P4 does not need
// it - Arduino core 3.3.8 already ships and links the ESP-IDF camera stack
// (esp_driver_cam / esp_driver_isp / esp_driver_jpeg / esp_driver_ppa). Only
// the SC2336 sensor register table had to be written by hand. See TECHNICAL.md.
//
// BRING-UP ORDER IS LOAD-BEARING: SD_MMC -> DSI -> CSI. Mounting SD after the
// DSI framebuffer is live leaves this panel backlit but blank (device-proven on
// Project 20); CSI goes last because it shares the VDD_MIPI_DPHY rail (LDO
// channel 3) that the display bring-up already powers.

EventLog eventLog;
StorageManager storage;
SerialCommandRouter router;
CamPipeline pipeline;
// One JPEG encoder, shared. The JPEG peripheral is a single hardware block and
// its buffers are large, so the recorder and the stream server take the same
// instance rather than each allocating their own.
JpegEncoder jpeg;
CamRecorder recorder;
CamAudio audio;
CamStreamServer streamServer;
VisionCamUi ui;

// Measured capture rate. The sensor mode is nominally 30 fps, but what reaches
// the screen depends on the blit and on how fast loop() drains frames, so the
// UI shows this rather than the datasheet number.
static float measuredFps = 0.0f;
static uint32_t fpsWindowStartMs = 0;
static uint32_t fpsWindowFrames = 0;

// Set by the shutter control (touch, serial, or the BOOT button) and consumed
// in loop(), which is the one place holding a live frame. Capturing anywhere
// else would mean a second code path owning a camera buffer.
static bool shutterRequested = false;

// --- Persistent settings ----------------------------------------------------
//
// Camera orientation is a property of how the panel is MOUNTED, not of this
// session. A camera bolted upside down under a shelf is upside down every time
// it powers on, so making the user re-set the flip after each boot is asking
// them to correct the same physical fact repeatedly.
//
// Stored in NVS rather than on the SD card: it must survive with no card
// present, and it is two bits. StorageManager is in-memory only, so this uses
// Preferences directly.
Preferences settings;

static void loadOrientation() {
  if (!settings.begin("visioncam", /*readOnly=*/true)) return;
  const bool vflip = settings.getBool("vflip", false);
  const bool hmirror = settings.getBool("hmirror", false);
  settings.end();

  Sc2336Sensor *s = CrowCamera::sensor();
  if (s != nullptr && (vflip || hmirror)) {
    s->setFlip(vflip, hmirror);
    Logger::info("settings", String("restored orientation vflip=") + (vflip ? "1" : "0") +
                                 " hmirror=" + (hmirror ? "1" : "0"));
  }
}

static void saveOrientation(bool vflip, bool hmirror) {
  if (!settings.begin("visioncam", /*readOnly=*/false)) return;
  settings.putBool("vflip", vflip);
  settings.putBool("hmirror", hmirror);
  settings.end();
}

// Portrait/landscape, remembered like the flip because it describes how the
// device is being held, not a preference for this session.
static void loadPortrait(bool &portrait, bool &flipped) {
  portrait = false;
  flipped = false;
  if (!settings.begin("visioncam", /*readOnly=*/true)) return;
  portrait = settings.getBool("portrait", false);
  flipped = settings.getBool("portflip", false);
  settings.end();
}

static void savePortrait(bool portrait, bool flipped) {
  if (!settings.begin("visioncam", /*readOnly=*/false)) return;
  settings.putBool("portrait", portrait);
  settings.putBool("portflip", flipped);
  settings.end();
}

// Keeps everything that has to agree about orientation in step: the UI layout,
// the JPEG encoder (so saved files open upright), and the setting itself.
static void applyOrientation(bool portrait, bool flipped) {
  ui.setOrientation(portrait ? CamOrientation::Portrait : CamOrientation::Landscape,
                    flipped);
  // 90 degrees counter-clockwise turns a 1024x600 sensor frame into a true
  // 600x1024 portrait file. The VIEWFINDER is deliberately NOT rotated - the
  // camera and panel are on the same device and turn together, so the preview
  // is already correct and rotating it would put it back on its side.
  jpeg.setRotation(portrait ? 90 : 0);
}

// Volume doubles as the on/off state: 0 is off. One stored value cannot
// disagree with itself the way a separate mute flag and level can.
static uint8_t loadSoundVolume() {
  if (!settings.begin("visioncam", /*readOnly=*/true)) return 90;
  const uint8_t v = settings.getUChar("vol", 90);
  settings.end();
  return v > 100 ? 100 : v;
}

static void saveSoundVolume(uint8_t volume) {
  if (!settings.begin("visioncam", /*readOnly=*/false)) return;
  settings.putUChar("vol", volume);
  settings.end();
}

// Pending record toggle, raised by the web UI and consumed in loop(). Same
// reasoning as shutterRequested: the network thread must not start or stop a
// clip mid-frame, so it asks and loop() acts.
static bool recordToggleRequested = false;

// Callbacks handed to CamStreamServer. Free functions because the server takes
// plain function pointers, matching SerialCommandRouter's style - sketch
// globals are directly reachable and there is nothing to capture.
static void webShutter() { shutterRequested = true; }
static void webRecordToggle() { recordToggleRequested = true; }

// --- BOOT button as a physical shutter release -----------------------------
//
// A real button beats a touch target for this: you can press it without looking
// at the screen, and without your finger covering the viewfinder.
//
// Debounced on the falling edge, and latched so holding the button takes one
// picture rather than a burst. Reusing BOOT is safe - it is a strapping pin
// only at reset, so hold-BOOT-tap-RESET still enters download mode.
// Both boot strapping pins are watched, and either one firing takes a picture.
//
// This is not indecision - the two candidates are GPIO35 (SPI_BOOT) and GPIO36
// (DOWNLOAD_BOOT), the schematic PDF does not resolve which the tactile switch
// actually reaches, and watching both costs two digitalReads per loop. Neither
// pin has another job while the app runs, so there is no downside to accepting
// either. The per-pin press counters on the Settings screen record which one is
// real, and the answer can then be written down instead of guessed.
struct ShutterPin {
  uint8_t gpio;
  bool wasDown;
  uint32_t changedMs;
  uint32_t presses;
};

static ShutterPin shutterPins[] = {
    {VISIONCAM_SHUTTER_PIN, false, 0, 0},
    {VISIONCAM_SHUTTER_ALT_PIN, false, 0, 0},
};
static uint32_t shutterPresses = 0;

static void pollShutterButton() {
  const uint32_t now = millis();
  for (ShutterPin &pin : shutterPins) {
    // Active LOW: the switch pulls the pin to ground against its pull-up.
    const bool down = digitalRead(pin.gpio) == LOW;

    if (down == pin.wasDown) {
      pin.changedMs = 0;  // bounce settled back; cancel the window
      continue;
    }
    // Level changed - start the debounce window and wait for it to hold.
    if (pin.changedMs == 0) {
      pin.changedMs = now;
      continue;
    }
    if (now - pin.changedMs < VISIONCAM_SHUTTER_DEBOUNCE_MS) continue;

    pin.wasDown = down;
    pin.changedMs = 0;
    // Fire on press, not release, so the shutter feels immediate.
    if (down) {
      shutterRequested = true;
      pin.presses++;
      shutterPresses++;
    }
  }
}

// ---------------------------------------------------------------------------
// Serial surface. Every touch action has a 1:1 command here, so the whole device
// is drivable headlessly - which matters on a board whose USB serial port drops
// the moment the app starts running.

static void cmdStatus(const String &) {
  printSystemStatus(Serial, "cypher-vision-cam", storage.eventCount());
  Serial.print(F("[cam] sensor=SC2336 (MIPI-CSI 2-lane 288Mbps, RAW8 1024x600@30)"));
  Serial.println(USE_CAMERA_DRIVER ? F(" driver=real") : F(" driver=absent"));
  Serial.print(F("[cam] capture="));
  Serial.print(USE_CAM_SD ? F("sd") : F("off"));
  Serial.print(F(" rec="));
  Serial.print(VISIONCAM_REC_WIDTH);
  Serial.print('x');
  Serial.print(VISIONCAM_REC_HEIGHT);
  Serial.print(F(" q"));
  Serial.print(VISIONCAM_REC_QUALITY);
  Serial.print(F(" @"));
  Serial.print(VISIONCAM_REC_FPS);
  Serial.println(F("fps (targets, not measured)"));
  // No battery ADC is documented on this board, so the charge state is not
  // reported rather than estimated. Fabricating a percentage here would be
  // worse than saying nothing.
  Serial.println(F("[cam] battery=unmonitored (no documented ADC on this board)"));
}

// `cam` drives the whole pipeline from Serial so the camera can be brought up
// and diagnosed before any touch UI exists. Sub-commands mirror what the Live
// screen's controls will do.
static void cmdCam(const String &args) {
  String a = args;
  a.trim();
  a.toLowerCase();

  if (a.length() == 0 || a == "status") {
    CrowCamera::printStatus(Serial);
    pipeline.printStatus(Serial);
    return;
  }

  // Exposure/white-balance control loops. `ae` with no argument reports; the
  // on/off forms are how a bad automatic result gets ruled out during bring-up.
  if (a == "ae" || a.startsWith("ae ")) {
    const String arg = a.length() > 3 ? a.substring(3) : String("");
    if (arg == "on" || arg == "off") {
      const bool on = (arg == "on");
      pipeline.setAutoExposure(on);
      pipeline.setAutoWhiteBalance(on);
    } else if (arg.startsWith("target ")) {
      pipeline.setTargetLuminance((uint32_t)arg.substring(7).toInt());
    } else if (arg.length() > 0) {
      Serial.println(F("usage: cam ae [on|off|target <n>]"));
      return;
    }
    pipeline.printStatus(Serial);
    return;
  }

  if (a == "begin") {
    Serial.println(CrowCamera::begin(activeHardwareProfile())
                       ? F("[cam] pipeline up")
                       : F("[cam] begin failed"));
    if (!CrowCamera::ready()) {
      Serial.print(F("[cam] reason: "));
      Serial.println(CrowCamera::lastError());
    }
    return;
  }

  if (a == "start") {
    Serial.println(CrowCamera::start() ? F("[cam] streaming")
                                       : F("[cam] start failed"));
    return;
  }

  if (a == "stop") {
    CrowCamera::stop();
    Serial.println(F("[cam] stopped"));
    return;
  }

  if (a == "end") {
    CrowCamera::end();
    Serial.println(F("[cam] torn down"));
    return;
  }

  // Grabs one frame and reports what actually arrived. Before any renderer
  // exists this is the only proof that the CSI path produced real pixels, so it
  // prints a corner sample and a rough mean rather than just "ok" - a buffer of
  // zeros and a live image are indistinguishable from a success flag alone.
  if (a == "grab") {
    CrowCamera::Frame frame;
    if (!CrowCamera::acquire(frame, 500)) {
      Serial.print(F("[cam] no frame within 500ms ("));
      Serial.print(CrowCamera::lastError());
      Serial.println(')');
      return;
    }
    uint32_t sum = 0;
    const uint32_t pixels = (uint32_t)frame.width * frame.height;
    // Sample a sparse grid rather than every pixel: 614k reads over PSRAM would
    // stall the loop for no extra insight.
    const uint32_t step = pixels / 4096;
    uint32_t sampled = 0;
    for (uint32_t i = 0; i < pixels; i += step) {
      const uint16_t px = frame.data[i];
      // RGB565 -> rough luma, good enough to tell "black frame" from "image".
      sum += ((px >> 11) & 0x1F) + ((px >> 5) & 0x3F) / 2 + (px & 0x1F);
      sampled++;
    }
    Serial.print(F("[cam] frame #"));
    Serial.print(frame.sequence);
    Serial.print(F(" "));
    Serial.print(frame.width);
    Serial.print('x');
    Serial.print(frame.height);
    Serial.print(F(" ("));
    Serial.print((uint32_t)frame.bytes);
    Serial.print(F(" B) first=0x"));
    Serial.print(frame.data[0], HEX);
    Serial.print(F(" mid=0x"));
    Serial.print(frame.data[pixels / 2], HEX);
    Serial.print(F(" mean_luma="));
    Serial.print(sampled ? sum / sampled : 0);
    Serial.println(F("/95"));
    CrowCamera::release(frame);
    return;
  }

  if (a.startsWith("exp ")) {
    Sc2336Sensor *s = CrowCamera::sensor();
    if (s == nullptr) {
      Serial.println(F("[cam] no sensor; run `cam begin` first"));
      return;
    }
    s->setExposure((uint32_t)a.substring(4).toInt());
    s->printStatus(Serial);
    return;
  }

  if (a.startsWith("gain ")) {
    Sc2336Sensor *s = CrowCamera::sensor();
    if (s == nullptr) {
      Serial.println(F("[cam] no sensor; run `cam begin` first"));
      return;
    }
    s->setAnalogGain((uint8_t)a.substring(5).toInt());
    s->printStatus(Serial);
    return;
  }

  Serial.println(F("usage: cam [status|begin|start|stop|end|grab|exp <n>|gain <n>|ae ...]"));
}

// Escape hatch. A wrong portrait touch mapping makes the Settings row that
// fixes it hard to hit - the exact trap encountered on hardware - so the same
// control exists over Serial where touch cannot interfere.
static void cmdOrient(const String &args) {
  String a = args;
  a.trim();
  a.toLowerCase();

  bool portrait = false, flipped = false;
  if (a == "landscape" || a == "l") {
    portrait = false;
  } else if (a == "portrait" || a == "p") {
    portrait = true;
  } else if (a == "flipped" || a == "f") {
    portrait = true;
    flipped = true;
  } else if (a == "mark") {
    ui.setShowTouchMark(!ui.showTouchMark());
    Serial.print(F("[orient] touch crosshair "));
    Serial.println(ui.showTouchMark() ? F("ON") : F("OFF"));
    return;
  } else {
    Serial.println(F("usage: orient <landscape|portrait|flipped|mark>"));
    Serial.print(F("[orient] now: "));
    Serial.println(ui.orientation() == CamOrientation::Portrait
                       ? (ui.portraitFlipped() ? "portrait flipped" : "portrait")
                       : "landscape");
    return;
  }

  applyOrientation(portrait, flipped);
  savePortrait(portrait, flipped);
  Serial.print(F("[orient] set to "));
  Serial.println(portrait ? (flipped ? "portrait flipped" : "portrait") : "landscape");
}

static void cmdShot(const String &) {
  if (!recorder.storageReady()) {
    Serial.print(F("[shot] "));
    Serial.println(recorder.lastError());
    return;
  }
  // Same path as the touch shutter: flag it and let loop() do the work while
  // it holds a frame.
  shutterRequested = true;
  Serial.println(F("[shot] queued for the next frame"));
}

static void cmdRec(const String &args) {
  String a = args;
  a.trim();
  a.toLowerCase();

  if (a == "stop" || (a.length() == 0 && recorder.recording())) {
    recorder.stopClip();
    recorder.refreshMediaList();
  } else if (a == "start" || a.length() == 0) {
    if (!recorder.startClip()) {
      Serial.print(F("[rec] "));
      Serial.println(recorder.lastError());
      return;
    }
  } else {
    Serial.println(F("usage: rec [start|stop]  (no argument toggles)"));
    return;
  }
  recorder.printStatus(Serial);
}

static void cmdGallery(const String &) {
  const uint8_t count = recorder.refreshMediaList();
  Serial.print(F("[gallery] "));
  Serial.print(count);
  Serial.println(F(" file(s) in /DCIM"));
  for (uint8_t i = 0; i < count; i++) {
    const CamRecorder::MediaEntry &entry = recorder.mediaAt(i);
    Serial.printf("  %-20s %8lu B  %s\n", entry.name, (unsigned long)entry.bytes,
                  entry.isVideo ? "video" : "still");
  }
  if (recorder.mediaTruncated()) {
    Serial.print(F("[gallery] list truncated at "));
    Serial.print(CamRecorder::kMaxListed);
    Serial.println(F(" entries; the card holds more"));
  }
  Serial.print(F("[gallery] free: "));
  Serial.print((uint32_t)(recorder.freeBytes() / (1024ULL * 1024ULL)));
  Serial.println(F(" MB"));
}
static void cmdStream(const String &args) {
  String a = args;
  a.trim();
  a.toLowerCase();

  if (a == "on" || a == "start") {
    if (!streamServer.begin(&jpeg)) {
      Serial.print(F("[stream] "));
      Serial.println(streamServer.lastError());
      return;
    }
  } else if (a == "off" || a == "stop") {
    streamServer.end();
  } else if (a.length() > 0) {
    Serial.println(F("usage: stream [on|off]"));
    return;
  }
  streamServer.printStatus(Serial);
}

static void cmdHistory(const String &) { eventLog.printHistory(Serial); }

static void cmdScreen(const String &args) {
  String s = args;
  s.trim();
  s.toLowerCase();
  if (s.length() > 0) {
    CamScreen target = CAM_SCR_COUNT;
    if (s == "live") target = CAM_SCR_LIVE;
    else if (s == "gallery") target = CAM_SCR_GALLERY;
    else if (s == "stream") target = CAM_SCR_STREAM;
    else if (s == "settings") target = CAM_SCR_SETTINGS;
    if (target == CAM_SCR_COUNT) {
      Serial.println(F("usage: screen [live|gallery|stream|settings]"));
      return;
    }
    ui.showScreen(target);
  }
  ui.renderSerial(Serial);
}

static void cmdTouch(const String &) { ui.printTouchDiagnostics(Serial); }

static void cmdSelfTest(const String &) {
  int pass = 0, fail = 0;
  Serial.println(F("[selftest] Cypher Vision Cam"));
  auto CHECK = [&](bool c, const char *name) {
    Serial.print(F("  ["));
    Serial.print(c ? F("PASS") : F("FAIL"));
    Serial.print(F("] "));
    Serial.println(name);
    if (c) pass++; else fail++;
  };

  // Stage 1 can only assert the scaffold itself. Each later stage adds its own
  // checks here; a thin selftest that tells the truth beats a padded one.
  CHECK(activeHardwareProfile().name != nullptr, "hardware profile resolves");
  CHECK(VISIONCAM_REC_WIDTH > 0 && VISIONCAM_REC_HEIGHT > 0, "record size configured");
  CHECK(VISIONCAM_REC_FPS > 0 && VISIONCAM_REC_QUALITY > 0 && VISIONCAM_REC_QUALITY <= 100,
        "record fps and quality in range");
  CHECK(VISIONCAM_HTTP_PORT != VISIONCAM_STREAM_PORT,
        "page and stream sockets are distinct ports");
  CHECK(String(VISIONCAM_AP_PASS).length() >= 8,
        "soft-AP password meets the WPA2 minimum");

  // Stage 2: the camera profile must be self-consistent whether or not the
  // driver is compiled in, so a misconfigured HardwareProfile is caught in the
  // headless build rather than as a mystery CSI timeout on hardware.
  const CameraPins &cam = activeHardwareProfile().camera;
  CHECK(cam.width == Sc2336Sensor::kWidth && cam.height == Sc2336Sensor::kHeight,
        "profile geometry matches the sensor mode table");
  CHECK(cam.sccbScl >= 0 && cam.sccbSda >= 0 && cam.sccbScl != cam.sccbSda,
        "SCCB pins are distinct and assigned");
  CHECK(cam.csiLanes == 2 && cam.laneBitRateMbps == 288,
        "CSI link matches the 1024x600 RAW8 mode (2 lane, 288 Mbps)");
  CHECK(Sc2336Sensor::maxExposure() == (uint32_t)(Sc2336Sensor::kVts - 6) * 2 &&
            Sc2336Sensor::maxExposure() > 0,
        "exposure ceiling derives from the mode table VTS");
  // Stage 4: the control loops must be sane before they touch hardware. A
  // damping factor outside (0,1] oscillates or never converges, and a target
  // of zero would drive exposure to its floor and stay there.
  CHECK(pipeline.targetLuminance() > 0, "auto-exposure target is set");
  {
    const bool wasAuto = pipeline.autoExposure();
    pipeline.setAutoExposure(!wasAuto);
    const bool toggled = pipeline.autoExposure() != wasAuto;
    pipeline.setAutoExposure(wasAuto);
    CHECK(toggled && pipeline.autoExposure() == wasAuto,
          "auto exposure toggles and restores");
  }

  // Stage 5: capture configuration must be self-consistent even with no card
  // present, so a bad record setting is caught headlessly.
  CHECK(VISIONCAM_REC_WIDTH <= Sc2336Sensor::kWidth &&
            VISIONCAM_REC_HEIGHT <= Sc2336Sensor::kHeight,
        "record size fits within the sensor frame");
  {
    // The record bitrate target must sit inside what 1-bit SD_MMC sustains
    // (~700 KB/s). This is arithmetic, not a measurement, but it catches a
    // settings change that could not possibly work before the card does.
    const uint32_t pixels = (uint32_t)VISIONCAM_REC_WIDTH * VISIONCAM_REC_HEIGHT;
    const uint32_t estBytesPerFrame = pixels / 8;  // ~q75 4:2:0 rule of thumb
    const uint32_t estBytesPerSec = estBytesPerFrame * VISIONCAM_REC_FPS;
    CHECK(estBytesPerSec < 700000UL,
          "record target fits the 1-bit SD_MMC budget");
  }
#if USE_CAM_SD
  CHECK(recorder.storageReady() || String(recorder.lastError()).length() > 0,
        "recorder reports a reason when storage is unavailable");
  CHECK(!recorder.recording(), "not recording at rest");
#endif

  // Stage 6: the AP must never come up without a real password. This is a
  // safety property, not a nicety - the device broadcasts a live camera feed.
  CHECK(String(VISIONCAM_AP_PASS).length() >= 8,
        "AP password meets WPA2 minimum (no open AP possible)");
#if USE_WIFI
  CHECK(streamServer.running() || String(streamServer.lastError()).length() > 0,
        "stream server reports a reason when it is not running");
  if (streamServer.running()) {
    CHECK(streamServer.url().length() > 0, "stream server publishes a URL");
    CHECK(!streamServer.usingDefaultPassword(),
          "AP is not using the placeholder password");
  }
#endif

#if USE_CAMERA_DRIVER
  // With the driver built in, a camera that never came up should be visible
  // here rather than only in `cam status`.
  CHECK(CrowCamera::ready(), "camera pipeline is up");
  CHECK(CrowCamera::dropCount() == 0 || CrowCamera::frameCount() > 0,
        "no frames dropped before any were produced");
  {
    // Colour gains must round-trip through the CCM. This catches a clamp or a
    // matrix write that silently does nothing, which would otherwise look like
    // "AWB runs but never changes the picture".
    float r = 0, g = 0, b = 0;
    const bool applied = CrowCamera::setColorGains(1.5f, 1.0f, 1.25f);
    CrowCamera::colorGains(r, g, b);
    CHECK(!applied || (fabsf(r - 1.5f) < 0.01f && fabsf(b - 1.25f) < 0.01f),
          "colour gains round-trip through the CCM");
    CrowCamera::setColorGains(1.0f, 1.0f, 1.0f);
  }
#endif

  Serial.print(F("[selftest] summary: "));
  Serial.print(pass);
  Serial.print(F(" passed, "));
  Serial.print(fail);
  Serial.print(F(" failed -> "));
  Serial.println(fail == 0 ? F("PASS") : F("FAIL"));
}

// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(300);

  Logger::info("boot", "Cypher Vision Cam starting");

  // Physical shutter. INPUT_PULLUP because the switch shorts to ground; the alt
  // pin is configured only so its level can be displayed for confirmation.
  pinMode(VISIONCAM_SHUTTER_PIN, INPUT_PULLUP);
  pinMode(VISIONCAM_SHUTTER_ALT_PIN, INPUT_PULLUP);

  storage.begin("vision-cam");
  eventLog.add("boot: cypher-vision-cam");

  // SD FIRST - before the DSI framebuffer exists. Mounting SD_MMC after DSI is
  // live leaves this panel backlit but blank (device-proven on Project 20). A
  // missing card is not fatal; the camera and viewfinder work without one and
  // the Gallery screen says why capture is unavailable.
  recorder.mountStorage();

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  // manualFlush=true: Arduino_GFX otherwise cache-syncs on every draw call,
  // which a per-frame video blit cannot afford. The renderer flushes only the
  // viewfinder rect once per frame from Stage 3 onward.
  CrowDisplay::begin(activeHardwareProfile(), "CYPHER VISION CAM", /*manualFlush=*/true);
  // Boot status goes on the panel because USBMode=hwcdc drops the serial port
  // the moment the app runs - the screen is the only diagnostic that survives.
  CrowDisplay::setLine(0, "booting...");
  CrowDisplay::flush();
#endif

  // Camera last: the D-PHY rail it needs is the one the display just powered,
  // and from Stage 3 the renderer blits into the framebuffer the display owns.
  // A camera failure is not fatal - the panel and the serial console stay up so
  // the reason is readable, which matters on a board whose USB serial drops the
  // moment the app runs.
  if (CrowCamera::begin(activeHardwareProfile())) {
    // Orientation before streaming, so the very first frame is already the
    // right way up rather than flipping a moment after boot.
    loadOrientation();
    CrowCamera::start();
    eventLog.add("camera up");
  } else {
    eventLog.add(String("camera down: ") + CrowCamera::lastError());
  }
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  CrowDisplay::setLine(1, CrowCamera::ready() ? "camera: streaming"
                                              : String("camera: ") + CrowCamera::lastError());
  CrowDisplay::flush();
#endif

  // UI after the camera so the Live screen can report the real pipeline state
  // on its first paint instead of a placeholder.
  // Encoder before its two consumers. Sized for the full sensor frame, which is
  // the largest thing ever encoded (a still); everything else scales down.
  jpeg.begin(Sc2336Sensor::kWidth, Sc2336Sensor::kHeight);

  // Recorder after the camera: there is nothing to encode until frames exist.
  recorder.begin(&jpeg);
  recorder.refreshMediaList();

  // Audio is independent of the camera, but must be up before the first capture
  // so the shutter is never silent.
  audio.begin(activeHardwareProfile());
  audio.setVolume(loadSoundVolume());
  audio.setEnabled(audio.volume() > 0);
  ui.setSound(audio.enabled(), audio.volume());

  // The radio comes up last. It is the only outward-facing thing here, and
  // advertising a feed before the camera works would be the wrong order.
  streamServer.attach(&recorder, webShutter, webRecordToggle);
  if (!streamServer.begin(&jpeg)) {
    Logger::warn("boot", String("stream server down: ") + streamServer.lastError());
  }

  pipeline.begin();
  ui.setRecorder(&recorder);
  ui.begin();
  // After ui.begin() so the first layout is already the remembered one.
  {
    bool portrait = false, flipped = false;
    loadPortrait(portrait, flipped);
    applyOrientation(portrait, flipped);
  }
  fpsWindowStartMs = millis();

  router.begin(Serial, "cypher-vision-cam");
  router.on("status", "system + camera status", cmdStatus);
  router.on("history", "event log, oldest first", cmdHistory);
  router.on("cam", "camera pipeline control", cmdCam);
  router.on("orient", "landscape|portrait|flipped|mark - orientation escape hatch",
            cmdOrient);
  router.on("shot", "capture a still to SD", cmdShot);
  router.on("rec", "start/stop a video clip", cmdRec);
  router.on("gallery", "list captured media", cmdGallery);
  router.on("stream", "soft-AP + MJPEG stream state", cmdStream);
  router.on("screen", "show or switch a screen", cmdScreen);
  router.on("touch", "raw + mapped touch coordinates", cmdTouch);
  router.on("selftest", "drive the flow headlessly", cmdSelfTest);

  cmdStatus("");
  Serial.println(F("[boot] ready - type `help`"));
}

// Executes what the touch console asked for. Kept here rather than in the UI so
// the UI stays a renderer: every branch below is reachable from Serial too.
static void applyUiEvent(const CamEvent &event) {
  switch (event.type) {
    case CamEventType::CameraToggle:
      if (CrowCamera::streaming()) {
        CrowCamera::stop();
        eventLog.add("camera paused");
      } else {
        CrowCamera::start();
        eventLog.add("camera started");
      }
      ui.markDirty();
      break;

    case CamEventType::ExposureUp:
    case CamEventType::ExposureDown: {
      Sc2336Sensor *s = CrowCamera::sensor();
      if (s == nullptr) break;
      // Multiplicative steps, not additive: exposure is perceptually
      // logarithmic, so a fixed increment is unusably coarse at the bottom of
      // the range and unusably fine at the top.
      // Touching exposure by hand means the user wants control, so drop out of
      // auto rather than fighting the loop for the next 200 ms.
      if (pipeline.autoExposure()) pipeline.setAutoExposure(false);
      const uint32_t current = s->exposure() ? s->exposure() : 1;
      const uint32_t next = event.type == CamEventType::ExposureUp
                                ? current + current / 4 + 1
                                : current - current / 5;
      s->setExposure(next);
      ui.markDirty();
      break;
    }

    case CamEventType::FlipToggle: {
      Sc2336Sensor *s = CrowCamera::sensor();
      if (s == nullptr) break;
      // Cycles normal -> vertical -> both -> horizontal -> normal, so one
      // control reaches every orientation. Done in the sensor, not the blitter:
      // the SC2336 flips for free, the PPA would not.
      const bool v = s->flippedVertically();
      const bool h = s->flippedHorizontally();
      if (!v && !h) s->setFlip(true, false);
      else if (v && !h) s->setFlip(true, true);
      else if (v && h) s->setFlip(false, true);
      else s->setFlip(false, false);
      // Persist immediately: orientation describes the mounting, so it should
      // survive a power cycle without being re-set.
      saveOrientation(s->flippedVertically(), s->flippedHorizontally());
      ui.markDirty();
      break;
    }

    case CamEventType::AutoExposureToggle:
      // One control drives both loops: they are a single "let the camera sort
      // itself out" concept to a user, and splitting them would put a knob on
      // screen that nobody has a reason to touch independently.
      pipeline.setAutoExposure(!pipeline.autoExposure());
      pipeline.setAutoWhiteBalance(pipeline.autoExposure());
      ui.markDirty();
      break;
    case CamEventType::Shutter:
      // Handled in loop(), which is the only place that holds a live frame.
      // Setting a flag rather than capturing here keeps the acquire/release
      // contract in exactly one function.
      shutterRequested = true;
      break;

    case CamEventType::RecordToggle:
      // Routed through the same flag the web path uses, so start/stop happens
      // in exactly one place and always between frames.
      recordToggleRequested = true;
      break;
    case CamEventType::OrientationToggle: {
      // Cycle landscape -> portrait -> portrait flipped -> landscape. The two
      // portrait states differ only in which way round the panel is held, which
      // software cannot detect - so it is offered rather than guessed.
      const bool wasPortrait = ui.orientation() == CamOrientation::Portrait;
      const bool wasFlipped = ui.portraitFlipped();
      bool portrait = true, flipped = false;
      if (!wasPortrait) {
        portrait = true;
        flipped = false;
      } else if (!wasFlipped) {
        portrait = true;
        flipped = true;
      } else {
        portrait = false;
        flipped = false;
      }
      applyOrientation(portrait, flipped);
      savePortrait(portrait, flipped);
      break;
    }

    case CamEventType::VolumeUp:
    case CamEventType::VolumeDown: {
      // 20% steps, 0 to 100. Zero is "off" - one control rather than a mute
      // switch and a level that can disagree with each other.
      int step = (event.type == CamEventType::VolumeUp) ? 20 : -20;
      int vol = (int)audio.volume() + step;
      if (vol < 0) vol = 0;
      if (vol > 100) vol = 100;
      audio.setVolume((uint8_t)vol);
      audio.setEnabled(vol > 0);
      saveSoundVolume((uint8_t)vol);
      ui.setSound(audio.enabled(), audio.volume());
      // Play at the new level so the setting demonstrates itself rather than
      // making you take a photo to find out.
      if (vol > 0) audio.play(CamSound::Shutter);
      ui.markDirty();
      break;
    }

    case CamEventType::StreamToggle:
      if (streamServer.running()) {
        streamServer.end();
        eventLog.add("stream stopped");
      } else if (!streamServer.begin(&jpeg)) {
        Serial.print(F("[stream] "));
        Serial.println(streamServer.lastError());
      } else {
        eventLog.add(String("stream up: ") + streamServer.url());
      }
      ui.markDirty();
      break;

    case CamEventType::None:
    default:
      break;
  }
}

void loop() {
  router.poll();
  pollShutterButton();

  // Drain one frame per iteration and hand it to the UI, which blits it and
  // releases nothing - ownership stays here, so the release below always runs
  // and the acquire/release contract cannot be broken by a UI early-return.
  CrowCamera::Frame frame;
  const bool haveFrame = CrowCamera::acquire(frame, 0);

  if (haveFrame) {
    fpsWindowFrames++;
    const uint32_t now = millis();
    const uint32_t elapsed = now - fpsWindowStartMs;
    if (elapsed >= 1000) {
      measuredFps = (float)fpsWindowFrames * 1000.0f / (float)elapsed;
      fpsWindowFrames = 0;
      fpsWindowStartMs = now;
      ui.setMeasuredFps(measuredFps);
    }
  }

  const CamEvent event = ui.tick(haveFrame ? &frame : nullptr);

  // Capture work happens while the frame is still held, and before release.
  if (haveFrame) {
    if (shutterRequested) {
      shutterRequested = false;
      if (recorder.captureStill(frame)) {
        // Sound AFTER the write succeeds, not before. A click that fires on
        // intent rather than outcome teaches you to trust a capture that may
        // not have happened.
        audio.play(CamSound::Shutter);
        recorder.refreshMediaList();
        eventLog.add("still captured");
      } else {
        audio.play(CamSound::Error);
        Serial.print(F("[shot] "));
        Serial.println(recorder.lastError());
      }
      ui.markDirty();
    }
    // Rate-limits internally to the configured record fps; cheap when idle.
    recorder.offerFrame(frame);
  }

  // Web-requested record toggle, handled here rather than in the HTTP handler
  // so start/stop never lands mid-frame.
  if (recordToggleRequested) {
    recordToggleRequested = false;
    if (recorder.recording()) {
      recorder.stopClip();
      audio.play(CamSound::RecordStop);
      recorder.refreshMediaList();
      eventLog.add(String("clip: ") + String(recorder.clipFrames()) + " frames, " +
                   String(recorder.droppedFrames()) + " dropped");
    } else if (recorder.startClip()) {
      audio.play(CamSound::RecordStart);
    } else {
      audio.play(CamSound::Error);
    }
    ui.markDirty();
  }

  // Stream while the frame is still held - it encodes from the live buffer.
  // Rate-limits internally, and is a no-op with no viewer connected.
  streamServer.handle(haveFrame ? &frame : nullptr);

  if (haveFrame) CrowCamera::release(frame);

  ui.setRecording(recorder.recording(), recorder.clipElapsedSec(),
                  recorder.droppedFrames());
  streamServer.setRecording(recorder.recording());
  ui.setServing(streamServer.servingFile(), streamServer.servingName());
  ui.setStreamState(streamServer.running(), streamServer.ssid(), streamServer.url(),
                    streamServer.viewerCount(), streamServer.stationCount(),
                    streamServer.stationUrl());

  // After release(), never before: the statistics reads block for up to a
  // frame time, and holding a buffer across that would starve the receiver.
  pipeline.tick();
  ui.setAutoExposure(pipeline.autoExposure(), pipeline.converged());
  ui.setWhitePatches(pipeline.lastWhitePatches());
  ui.setShutterButton(digitalRead(VISIONCAM_SHUTTER_PIN) == HIGH,
                      digitalRead(VISIONCAM_SHUTTER_ALT_PIN) == HIGH,
                      shutterPins[0].presses, shutterPins[1].presses);

  applyUiEvent(event);

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  CrowDisplay::tick();
#endif

  // No delay when frames are flowing - a fixed sleep here would cap the frame
  // rate below what the pipeline can do. Yield only when idle so the serial
  // router and the Wi-Fi stack still get time.
  if (!haveFrame) delay(2);
}
