// Soft-AP + MJPEG live feed served from the onboard ESP32-C6.
//
// HARDWARE-VERIFIED (V1.2 panel, 2026-07-24) in STATION mode: a browser on the
// LAN watched the live feed at 15-20 fps (640x480 q60) while the panel's own
// viewfinder held ~21 fps.
//
// THE LINK IS BANDWIDTH-BOUND, NOT CPU-BOUND. Frame rate drops in bright
// scenes: a high-contrast image compresses worse, so each frame costs more
// bytes over the SDIO link to the C6. To raise the rate, lower kStreamQuality
// or kStreamWidth/Height - throwing CPU at it would change nothing.
//
// Soft-AP mode is still NOT verified. It advertises correctly and softAP()
// reports success, but no association has been observed on this board. Station
// mode is the proven path and the one to prefer.

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

  // Pure AP unless station credentials actually exist.
  //
  // The first hardware attempt used WIFI_AP_STA unconditionally, and no client
  // could associate (softAP() returned success, the SSID was advertised, but
  // softAPgetStationNum() stayed at 0). AP_STA makes the radio time-share
  // between hosting and scanning for its station network, and over the hosted
  // C6's SDIO link that is a far less travelled path than plain AP. With no
  // station SSID configured there is nothing to gain from it anyway.
  const bool wantStation = String(WIFI_SSID).length() > 0;
  WiFi.mode(wantStation ? WIFI_AP_STA : WIFI_AP);

  // Explicit channel and connection limit rather than the defaults. Channel 1
  // is the safest single choice; leaving it implicit lets the stack pick,
  // which on a hosted radio can land somewhere a client will not follow.
  constexpr int kApChannel = 1;
  constexpr int kApHidden = 0;
  constexpr int kApMaxClients = 4;
  if (!WiFi.softAP(ssid.c_str(), password.c_str(), kApChannel, kApHidden,
                   kApMaxClients)) {
    lastError_ = "soft-AP would not start";
    Logger::warn("stream", lastError_);
    return false;
  }

  // Give the C6 a moment to actually bring the interface up before reading its
  // address. softAP() returning true only means the request was accepted.
  delay(300);

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
  // Registered ONCE. WebServer::on() appends to a handler list rather than
  // replacing, so registering per-loop to capture a fresh frame pointer grows
  // that list without bound - the route reads currentFrame_ instead.
  gPages->on("/snapshot", HTTP_GET, [this]() { serveSnapshot_(currentFrame_); });
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
  // The <img> needs an absolute URL because the stream lives on a different
  // port, and a relative path would hit :80.
  //
  // The host MUST come from the request, not from softAPIP(). The panel can be
  // reachable at two addresses at once - 192.168.4.1 on its own AP and a LAN
  // address as a station - and baking in the AP address breaks every LAN
  // viewer: the page loads fine over the LAN, then points the browser at an AP
  // address it has no route to, so the frame never appears while /snapshot
  // (a relative path) works perfectly. Echoing back the Host header keeps the
  // stream on whichever interface the client actually used.
  String host = gPages->hostHeader();
  const int colon = host.indexOf(':');
  if (colon >= 0) host = host.substring(0, colon);  // drop any :port
  if (host.length() == 0) {
    // No Host header (HTTP/1.0). Prefer the station address, since that is the
    // interface a client is most likely to have reached us on.
    host = stationConnected() ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
  }

  String page = FPSTR(kViewerPage);
  page.replace("%STREAM_HOST%", host + ":" + String(VISIONCAM_STREAM_PORT));
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
      // Drain the request headers; the socket only serves one thing, so their
      // contents do not change what happens next. delay(1) rather than a bare
      // spin - this runs inside the render loop, and busy-waiting here would
      // stall video and touch for up to the full timeout.
      const uint32_t deadline = millis() + 200;
      while (incoming.connected() && millis() < deadline) {
        if (!incoming.available()) {
          delay(1);
          continue;
        }
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

  // Publish the frame for the /snapshot route, then clear it again below: the
  // pointer is only valid while this call is on the stack, and handleClient()
  // is the only thing that can invoke the route.
  currentFrame_ = frame;
  if (gPages != nullptr) gPages->handleClient();
  serviceStreamSocket_(frame);
  currentFrame_ = nullptr;
}

uint8_t CamStreamServer::stationCount() const {
  if (!running_) return 0;
  return (uint8_t)WiFi.softAPgetStationNum();
}

bool CamStreamServer::stationConnected() const {
  return running_ && WiFi.status() == WL_CONNECTED;
}

String CamStreamServer::stationUrl() const {
  if (!stationConnected()) return String();
  return String("http://") + WiFi.localIP().toString() + "/";
}

void CamStreamServer::printStatus(Print &out) const {
  out.print(F("[stream] "));
  if (!running_) {
    out.print(F("down ("));
    out.print(lastError_);
    out.println(')');
    return;
  }
  out.print(F("stations="));
  out.print(stationCount());
  out.print(F(" ap='"));
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
uint8_t CamStreamServer::stationCount() const { return 0; }
bool CamStreamServer::stationConnected() const { return false; }
String CamStreamServer::stationUrl() const { return String(); }

void CamStreamServer::printStatus(Print &out) const {
  out.println(F("[stream] disabled (build with -DUSE_WIFI=1)"));
}

#endif
