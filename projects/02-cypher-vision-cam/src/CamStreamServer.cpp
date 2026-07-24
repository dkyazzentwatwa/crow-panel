// Soft-AP + MJPEG live feed served from the onboard ESP32-C6.
// COMPILE-VERIFIED on esp32:esp32:esp32p4 (core 3.3.8). NOT HARDWARE-VERIFIED -
// no client has ever connected and the SDIO link's throughput under video load
// is unmeasured. Treat every fps figure here as unproven until it is observed.

#include "CamStreamServer.h"

#if USE_WIFI

#include <WiFi.h>
#include <WebServer.h>

namespace {

WebServer *gPages = nullptr;       // :80
WiFiServer *gStreamSocket = nullptr;  // :81
WiFiClient gViewer;

// Multipart boundary. Arbitrary, but it must not appear in the JPEG payload -
// a hyphen-heavy token cannot collide with JFIF marker bytes.
constexpr const char *kBoundary = "cypherframe";

// Stream resolution. Lower than the record size on purpose: this crosses an
// SDIO link to a separate chip and then a Wi-Fi hop, and 640x480 at q60 lands
// near 25 KB/frame, which is roughly 2 Mbit/s at 10 fps.
constexpr uint16_t kStreamWidth = 640;
constexpr uint16_t kStreamHeight = 480;
constexpr uint8_t kStreamQuality = 60;
constexpr uint32_t kStreamIntervalMs = 100;  // 10 fps ceiling

// The viewer page. Deliberately a single self-contained string with no external
// anything: the panel is often its own island with no route to the internet, so
// a page that pulled in a CDN stylesheet would render as unstyled text.
const char kViewerPage[] PROGMEM =
    "<!doctype html><html><head><meta charset=utf-8>"
    "<meta name=viewport content='width=device-width,initial-scale=1'>"
    "<title>Cypher Vision Cam</title><style>"
    "body{margin:0;background:#0b111c;color:#eaf0f7;"
    "font-family:system-ui,-apple-system,sans-serif;text-align:center}"
    "h1{font-size:1rem;font-weight:600;letter-spacing:.08em;padding:14px;margin:0;"
    "color:#8296ac;text-transform:uppercase}"
    "img{max-width:100%;height:auto;display:block;margin:0 auto;background:#16202f}"
    "p{color:#8296ac;font-size:.8rem;padding:12px}"
    "a{color:#16c2c9}"
    "</style></head><body>"
    "<h1>Cypher Vision Cam</h1>"
    "<img src='http://%STREAM_HOST%/stream' alt='live feed'>"
    "<p>Live feed &middot; <a href='/snapshot'>still snapshot</a></p>"
    "</body></html>";

}  // namespace

bool CamStreamServer::startAccessPoint_() {
  // MUST come before any WiFi.* call: the CrowPanel wires the P4<->C6 SDIO data
  // lines differently from the core's default map, and without this esp_hosted
  // hangs during handshake and the board watchdog-reboots.
  configureCrowPanelHostedWiFiPins("vision-cam");

  String ssid = VISIONCAM_AP_SSID;
  if (ssid.length() == 0) {
    // Derive from the MAC so two panels in one room do not collide.
    uint8_t mac[6] = {0};
    WiFi.macAddress(mac);
    char generated[24];
    snprintf(generated, sizeof(generated), "CypherCam-%02X%02X", mac[4], mac[5]);
    ssid = generated;
  }

  const String password = VISIONCAM_AP_PASS;
  defaultPassword_ = (password == "changeme-cypher");

  // WPA2 needs 8-63 characters. Refusing to start beats falling back to an open
  // AP: this device broadcasts a live camera feed, and an open AP would put it
  // in range of anyone.
  if (password.length() < 8) {
    lastError_ = "AP password too short (WPA2 needs 8+); refusing to start an open AP";
    Logger::warn("stream", lastError_);
    return false;
  }

  // AP_STA rather than AP: the panel can host its own network AND join an
  // existing one, so the feed is reachable from either side.
  WiFi.mode(WIFI_AP_STA);
  if (!WiFi.softAP(ssid.c_str(), password.c_str())) {
    lastError_ = "soft-AP would not start";
    Logger::warn("stream", lastError_);
    return false;
  }

  ssid_ = ssid;
  const IPAddress ip = WiFi.softAPIP();
  url_ = String("http://") + ip.toString() + "/";

  if (String(WIFI_SSID).length() > 0) {
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    Logger::info("stream", String("also joining ") + WIFI_SSID);
  }

  Logger::info("stream", String("AP '") + ssid_ + "' up at " + url_);
  if (defaultPassword_) {
    Logger::warn("stream",
                 "AP is using the placeholder password - copy config/CamSecrets.example.h "
                 "to CamSecrets.h and set your own");
  }
  return true;
}

bool CamStreamServer::begin(JpegEncoder *encoder) {
  encoder_ = encoder;
  if (running_) return true;
  if (encoder_ == nullptr || !encoder_->ready()) {
    lastError_ = "no JPEG encoder";
    return false;
  }
  if (!startAccessPoint_()) return false;

  gPages = new WebServer(VISIONCAM_HTTP_PORT);
  gPages->on("/", HTTP_GET, [this]() { serveViewerPage_(); });
  gPages->on("/health", HTTP_GET, [this]() {
    String body = String("{\"ok\":true,\"service\":\"cypher-vision-cam\",\"viewer\":") +
                  (viewerConnected_ ? "true" : "false") +
                  ",\"frames_sent\":" + String(framesSent_) +
                  ",\"stream_fps\":" + String(streamFps_, 1) + "}";
    gPages->send(200, "application/json", body);
  });
  // /snapshot is registered here but answered from handle(), because only
  // handle() has a live frame. Registering a stub that says so beats a 404 that
  // looks like a broken server.
  gPages->on("/snapshot", HTTP_GET, [this]() {
    gPages->send(503, "text/plain", "no frame available yet");
  });
  gPages->onNotFound([this]() { gPages->send(404, "text/plain", "not found"); });
  gPages->begin();

  gStreamSocket = new WiFiServer(VISIONCAM_STREAM_PORT);
  gStreamSocket->begin();
  gStreamSocket->setNoDelay(true);

  running_ = true;
  framesSent_ = 0;
  fpsWindowStartMs_ = millis();
  fpsWindowFrames_ = 0;
  lastError_ = "";
  Logger::info("stream", String("serving pages on :") + String(VISIONCAM_HTTP_PORT) +
                             " and MJPEG on :" + String(VISIONCAM_STREAM_PORT));
  return true;
}

void CamStreamServer::end() {
  if (!running_) return;
  if (gViewer) gViewer.stop();
  viewerConnected_ = false;
  if (gStreamSocket != nullptr) {
    gStreamSocket->end();
    delete gStreamSocket;
    gStreamSocket = nullptr;
  }
  if (gPages != nullptr) {
    gPages->stop();
    delete gPages;
    gPages = nullptr;
  }
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  running_ = false;
  Logger::info("stream", "stopped");
}

void CamStreamServer::serveViewerPage_() {
  // The <img> must point at :81 by absolute URL, because the page is served
  // from :80 and a relative path would hit the wrong socket.
  String page = FPSTR(kViewerPage);
  page.replace("%STREAM_HOST%",
               WiFi.softAPIP().toString() + ":" + String(VISIONCAM_STREAM_PORT));
  gPages->send(200, "text/html", page);
}

void CamStreamServer::serveSnapshot_(const CrowCamera::Frame *frame) {
  if (frame == nullptr || encoder_ == nullptr) {
    gPages->send(503, "text/plain", "no frame available yet");
    return;
  }
  const size_t bytes = encoder_->encode(*frame, frame->width, frame->height, 85);
  if (bytes == 0) {
    gPages->send(500, "text/plain", "encode failed");
    return;
  }
  gPages->setContentLength(bytes);
  gPages->send(200, "image/jpeg", "");
  gPages->client().write(encoder_->data(), bytes);
}

void CamStreamServer::serviceStreamSocket_(const CrowCamera::Frame *frame) {
  // Accept a new viewer only when nobody is watching. A second client would
  // halve the first one's frame rate silently, so it is told plainly instead.
  if (!viewerConnected_) {
    WiFiClient incoming = gStreamSocket->accept();
    if (incoming) {
      // Drain the request line; the socket only serves one thing, so its
      // contents do not change what happens next.
      const uint32_t deadline = millis() + 200;
      while (incoming.connected() && millis() < deadline) {
        if (!incoming.available()) continue;
        const String line = incoming.readStringUntil('\n');
        if (line.length() <= 1) break;  // blank line ends the headers
      }
      gViewer = incoming;
      gViewer.print(F("HTTP/1.1 200 OK\r\n"
                      "Content-Type: multipart/x-mixed-replace; boundary="));
      gViewer.print(kBoundary);
      gViewer.print(F("\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n"));
      viewerConnected_ = true;
      Logger::info("stream", "viewer connected");
    }
  } else if (!gViewer.connected()) {
    gViewer.stop();
    viewerConnected_ = false;
    Logger::info("stream", "viewer disconnected");
    return;
  }

  if (!viewerConnected_ || frame == nullptr || encoder_ == nullptr) return;

  // Rate-limit independently of the camera. The viewfinder can run faster than
  // the link; pushing every frame would just build a backlog in the C6.
  const uint32_t now = millis();
  if (now - lastFrameMs_ < kStreamIntervalMs) return;

  const size_t bytes = encoder_->encode(*frame, kStreamWidth, kStreamHeight, kStreamQuality);
  if (bytes == 0) return;

  gViewer.print(F("--"));
  gViewer.print(kBoundary);
  gViewer.print(F("\r\nContent-Type: image/jpeg\r\nContent-Length: "));
  gViewer.print((uint32_t)bytes);
  gViewer.print(F("\r\n\r\n"));
  const size_t written = gViewer.write(encoder_->data(), bytes);
  gViewer.print(F("\r\n"));

  if (written != bytes) {
    // A short write means the socket buffer filled: the link cannot carry this
    // rate. Drop the viewer rather than sending a truncated frame, which would
    // desynchronise the multipart stream permanently.
    Logger::warn("stream", "short write; dropping the viewer");
    gViewer.stop();
    viewerConnected_ = false;
    return;
  }

  lastFrameMs_ = now;
  framesSent_++;
  fpsWindowFrames_++;
  const uint32_t elapsed = now - fpsWindowStartMs_;
  if (elapsed >= 1000) {
    streamFps_ = (float)fpsWindowFrames_ * 1000.0f / (float)elapsed;
    fpsWindowFrames_ = 0;
    fpsWindowStartMs_ = now;
  }
}

void CamStreamServer::handle(const CrowCamera::Frame *frame) {
  if (!running_) return;
  if (frame != nullptr) sawFrame_ = true;

  // /snapshot needs a live frame, and only this function has one. Rebinding the
  // handler each call keeps the frame pointer current without a static.
  if (gPages != nullptr) {
    gPages->on("/snapshot", HTTP_GET, [this, frame]() { serveSnapshot_(frame); });
    gPages->handleClient();
  }
  serviceStreamSocket_(frame);
}

void CamStreamServer::printStatus(Print &out) const {
  out.print(F("[stream] "));
  if (!running_) {
    out.print(F("down ("));
    out.print(lastError_);
    out.println(')');
    return;
  }
  out.print(F("ap='"));
  out.print(ssid_);
  out.print(F("' url="));
  out.print(url_);
  out.print(F(" viewer="));
  out.print(viewerConnected_ ? F("yes") : F("no"));
  out.print(F(" sent="));
  out.print(framesSent_);
  out.print(F(" fps="));
  out.println(streamFps_, 1);
  if (defaultPassword_) {
    out.println(F("[stream] WARNING: placeholder AP password; set one in config/CamSecrets.h"));
  }
  if (String(WIFI_SSID).length() > 0) {
    out.print(F("[stream] station: "));
    out.println(WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : String("joining"));
  }
}

#else  // USE_WIFI == 0

bool CamStreamServer::begin(JpegEncoder *) {
  lastError_ = "built without USE_WIFI";
  return false;
}
void CamStreamServer::end() {}
void CamStreamServer::handle(const CrowCamera::Frame *) {}

void CamStreamServer::printStatus(Print &out) const {
  out.println(F("[stream] disabled (build with -DUSE_WIFI=1)"));
}

#endif
