#include "AcidGlassRemote.h"

#include "AcidGlassAudio.h"
#include "AcidGlassVisuals.h"
#include <CrowPanelShared.h>
#include <string.h>

#if USE_ACID_GLASS_REMOTE && USE_WIFI && defined(CONFIG_IDF_TARGET_ESP32P4)
#include <WebServer.h>
#include <WiFi.h>
#endif

namespace {

#if USE_ACID_GLASS_REMOTE && USE_WIFI && defined(CONFIG_IDF_TARGET_ESP32P4)
const char kRemotePage[] PROGMEM = R"HTML(
<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Acid Glass</title><style>
:root{--bg:#08030f;--card:#171025;--line:#3a2453;--ink:#f7edff;--mut:#a58fb8;--hot:#ff38c7;--acid:#77ff41}
*{box-sizing:border-box}body{margin:0;background:radial-gradient(circle at top,#27103b,var(--bg) 48%);color:var(--ink);font:15px system-ui,sans-serif}
header{padding:22px 18px 13px;position:sticky;top:0;background:#08030fe8;backdrop-filter:blur(10px);z-index:3;border-bottom:1px solid var(--line)}
h1{margin:0;font-size:1.05rem;letter-spacing:.22em;text-transform:uppercase}header p{margin:6px 0 0;color:var(--mut);font-size:.78rem}
main{max-width:780px;margin:auto;padding:16px;display:grid;gap:14px}.card{background:var(--card);border:1px solid var(--line);border-radius:16px;padding:15px}
h2{font-size:.72rem;letter-spacing:.16em;text-transform:uppercase;color:var(--mut);margin:0 0 12px}.grid{display:grid;grid-template-columns:repeat(2,1fr);gap:10px}
button,select,input{width:100%;font:inherit}button,select{border:1px solid var(--line);background:#241535;color:var(--ink);border-radius:11px;padding:12px}
button.hot{background:linear-gradient(90deg,var(--hot),#9b40ff);color:#130017;font-weight:800}button.acid{background:var(--acid);color:#0a1b00;font-weight:800}
label{display:grid;grid-template-columns:90px 1fr 36px;align-items:center;gap:8px;color:var(--mut);font-size:.8rem;margin:9px 0}
input{accent-color:var(--hot)}.status{font:12px ui-monospace,monospace;color:var(--acid);white-space:pre-wrap}.warn{color:#ffd166}.presets{grid-template-columns:repeat(4,1fr)}
</style></head><body><header><h1>Acid Glass</h1><p>OPEN CONTROL NETWORK · anyone nearby can operate this panel</p></header><main>
<section class=card><h2>Performance</h2><div class=grid><select id=scene></select><select id=palette></select><select id=quality><option value=0>Adaptive 30/45</option><option value=1>Performance 30</option><option value=2>Quality 45</option></select><button class=hot onclick="act('randomize')">Randomize</button><button onclick="act('demo','',1)">Demo</button><button onclick="act('previous')">Previous</button><button onclick="act('next')">Next</button></div></section>
<section class=card><h2>Visual Modifiers</h2><div id=sliders></div></section>
<section class=card><h2>Music</h2><div class=grid><button class=acid onclick="act('play')">Play</button><button onclick="act('stop')">Stop</button><button onclick="act('trackprev')">Track −</button><button onclick="act('tracknext')">Track +</button></div><label>Volume<input id=volume type=range min=0 max=100><b id=volumeV></b></label></section>
<section class=card><h2>Presets</h2><div class="grid presets" id=presets></div></section>
<section class=card><h2>Live Proof</h2><div class=status id=status>connecting…</div></section>
</main><script>
const keys=['speed','zoom','intensity','warp','feedback','trails','symmetry','hue','sensitivity','macrox','macroy'];
const names=['Speed','Zoom','Intensity','Warp','Feedback','Trails','Symmetry','Hue','Audio','Macro X','Macro Y'];
const sl=document.getElementById('sliders');keys.forEach((k,i)=>{sl.insertAdjacentHTML('beforeend',`<label>${names[i]}<input id=${k} type=range min=0 max=255><b id=${k}V></b></label>`);document.getElementById(k).onchange=e=>act('set',k,e.target.value)});
const presetNames=['Violet Code','Toxic Rain','Molten Core','Lime Mirror','Ocean Blobs','Solar Cells','Candy Chaos','Red Signal','Ice Melt','Star Dust','White Scope','RGB Bloom','Laser Temple','Deep Dream','Neon Heart','Installation'];
const ps=document.getElementById('presets');presetNames.forEach((n,i)=>ps.insertAdjacentHTML('beforeend',`<button onclick="preset(${i})">${i+1}<br><small>${n}</small></button>`));
async function post(url,data){await fetch(url,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(data)})}
function act(action,key='',value=0){return post('/api/control',{action,key,value})}function preset(slot){return post('/api/preset',{action:'load',value:slot})}
document.getElementById('scene').onchange=e=>act('scene','',e.target.value);document.getElementById('palette').onchange=e=>act('palette','',e.target.value);document.getElementById('quality').onchange=e=>act('quality','',e.target.value);document.getElementById('volume').onchange=e=>act('volume','',e.target.value);
async function poll(){try{const j=await(await fetch('/api/state')).json();let s=document.getElementById('scene'),p=document.getElementById('palette');if(!s.options.length){j.scenes.forEach((n,i)=>s.add(new Option(n,i)));j.palettes.forEach((n,i)=>p.add(new Option(n,i)))}s.value=j.scene;p.value=j.palette;document.getElementById('quality').value=j.quality;keys.forEach(k=>{document.getElementById(k).value=j[k];document.getElementById(k+'V').textContent=j[k]});document.getElementById('volume').value=j.volume;document.getElementById('volumeV').textContent=j.volume;document.getElementById('status').textContent=`${j.fps}/${j.target_fps} fps · ${j.frame_us} µs · present ${j.present_us} µs\nPPA ${j.ppa?'ON':j.ppa_requested?'ERR':'CPU'} · Q${j.quality_tier+1} · drops ${j.dropped_frames}\n${j.track} · ${j.playing?'PLAYING':'STOPPED'} · underruns ${j.underruns}`}catch(e){document.getElementById('status').textContent='panel unavailable'}}poll();setInterval(poll,1000);
</script></body></html>)HTML";

String jsonEscape(const char *value) {
  String escaped(value != nullptr ? value : "");
  escaped.replace("\\", "\\\\");
  escaped.replace("\"", "\\\"");
  escaped.replace("\r", " ");
  escaped.replace("\n", " ");
  return escaped;
}
#endif

}  // namespace

bool AcidGlassRemote::begin(void *context, ControlHandler handler, const AcidGlassState *state,
                            const AcidGlassAudio *audio, const AcidGlassVisuals *visuals) {
  context_ = context;
  handler_ = handler;
  state_ = state;
  audio_ = audio;
  visuals_ = visuals;
#if USE_ACID_GLASS_REMOTE && USE_WIFI && defined(CONFIG_IDF_TARGET_ESP32P4)
  if (!configureCrowPanelHostedWiFiPins("acid-glass")) return false;
  WiFi.mode(WIFI_AP);
  uint8_t mac[6] = {};
  WiFi.macAddress(mac);
  snprintf(ssid_, sizeof(ssid_), "%s-%02X%02X", ACID_GLASS_AP_PREFIX, mac[4], mac[5]);
  if (!WiFi.softAP(ssid_, nullptr, 1, 0, 4)) return false;
  delay(300);
  snprintf(url_, sizeof(url_), "http://%s/", WiFi.softAPIP().toString().c_str());

  WebServer *server = new WebServer(ACID_GLASS_HTTP_PORT);
  server_ = server;
  server->on("/", HTTP_GET, [this]() { servePage_(); });
  server->on("/api/health", HTTP_GET, [this]() { serveHealth_(); });
  server->on("/api/state", HTTP_GET, [this]() { serveState_(); });
  server->on("/api/control", HTTP_POST, [this]() { handleControl_(false); });
  server->on("/api/preset", HTTP_POST, [this]() { handleControl_(true); });
  server->onNotFound([server]() { server->send(404, "application/json", "{\"ok\":false,\"error\":\"not found\"}"); });
  server->begin();
  ready_ = true;
  Logger::warn("acid-glass", String("OPEN control AP '") + ssid_ + "' at " + url_);
  return true;
#else
  return false;
#endif
}

void AcidGlassRemote::tick() {
#if USE_ACID_GLASS_REMOTE && USE_WIFI && defined(CONFIG_IDF_TARGET_ESP32P4)
  if (server_ != nullptr) static_cast<WebServer *>(server_)->handleClient();
#endif
}

void AcidGlassRemote::end() {
#if USE_ACID_GLASS_REMOTE && USE_WIFI && defined(CONFIG_IDF_TARGET_ESP32P4)
  if (server_ != nullptr) {
    WebServer *server = static_cast<WebServer *>(server_);
    server->stop();
    delete server;
    server_ = nullptr;
  }
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
#endif
  ready_ = false;
}

uint8_t AcidGlassRemote::clients() const {
#if USE_ACID_GLASS_REMOTE && USE_WIFI && defined(CONFIG_IDF_TARGET_ESP32P4)
  return ready_ ? static_cast<uint8_t>(WiFi.softAPgetStationNum()) : 0;
#else
  return 0;
#endif
}

void AcidGlassRemote::servePage_() {
#if USE_ACID_GLASS_REMOTE && USE_WIFI && defined(CONFIG_IDF_TARGET_ESP32P4)
  static_cast<WebServer *>(server_)->send_P(200, "text/html", kRemotePage);
#endif
}

void AcidGlassRemote::serveHealth_() {
#if USE_ACID_GLASS_REMOTE && USE_WIFI && defined(CONFIG_IDF_TARGET_ESP32P4)
  String body = String("{\"ok\":true,\"service\":\"acid-glass\",\"clients\":") + clients() +
                ",\"display\":" + (visuals_ != nullptr ? "true" : "false") +
                ",\"audio\":" + (audio_ != nullptr && audio_->audioReady() ? "true" : "false") +
                ",\"proof\":\"runtime-status-only\"}";
  static_cast<WebServer *>(server_)->send(200, "application/json", body);
#endif
}

void AcidGlassRemote::serveState_() {
#if USE_ACID_GLASS_REMOTE && USE_WIFI && defined(CONFIG_IDF_TARGET_ESP32P4)
  if (state_ == nullptr || visuals_ == nullptr || audio_ == nullptr) {
    static_cast<WebServer *>(server_)->send(503, "application/json", "{\"ok\":false}");
    return;
  }
  String body;
  body.reserve(1200);
  body = "{\"ok\":true,\"scene\":" + String(state_->scene) +
         ",\"scene_name\":\"" + AcidGlassVisuals::sceneName(state_->scene) +
         "\",\"palette\":" + String(state_->palette) + ",\"palette_name\":\"" +
         AcidGlassVisuals::paletteName(state_->palette) + "\",\"volume\":" + state_->volume +
         ",\"speed\":" + state_->visual.speed + ",\"zoom\":" + state_->visual.zoom +
         ",\"intensity\":" + state_->visual.intensity + ",\"warp\":" + state_->visual.warp +
         ",\"feedback\":" + state_->visual.feedback + ",\"trails\":" + state_->visual.trails +
         ",\"symmetry\":" + state_->visual.symmetry + ",\"hue\":" + state_->visual.hueRate +
         ",\"sensitivity\":" + state_->visual.audioSensitivity +
         ",\"macrox\":" + state_->visual.macroX + ",\"macroy\":" + state_->visual.macroY +
         ",\"quality\":" + static_cast<uint8_t>(state_->qualityMode) +
         ",\"fps\":" + visuals_->fps() + ",\"target_fps\":" + visuals_->targetFps() +
         ",\"frame_us\":" + visuals_->lastFrameUs() + ",\"present_us\":" + visuals_->lastPresentUs() +
         ",\"quality_tier\":" + visuals_->qualityTier() + ",\"dropped_frames\":" + visuals_->droppedFrames() +
         ",\"ui_dirty\":" + (visuals_->uiDirty() ? "true" : "false") +
         ",\"ppa\":" + (visuals_->ppaReady() ? "true" : "false") +
         ",\"ppa_requested\":" + (visuals_->ppaRequested() ? "true" : "false") +
         ",\"playing\":" + (audio_->playing() ? "true" : "false") +
         ",\"track\":\"" + jsonEscape(audio_->trackName(audio_->activeTrack())) +
         "\",\"underruns\":" + audio_->underruns() + ",\"scenes\":[";
  for (uint8_t i = 0; i < kAcidSceneCount; ++i) {
    if (i) body += ',';
    body += "\"" + String(AcidGlassVisuals::sceneName(i)) + "\"";
  }
  body += "],\"palettes\":[";
  for (uint8_t i = 0; i < kAcidPaletteCount; ++i) {
    if (i) body += ',';
    body += "\"" + String(AcidGlassVisuals::paletteName(i)) + "\"";
  }
  body += "]}";
  static_cast<WebServer *>(server_)->send(200, "application/json", body);
#endif
}

void AcidGlassRemote::handleControl_(bool presetRoute) {
#if USE_ACID_GLASS_REMOTE && USE_WIFI && defined(CONFIG_IDF_TARGET_ESP32P4)
  WebServer *server = static_cast<WebServer *>(server_);
  String action = server->arg("action");
  String key = server->arg("key");
  if (presetRoute && action.length() == 0) action = "preset";
  if (action.length() == 0 || action.length() >= 16 || key.length() >= 20 || !server->hasArg("value")) {
    server->send(400, "application/json", "{\"ok\":false,\"error\":\"bad control\"}");
    return;
  }
  ControlEvent event;
  event.source = ControlSource::kRemote;
  strlcpy(event.action, presetRoute ? "preset" : action.c_str(), sizeof(event.action));
  strlcpy(event.key, presetRoute ? action.c_str() : key.c_str(), sizeof(event.key));
  event.value = server->arg("value").toInt();
  bool accepted = handler_ != nullptr && handler_(context_, event);
  server->send(accepted ? 200 : 400, "application/json",
               accepted ? "{\"ok\":true}" : "{\"ok\":false,\"error\":\"rejected\"}");
#endif
}

void AcidGlassRemote::printStatus(Print &out) const {
  out.print(F("[remote] ready="));
  out.print(ready_ ? F("yes") : F("no"));
  out.print(F(" ssid="));
  out.print(ssid_[0] ? ssid_ : "disabled");
  out.print(F(" url="));
  out.print(url_[0] ? url_ : "none");
  out.print(F(" clients="));
  out.println(clients());
}
