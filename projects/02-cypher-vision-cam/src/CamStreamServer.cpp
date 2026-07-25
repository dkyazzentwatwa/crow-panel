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
#if USE_CAM_SD
#include <SD_MMC.h>
#endif

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

// Shared stylesheet. Deliberately self-contained with no external anything: the
// panel is often its own island with no route to the internet, so a page that
// pulled in a CDN stylesheet or webfont would render as unstyled text.
//
// Palette matches shared/CrowPanelShared/DashboardWidgets.h so the browser and
// the panel read as one product rather than two.
const char kStyle[] PROGMEM =
    "<style>"
    ":root{--bg:#0b111c;--surf:#16202f;--surf2:#1e2b3d;--line:#2a3a4f;"
    "--tx:#eaf0f7;--mut:#8296ac;--acc:#16c2c9;--grn:#35d07f;--amb:#f7b733;--red:#ff5470}"
    "*{box-sizing:border-box}"
    "body{margin:0;background:var(--bg);color:var(--tx);"
    "font:15px/1.5 system-ui,-apple-system,'Segoe UI',sans-serif}"
    "header{display:flex;align-items:center;gap:12px;padding:14px 18px;"
    "border-bottom:1px solid var(--line);position:sticky;top:0;background:var(--bg);z-index:9}"
    "header h1{font-size:.82rem;font-weight:600;letter-spacing:.14em;margin:0;"
    "text-transform:uppercase;color:var(--mut);flex:1}"
    "nav a{color:var(--mut);text-decoration:none;font-size:.78rem;letter-spacing:.08em;"
    "text-transform:uppercase;padding:7px 12px;border-radius:8px;margin-left:4px}"
    "nav a:hover{background:var(--surf)}"
    "nav a.on{color:var(--bg);background:var(--acc);font-weight:600}"
    "main{padding:18px;max-width:1100px;margin:0 auto}"
    ".dot{width:8px;height:8px;border-radius:50%;display:inline-block;background:var(--mut)}"
    ".dot.live{background:var(--grn)}.dot.rec{background:var(--red)}"
    ".feed{background:#000;border:1px solid var(--line);border-radius:14px;"
    "overflow:hidden;line-height:0}"
    ".feed img{width:100%;height:auto;display:block}"
    ".bar{display:flex;gap:10px;flex-wrap:wrap;align-items:center;margin-top:14px}"
    "button{font:inherit;font-weight:600;border:0;border-radius:10px;padding:12px 20px;"
    "background:var(--acc);color:#04222a;cursor:pointer}"
    "button:hover{filter:brightness(1.12)}"
    "button.ghost{background:var(--surf2);color:var(--tx)}"
    "button.rec{background:var(--red);color:#2a0009}"
    "button:disabled{opacity:.45;cursor:not-allowed}"
    ".stat{margin-left:auto;color:var(--mut);font-size:.82rem;font-variant-numeric:tabular-nums}"
    ".grid{display:grid;gap:12px;margin-top:16px;"
    "grid-template-columns:repeat(auto-fill,minmax(200px,1fr))}"
    ".card{background:var(--surf);border:1px solid var(--line);border-radius:12px;"
    "overflow:hidden}"
    ".card img{width:100%;aspect-ratio:1024/600;object-fit:cover;display:block;"
    "background:var(--surf2)}"
    ".card .meta{display:flex;justify-content:space-between;align-items:center;"
    "padding:9px 11px;font-size:.76rem;color:var(--mut)}"
    ".card .meta b{color:var(--tx);font-weight:600}"
    ".vid{display:flex;flex-direction:column;justify-content:center;align-items:center;"
    "aspect-ratio:1024/600;background:var(--surf2);color:var(--mut);gap:6px}"
    ".vid span{font-size:1.6rem}"
    "a.dl{color:var(--acc);text-decoration:none;font-weight:600}"
    "a.dl:hover{text-decoration:underline}"
    ".note{color:var(--mut);font-size:.8rem;margin-top:18px;padding-top:14px;"
    "border-top:1px solid var(--line)}"
    ".warn{color:var(--amb)}"
    ".empty{text-align:center;color:var(--mut);padding:60px 20px}"
    "</style>";

// Nav is shared between pages; `%A%` / `%B%` mark the active tab.
const char kNav[] PROGMEM =
    "<header><span class='dot %DOT%'></span><h1>Cypher Vision Cam</h1>"
    "<nav><a class='%A%' href='/'>Live</a>"
    "<a class='%B%' href='/gallery'>Gallery</a></nav></header>";

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
  gPages->on("/gallery", HTTP_GET, [this]() { serveGalleryPage_(); });
  gPages->on("/media", HTTP_GET, [this]() { serveMedia_(); });

  // Control endpoints. Both only raise a flag - the sketch performs the action
  // in loop(), which is the one place that holds a live camera frame. A second
  // path owning a buffer would break the acquire/release contract that keeps
  // the capture pipeline from stalling.
  gPages->on("/shutter", HTTP_POST, [this]() {
    if (onShutter_) onShutter_();
    gPages->send(200, "application/json", "{\"ok\":true}");
  });
  gPages->on("/record", HTTP_POST, [this]() {
    if (onRecordToggle_) onRecordToggle_();
    gPages->send(200, "application/json", "{\"ok\":true}");
  });

  gPages->on("/health", HTTP_GET, [this]() {
    const uint32_t freeMb =
        recorder_ != nullptr ? (uint32_t)(recorder_->freeBytes() / (1024ULL * 1024ULL)) : 0;
    const uint8_t files = recorder_ != nullptr ? recorder_->mediaCount() : 0;
    String body = String("{\"ok\":true,\"service\":\"cypher-vision-cam\",\"viewer\":") +
                  (viewerConnected_ ? "true" : "false") +
                  ",\"recording\":" + (recordingHint_ ? "true" : "false") +
                  ",\"frames_sent\":" + String(framesSent_) +
                  ",\"fps\":" + String(streamFps_, 1) +
                  ",\"files\":" + String(files) +
                  ",\"free_mb\":" + String(freeMb) + "}";
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

// The host the client actually used to reach us.
//
// This MUST come from the request, not from softAPIP(). The panel can be
// reachable at two addresses at once - 192.168.4.1 on its own AP and a LAN
// address as a station - and baking in the AP address breaks every LAN viewer:
// the page loads fine over the LAN, then points the browser at an AP address it
// has no route to, so the frame never appears while /snapshot (a relative path)
// works perfectly. Echoing back the Host header keeps the stream on whichever
// interface the client actually used.
String CamStreamServer::requestHost_() const {
  String host = gPages->hostHeader();
  const int colon = host.indexOf(':');
  if (colon >= 0) host = host.substring(0, colon);  // drop any :port
  if (host.length() == 0) {
    // No Host header (HTTP/1.0). Prefer the station address, since that is the
    // interface a client is most likely to have reached us on.
    host = stationConnected() ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
  }
  return host;
}

String CamStreamServer::chrome_(bool onGallery) const {
  String nav = FPSTR(kNav);
  nav.replace("%DOT%", recordingHint_ ? "rec" : "live");
  nav.replace("%A%", onGallery ? "" : "on");
  nav.replace("%B%", onGallery ? "on" : "");
  return nav;
}

void CamStreamServer::serveViewerPage_() {
  const String stream =
      "http://" + requestHost_() + ":" + String(VISIONCAM_STREAM_PORT) + "/stream";

  String page = "<!doctype html><html><head><meta charset=utf-8>"
                "<meta name=viewport content='width=device-width,initial-scale=1'>"
                "<title>Cypher Vision Cam</title>";
  page += FPSTR(kStyle);
  page += "</head><body>";
  page += chrome_(false);
  page += "<main>";
  page += "<div class=feed><img src='" + stream + "' alt='live feed'></div>";

  page += "<div class=bar>"
          "<button onclick=shot()>Take photo</button>"
          "<button id=rec class='" +
          String(recordingHint_ ? "rec" : "ghost") + "' onclick=rec_()>" +
          String(recordingHint_ ? "Stop recording" : "Record") + "</button>"
          "<a class=dl href='/snapshot' download><button class=ghost>Save snapshot</button></a>"
          "<span class=stat id=stat>&nbsp;</span></div>";

  // Status polls /health rather than being baked into the page, so the numbers
  // stay live without a reload. Kept deliberately small - this is a camera, not
  // a dashboard, and every byte here crosses the same link as the video.
  page += "<p class=note>Anyone who can reach this page can make the camera "
          "take pictures. There is no password on these controls.</p>";
  page += "</main><script>"
          "async function post(u){try{await fetch(u,{method:'POST'})}catch(e){}}"
          "function shot(){post('/shutter')}"
          "async function rec_(){await post('/record');setTimeout(stat_,300)}"
          "async function stat_(){try{const r=await fetch('/health');const j=await r.json();"
          "document.getElementById('stat').textContent="
          "j.fps.toFixed(0)+' fps  |  '+j.files+' files  |  '+j.free_mb+' MB free';"
          "const b=document.getElementById('rec');"
          "b.textContent=j.recording?'Stop recording':'Record';"
          "b.className=j.recording?'rec':'ghost';"
          "document.querySelector('.dot').className='dot '+(j.recording?'rec':'live')"
          "}catch(e){}}"
          "stat_();setInterval(stat_,2000);"
          "</script></body></html>";

  gPages->send(200, "text/html", page);
}

void CamStreamServer::serveGalleryPage_() {
  if (recorder_ == nullptr) {
    gPages->send(503, "text/plain", "no recorder");
    return;
  }
  recorder_->refreshMediaList();
  const uint8_t count = recorder_->mediaCount();

  String page = "<!doctype html><html><head><meta charset=utf-8>"
                "<meta name=viewport content='width=device-width,initial-scale=1'>"
                "<title>Gallery - Cypher Vision Cam</title>";
  page += FPSTR(kStyle);
  page += "</head><body>";
  page += chrome_(true);
  page += "<main>";

  if (!recorder_->storageReady()) {
    page += "<div class=empty><h2>No card</h2><p>Insert an SD card to store and "
            "browse captures.</p></div>";
  } else if (count == 0) {
    page += "<div class=empty><h2>Nothing yet</h2><p>Take a photo from the Live "
            "page or press the panel's shutter.</p></div>";
  } else {
    page += "<div class=grid>";
    for (uint8_t i = 0; i < count; i++) {
      const CamRecorder::MediaEntry &e = recorder_->mediaAt(i);
      const String name = String(e.name);
      const String href = "/media?f=" + name;

      char size[24];
      if (e.bytes >= 1024UL * 1024UL) {
        snprintf(size, sizeof(size), "%.1f MB", (double)e.bytes / (1024.0 * 1024.0));
      } else {
        snprintf(size, sizeof(size), "%lu KB", (unsigned long)(e.bytes / 1024UL));
      }

      page += "<div class=card>";
      if (e.isVideo) {
        // No poster frame for clips: producing one would mean decoding the
        // first JPEG out of the AVI on the panel, and a placeholder is honest
        // about the fact that nothing was rendered.
        page += "<div class=vid><span>&#9654;</span>video</div>";
      } else {
        // loading=lazy is load-bearing, not decoration. There are no stored
        // thumbnails, so each tile is the full ~200 KB JPEG; without lazy
        // loading a card of 60 stills would try to pull ~12 MB at once over a
        // link that also carries the live video.
        page += "<a href='" + href + "'><img loading=lazy src='" + href + "' alt='" +
                name + "'></a>";
      }
      page += "<div class=meta><b>" + name + "</b><span>" + size + "</span></div>";
      page += "<div class=meta><a class=dl href='" + href +
              "' download>Download</a><span>" + (e.isVideo ? "AVI" : "JPEG") +
              "</span></div>";
      page += "</div>";
    }
    page += "</div>";

    page += "<p class=note>";
    if (recorder_->mediaTruncated()) {
      page += "<span class=warn>Showing the first " + String(CamRecorder::kMaxListed) +
              " files only - the card holds more.</span><br>";
    }
    page += String(count) + " file" + (count == 1 ? "" : "s") + " &middot; " +
            String((uint32_t)(recorder_->freeBytes() / (1024ULL * 1024ULL))) +
            " MB free<br>Downloading a video pauses the panel until the transfer "
            "finishes - the screen will say so.</p>";
  }

  page += "</main></body></html>";
  gPages->send(200, "text/html", page);
}

// Only ever serve names this device itself created: CAM_NNNNN.JPG or
// VID_NNNNN.AVI, nothing else.
//
// This is a whitelist rather than a blacklist on purpose. `/media?f=` takes a
// name straight from the network and turns it into a path on the card, so
// checking for ".." and slashes would be the start of an arms race. Requiring
// the exact shape this recorder produces leaves no room to negotiate.
bool CamStreamServer::validMediaName_(const String &name, bool &isVideo) {
  if (name.length() != 13) return false;  // PREFIX(4) + 5 digits + ".EXT"(4)

  const bool jpg = name.startsWith("CAM_") && name.endsWith(".JPG");
  const bool avi = name.startsWith("VID_") && name.endsWith(".AVI");
  if (!jpg && !avi) return false;

  for (uint8_t i = 4; i < 9; i++) {
    if (!isdigit((int)name[i])) return false;
  }
  isVideo = avi;
  return true;
}

void CamStreamServer::serveMedia_() {
#if USE_CAM_SD
  const String name = gPages->arg("f");
  bool isVideo = false;
  if (!validMediaName_(name, isVideo)) {
    gPages->send(400, "text/plain", "bad file name");
    return;
  }

  const String path = String("/DCIM/") + name;
  File file = SD_MMC.open(path, FILE_READ);
  if (!file || file.isDirectory()) {
    if (file) file.close();
    gPages->send(404, "text/plain", "not found");
    return;
  }

  // Announce the transfer BEFORE it starts. streamFile() runs to completion
  // inside handleClient(), so from here until it returns the render loop is
  // stopped: no video, no touch. For a still that is a blink; for a 30 MB clip
  // it is most of a minute, and a panel that freezes without explanation reads
  // as a crash. The MJPEG pusher is dropped for the same reason - it cannot run
  // anyway, and releasing the viewer frees the link for the download.
  serving_ = true;
  servingName_ = name;
  if (viewerConnected_) {
    gViewer.stop();
    viewerConnected_ = false;
  }

  // Inline for stills so the gallery grid can render them; attachment for clips
  // because browsers largely cannot play AVI/MJPEG in a <video> tag, and a tab
  // that opens and does nothing is worse than a download.
  if (isVideo) {
    gPages->sendHeader("Content-Disposition", "attachment; filename=" + name);
  }
  gPages->streamFile(file, isVideo ? "video/x-msvideo" : "image/jpeg");
  file.close();

  serving_ = false;
  servingName_ = "";
#else
  gPages->send(503, "text/plain", "built without SD support");
#endif
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
