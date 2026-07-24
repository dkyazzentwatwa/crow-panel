#ifndef VISION_CAM_STREAM_SERVER_H
#define VISION_CAM_STREAM_SERVER_H

#include "../config/ProjectConfig.h"
#include <Arduino.h>
#include <CrowPanelShared.h>
#include "JpegEncoder.h"

// Serves the live camera feed over Wi-Fi from the onboard ESP32-C6.
//
// The panel hosts its own access point, so this works with no router - which is
// the point of a portable camera. If station credentials are configured it also
// joins that network, and then the stream is reachable from either side.
//
// TWO SOCKETS, ON PURPOSE:
//   :80 (WebServer)  - the viewer page, a single-shot /snapshot, /health.
//   :81 (WiFiServer) - /stream, multipart/x-mixed-replace, pushed frame by
//                      frame from loop().
// WebServer::handleClient() runs a request to completion before returning, and
// an MJPEG response never completes - serving the stream from :80 would stall
// the render loop forever. The raw socket on :81 lets the pusher hand over one
// frame per loop() iteration and return immediately.
//
// One viewer at a time. Each additional client would multiply both the encode
// cost and the SDIO traffic to the C6, and a second viewer silently halving the
// first one's frame rate is worse than being told the seat is taken.

class CamStreamServer {
 public:
  // Brings up the radio and both sockets. `encoder` is the shared JPEG encoder
  // (the recorder uses the same one). Returns false if the AP could not start.
  bool begin(JpegEncoder *encoder);

  // Tears the radio down. Safe to call when never started.
  void end();

  // Call once per loop(). Services :80, and pushes at most one frame to the
  // stream client - `frame` may be null when no capture is available.
  void handle(const CrowCamera::Frame *frame);

  bool running() const { return running_; }
  bool hasViewer() const { return viewerConnected_; }
  uint8_t viewerCount() const { return viewerConnected_ ? 1 : 0; }

  const String &ssid() const { return ssid_; }
  const String &url() const { return url_; }

  // Frames per second actually delivered to the viewer, measured. The link to
  // the C6 is SDIO and its real throughput under video load is unmeasured, so
  // this is the number that matters rather than the camera's frame rate.
  float measuredFps() const { return streamFps_; }
  uint32_t framesSent() const { return framesSent_; }

  // True when the AP is running on the placeholder password from
  // ProjectConfig.h rather than a real one from CamSecrets.h. Surfaced so the
  // UI can warn instead of quietly broadcasting video behind a known secret.
  bool usingDefaultPassword() const { return defaultPassword_; }

  const char *lastError() const { return lastError_; }
  void printStatus(Print &out) const;

 private:
#if USE_WIFI
  void serveViewerPage_();
  void serveSnapshot_(const CrowCamera::Frame *frame);
  void serviceStreamSocket_(const CrowCamera::Frame *frame);
  bool startAccessPoint_();
#endif

  JpegEncoder *encoder_ = nullptr;
  bool running_ = false;
  bool viewerConnected_ = false;
  bool defaultPassword_ = false;
  String ssid_;
  String url_;
  uint32_t framesSent_ = 0;
  uint32_t lastFrameMs_ = 0;
  uint32_t fpsWindowStartMs_ = 0;
  uint32_t fpsWindowFrames_ = 0;
  float streamFps_ = 0.0f;
  // Latest frame the snapshot endpoint can serve. The frame itself is not kept
  // (it belongs to the camera driver); this only records whether one has been
  // seen, so /snapshot can answer honestly before the first capture.
  bool sawFrame_ = false;
  const char *lastError_ = "not started";
};

#endif
