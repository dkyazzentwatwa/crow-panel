#include "VisionGuardUi.h"

const char *visionScreenName(VisionScreen s) {
  switch (s) {
    case SCR_LIVE:    return "live";
    case SCR_SCAN:    return "scan";
    case SCR_CHECKS:  return "checks";
    case SCR_RESULT:  return "result";
    case SCR_HISTORY: return "history";
    default:          return "?";
  }
}

// ===========================================================================
// Display path: full touch dashboard drawn with the shared Widgets toolkit.
// ===========================================================================
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
#include <Arduino_GFX_Library.h>
#include <DashboardWidgets.h>

using namespace Widgets;

namespace {
constexpr int16_t kW = 1024;

struct Rect { int16_t x, y, w, h; };
bool inR(const Rect &r, int16_t x, int16_t y) {
  return hitRect(x, y, r.x, r.y, r.w, r.h);
}

const char *const kTabs[SCR_COUNT] = {"LIVE", "SCAN", "CHECKS", "RESULT", "HISTORY"};

// --- Live screen ---
const Rect kLiveFrame   = {16, 88, 616, 372};
const Rect kLiveCamCard = {648, 88, 360, 176};
const Rect kLiveCapCard = {648, 276, 360, 184};
const Rect kLiveCapBtn  = {664, 388, 328, 56};

// --- Scan screen ---
const Rect kScanCard = {212, 96, 600, 292};
const Rect kScanBtn  = {312, 404, 400, 60};

// --- Checks screen ---
const Rect kChkProgress = {16, 120, 640, 18};
Rect chkRow(uint8_t i) { return {16, (int16_t)(150 + i * 52), 664, 46}; }
const Rect kChkEvalBtn = {696, 150, 312, 56};
const Rect kChkViewBtn = {696, 214, 312, 56};
const Rect kChkSummary = {696, 286, 312, 226};

// --- Result screen ---
const Rect kResHero   = {16, 88, 430, 196};
const Rect kResCounts = {16, 300, 430, 92};
const Rect kResRescan = {16, 404, 430, 56};
const Rect kResNote   = {462, 88, 546, 150};
const Rect kResItems  = {462, 250, 546, 210};

// --- History screen ---
constexpr uint8_t kHistPageSize = 7;
Rect histRow(uint8_t i) { return {16, (int16_t)(96 + i * 54), 992, 48}; }
const Rect kHistPrev = {16, 484, 200, 46};
const Rect kHistNext = {808, 484, 200, 46};

uint16_t stateColor(ItemState s) {
  switch (s) {
    case ITEM_PASS: return kGreen;
    case ITEM_FAIL: return kRed;
    case ITEM_SKIP: return kAmber;
    default:        return kTextMut;
  }
}

// Shorten `s` to fit `maxW` px in `font`, appending "..." when clipped.
String fitText(Arduino_GFX *g, const String &s, const GFXfont *font, int16_t maxW) {
  if (textWidth(g, s.c_str(), font) <= maxW) return s;
  String out = s;
  while (out.length() > 1 && textWidth(g, (out + "...").c_str(), font) > maxW) {
    out.remove(out.length() - 1);
  }
  return out + "...";
}

// Greedy word wrap into up to maxLines lines, last line clipped with "...".
uint8_t wrapText(Arduino_GFX *g, const String &s, const GFXfont *font, int16_t maxW,
                 String out[], uint8_t maxLines) {
  uint8_t n = 0;
  String line;
  int start = 0;
  bool overflow = false;
  while (n < maxLines) {
    int sp = s.indexOf(' ', start);
    String word = (sp < 0) ? s.substring(start) : s.substring(start, sp);
    String trial = line.length() ? line + " " + word : word;
    if (textWidth(g, trial.c_str(), font) <= maxW || line.length() == 0) {
      line = trial;
    } else {
      out[n++] = line;
      line = word;
    }
    if (sp < 0) { start = s.length() + 1; break; }
    start = sp + 1;
  }
  if (n < maxLines && line.length()) out[n++] = line;
  else if (start <= (int)s.length()) overflow = true;
  if (overflow && n > 0) {
    out[n - 1] = fitText(g, out[n - 1] + " ...", font, maxW);
  }
  return n;
}

void drawFrameCorners(Arduino_GFX *g, const Rect &r, uint16_t color) {
  const int16_t L = 26, inset = 12;
  int16_t x0 = r.x + inset, y0 = r.y + inset;
  int16_t x1 = r.x + r.w - inset, y1 = r.y + r.h - inset;
  g->drawFastHLine(x0, y0, L, color);       g->drawFastVLine(x0, y0, L, color);
  g->drawFastHLine(x1 - L, y0, L, color);   g->drawFastVLine(x1, y0, L, color);
  g->drawFastHLine(x0, y1, L, color);       g->drawFastVLine(x0, y1 - L, L, color);
  g->drawFastHLine(x1 - L, y1, L, color);   g->drawFastVLine(x1, y1 - L, L, color);
}

uint32_t hash32(const char *s) {
  uint32_t h = 2166136261u;
  while (*s) { h ^= (uint8_t)*s++; h *= 16777619u; }
  return h;
}
}  // namespace
#endif  // USE_DISPLAY && CONFIG_IDF_TARGET_ESP32P4

// ===========================================================================
// Unconditional API (compiles and runs headless over Serial too).
// ===========================================================================
void VisionGuardUi::begin(InspectionWorkflow *workflow) {
  wf_ = workflow;
  const UiTheme &theme = defaultUiTheme();
  Logger::info("ui", String("theme=") + theme.name +
                         " screens=Live,Scan,Checklist,Result,History");
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  ready_ = CrowDisplay::begin(activeHardwareProfile(), "Vision Guard Inspection Kiosk",
                              /*manualFlush=*/true) &&
           CrowDisplay::canvas() != nullptr;
  dirty_ = true;
  Logger::info("ui", ready_ ? "panel ready" : "panel unavailable; serial-only");
#endif
}

void VisionGuardUi::setCameraStatus(const CameraStatus &status, const char *sourceName) {
  cam_ = status;
  if (sourceName) camSource_ = sourceName;
}

void VisionGuardUi::showScreen(VisionScreen s) {
  if (s >= SCR_COUNT) return;
  screen_ = s;
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  dirty_ = true;
#endif
}

void VisionGuardUi::markDirty() {
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  dirty_ = true;
#endif
}

VisionEvent VisionGuardUi::tick() {
  touch_.tick();
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  if (!ready_) return VisionEvent{};
  VisionEvent ev = handleTouch_();
  const bool animate = (screen_ == SCR_LIVE);
  if (dirty_ || (animate && millis() - lastDrawMs_ >= 500)) {
    draw_();
    dirty_ = false;
    lastDrawMs_ = millis();
  }
  return ev;
#else
  return VisionEvent{};
#endif
}

void VisionGuardUi::printTouchDiagnostics(Print &out) const {
  out.print(F("[touch] screen="));
  out.print(screenName());
  out.print(F(" taps="));
  out.print(touch_.count());
  out.print(F(" down="));
  out.print(touch_.down() ? 1 : 0);
  out.print(F(" raw=("));
  out.print(touch_.rawX());
  out.print(F(","));
  out.print(touch_.rawY());
  out.print(F(") mapped=("));
  out.print(touch_.x());
  out.print(F(","));
  out.print(touch_.y());
  out.println(F(")"));
}

void VisionGuardUi::renderSerial(Print &out) const {
  out.print(F("[screen:"));
  out.print(screenName());
  out.println(F("]"));
  switch (screen_) {
    case SCR_LIVE:
      out.print(F("  camera source="));
      out.print(camSource_);
      out.print(F(" online="));
      out.print(cam_.online ? F("yes") : F("no"));
      out.print(F(" mode="));
      out.print(cam_.mode);
      out.print(F(" frame="));
      out.print(cam_.frameId);
      out.print(F(" res="));
      out.print(cam_.width);
      out.print(F("x"));
      out.println(cam_.height);
      out.print(F("  frame=placeholder reason="));
      out.print(cameraStubReason());
      out.print(F(" ("));
      out.print(cameraHardwareNote());
      out.println(F(")"));
      break;
    case SCR_SCAN:
      out.print(F("  last code="));
      out.println(wf_ && wf_->hasCurrent() ? wf_->current().qr : "(none)");
      break;
    case SCR_CHECKS:
    case SCR_RESULT: {
      if (!wf_ || !wf_->hasCurrent()) { out.println(F("  no inspection yet")); break; }
      const InspectionRun &r = wf_->current();
      out.print(F("  qr="));
      out.print(r.qr);
      out.print(F(" status="));
      out.print(r.failStatus ? F("FAIL") : F("PASS"));
      out.print(F(" p="));
      out.print(r.passed);
      out.print(F(" f="));
      out.print(r.failed);
      out.print(F(" s="));
      out.println(r.skipped);
      for (uint8_t i = 0; i < wf_->itemCount(); i++) {
        out.print(F("  ["));
        out.print(itemStateLabel(r.items[i]));
        out.print(F("] "));
        out.println(wf_->itemName(i));
      }
      out.print(F("  reason="));
      out.println(r.reason);
      out.print(F("  ai="));
      out.println(r.aiNote);
      break;
    }
    case SCR_HISTORY: {
      uint8_t n = wf_ ? wf_->historyCount() : 0;
      out.print(F("  runs="));
      out.println(n);
      for (uint8_t i = 0; i < n; i++) {
        const InspectionRun &r = wf_->runAt(i);
        out.print(F("  #"));
        out.print(i);
        out.print(F(" "));
        out.print(r.qr);
        out.print(F(" "));
        out.print(r.failStatus ? F("FAIL") : F("PASS"));
        out.print(F(" "));
        out.print(r.passed);
        out.print(F("/"));
        out.println(wf_->itemCount());
      }
      break;
    }
    default:
      break;
  }
}

// ===========================================================================
// Display-only rendering + touch.
// ===========================================================================
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)

VisionEvent VisionGuardUi::handleTouch_() {
  if (!touch_.releasedEdge()) return VisionEvent{};
  const int16_t x = touch_.releaseX();
  const int16_t y = touch_.releaseY();
  dirty_ = true;

  const int8_t tab = tabHit(x, y, SCR_COUNT);
  if (tab >= 0) {
    showScreen((VisionScreen)tab);
    return VisionEvent{};
  }

  switch (screen_) {
    case SCR_LIVE:
      if (inR(kLiveCapBtn, x, y)) return {VisionEventType::Scan, -1};
      break;
    case SCR_SCAN:
      if (inR(kScanBtn, x, y)) return {VisionEventType::Scan, -1};
      break;
    case SCR_CHECKS:
      if (wf_ && wf_->hasCurrent()) {
        for (uint8_t i = 0; i < wf_->itemCount(); i++) {
          if (inR(chkRow(i), x, y)) return {VisionEventType::CycleItem, (int16_t)i};
        }
        if (inR(kChkEvalBtn, x, y)) return {VisionEventType::ReEvaluate, -1};
        if (inR(kChkViewBtn, x, y)) { showScreen(SCR_RESULT); return VisionEvent{}; }
      }
      break;
    case SCR_RESULT:
      if (inR(kResRescan, x, y)) return {VisionEventType::Scan, -1};
      break;
    case SCR_HISTORY: {
      const uint8_t n = wf_ ? wf_->historyCount() : 0;
      const uint8_t pages = n ? (uint8_t)((n + kHistPageSize - 1) / kHistPageSize) : 1;
      if (inR(kHistPrev, x, y)) { if (histPage_ > 0) histPage_--; return VisionEvent{}; }
      if (inR(kHistNext, x, y)) { if (histPage_ + 1 < pages) histPage_++; return VisionEvent{}; }
      for (uint8_t i = 0; i < kHistPageSize; i++) {
        const uint8_t age = histPage_ * kHistPageSize + i;
        if (age >= n) break;
        if (inR(histRow(i), x, y)) return {VisionEventType::OpenRun, (int16_t)age};
      }
      break;
    }
    default:
      break;
  }
  return VisionEvent{};
}

void VisionGuardUi::draw_() {
  Arduino_GFX *g = CrowDisplay::canvas();
  if (!g) return;
  g->fillScreen(kBg);
  drawHeader_();
  switch (screen_) {
    case SCR_LIVE:    drawLive_();    break;
    case SCR_SCAN:    drawScan_();    break;
    case SCR_CHECKS:  drawChecks_();  break;
    case SCR_RESULT:  drawResult_();  break;
    case SCR_HISTORY: drawHistory_(); break;
    default: break;
  }
  tabBar(g, kTabs, SCR_COUNT, (uint8_t)screen_);
  CrowDisplay::flush();
}

void VisionGuardUi::drawHeader_() {
  Arduino_GFX *g = CrowDisplay::canvas();
  const bool has = wf_ && wf_->hasCurrent();

  char sub[48];
  switch (screen_) {
    case SCR_LIVE:    snprintf(sub, sizeof(sub), "Live camera - synthetic feed"); break;
    case SCR_SCAN:    snprintf(sub, sizeof(sub), "Capture a code to inspect"); break;
    case SCR_CHECKS:  snprintf(sub, sizeof(sub), "Checklist - tap a row to set state"); break;
    case SCR_RESULT:  snprintf(sub, sizeof(sub), "Inspection result"); break;
    case SCR_HISTORY: snprintf(sub, sizeof(sub), "Audit log - %u runs",
                               wf_ ? wf_->historyCount() : 0); break;
    default: sub[0] = 0; break;
  }

  char pillBuf[20];
  uint16_t pillColor = kAccent;
  if (has) {
    const InspectionRun &r = wf_->current();
    if (r.failStatus) {
      snprintf(pillBuf, sizeof(pillBuf), "FAIL %u", r.failed);
      pillColor = kRed;
    } else {
      snprintf(pillBuf, sizeof(pillBuf), "PASS %u/%u", r.passed, wf_->itemCount());
      pillColor = kGreen;
    }
  } else {
    snprintf(pillBuf, sizeof(pillBuf), "READY");
  }
  headerBar(g, "VISION GUARD", sub, pillBuf, pillColor);
}

void VisionGuardUi::drawLive_() {
  Arduino_GFX *g = CrowDisplay::canvas();

  // Honest placeholder "frame" - a viewfinder with NO photographed content.
  panel(g, kLiveFrame.x, kLiveFrame.y, kLiveFrame.w, kLiveFrame.h, 14, kSurface, 1, kLine);
  // Faint guide lines + a moving scanline so it reads as live-but-imageless.
  for (int16_t gy = kLiveFrame.y + 40; gy < kLiveFrame.y + kLiveFrame.h - 20; gy += 44) {
    g->drawFastHLine(kLiveFrame.x + 20, gy, kLiveFrame.w - 40, kSurfaceHi);
  }
  int16_t scan = kLiveFrame.y + 24 + (int16_t)((millis() / 18) % (kLiveFrame.h - 48));
  g->drawFastHLine(kLiveFrame.x + 16, scan, kLiveFrame.w - 32, kAccent);
  drawFrameCorners(g, kLiveFrame, kAccent);
  // Center crosshair.
  int16_t cx = kLiveFrame.x + kLiveFrame.w / 2;
  int16_t cy = kLiveFrame.y + kLiveFrame.h / 2;
  g->drawFastHLine(cx - 16, cy, 32, kLine);
  g->drawFastVLine(cx, cy - 16, 32, kLine);

  pill(g, kLiveFrame.x + 16, kLiveFrame.y + 14, "MOCK PREVIEW", fontS(), kBg, kAmber);
  text(g, cx, cy - 70, "NO LIVE CAMERA FRAME", fontL(), kTextHi, kCenter);
  text(g, cx, cy - 40, cameraHardwareNote(), fontS(), kTextMut, kCenter);
  text(g, cx, cy + 40, "driver:", fontS(), kTextMut, kCenter);
  text(g, cx, cy + 60, cameraStubReason(), fontS(), kAmber, kCenter);

  // Camera status card.
  const Rect &c = kLiveCamCard;
  panel(g, c.x, c.y, c.w, c.h, 12, kSurface, 1, kLine);
  text(g, c.x + 16, c.y + 14, "CAMERA", fontS(), kTextMut, kLeft);
  statusDot(g, c.x + 24, c.y + 46, 6, cam_.online ? kGreen : kRed);
  text(g, c.x + 40, c.y + 36, cam_.online ? "SYNTHETIC FEED" : "OFFLINE", fontL(), kTextHi, kLeft);
  char line[40];
  snprintf(line, sizeof(line), "Source  %s", camSource_);
  text(g, c.x + 16, c.y + 72, line, fontS(), kTextMut, kLeft);
  snprintf(line, sizeof(line), "Mode    %s", cam_.mode.c_str());
  text(g, c.x + 16, c.y + 96, line, fontS(), kTextMut, kLeft);
  snprintf(line, sizeof(line), "Frame   %lu", (unsigned long)cam_.frameId);
  text(g, c.x + 16, c.y + 120, line, fontS(), kTextMut, kLeft);
  snprintf(line, sizeof(line), "Res     %ux%u", cam_.width, cam_.height);
  text(g, c.x + 16, c.y + 144, line, fontS(), kTextMut, kLeft);

  // Capture card.
  const Rect &a = kLiveCapCard;
  panel(g, a.x, a.y, a.w, a.h, 12, kSurface, 1, kLine);
  text(g, a.x + 16, a.y + 14, "CAPTURE", fontS(), kTextMut, kLeft);
  text(g, a.x + 16, a.y + 40, "Grab the next code and", fontS(), kTextMut, kLeft);
  text(g, a.x + 16, a.y + 60, "open its checklist.", fontS(), kTextMut, kLeft);
  String last = wf_ && wf_->hasCurrent() ? String("Last: ") + wf_->current().qr : String("Last: --");
  text(g, a.x + 16, a.y + 88, fitText(g, last, fontS(), a.w - 32).c_str(), fontS(), kTextHi, kLeft);
  touchButton(g, kLiveCapBtn.x, kLiveCapBtn.y, kLiveCapBtn.w, kLiveCapBtn.h,
              "CAPTURE & INSPECT", true);
}

void VisionGuardUi::drawScan_() {
  Arduino_GFX *g = CrowDisplay::canvas();
  const Rect &c = kScanCard;
  panel(g, c.x, c.y, c.w, c.h, 14, kSurface, 1, kLine);
  text(g, c.x + 24, c.y + 18, "CODE SCANNER", fontS(), kTextMut, kLeft);

  String qr = wf_ && wf_->hasCurrent() ? String(wf_->current().qr) : String("READY-TO-SCAN");
  uint32_t h = hash32(qr.c_str());

  // Mock data glyph (a representation of the code string, NOT a camera image).
  const int16_t gx = c.x + 40, gy = c.y + 64, cell = 9, dim = 12;
  panel(g, gx - 10, gy - 10, dim * cell + 20, dim * cell + 20, 8, kBg, 1, kLine);
  uint32_t bits = h;
  for (int16_t r = 0; r < dim; r++) {
    for (int16_t col = 0; col < dim; col++) {
      bool finder = (r < 3 && col < 3) || (r < 3 && col >= dim - 3) || (r >= dim - 3 && col < 3);
      bits = bits * 1103515245u + 12345u;
      bool on = finder ? ((r == 0 || r == 2 || col == 0 || col == 2) || (r == 1 && col == 1))
                       : ((bits >> 24) & 1);
      if (on) g->fillRect(gx + col * cell, gy + r * cell, cell - 1, cell - 1, kTextHi);
    }
  }
  text(g, gx - 10, gy + dim * cell + 16, "sample glyph (mock)", fontS(), kTextMut, kLeft);

  // Decoded fields.
  const int16_t tx = c.x + 210;
  text(g, tx, c.y + 60, "DECODED", fontS(), kTextMut, kLeft);
  text(g, tx, c.y + 84, fitText(g, qr, fontXL(), c.w - 230).c_str(), fontXL(), kTextHi, kLeft);
  char row[48];
  snprintf(row, sizeof(row), "Symbology   QR / Code-128");
  text(g, tx, c.y + 140, row, fontS(), kTextMut, kLeft);
  snprintf(row, sizeof(row), "Length      %u bytes", (unsigned)qr.length());
  text(g, tx, c.y + 168, row, fontS(), kTextMut, kLeft);
  snprintf(row, sizeof(row), "Checksum    0x%02X", (unsigned)(h & 0xFF));
  text(g, tx, c.y + 196, row, fontS(), kTextMut, kLeft);
  snprintf(row, sizeof(row), "Source      mock scanner");
  text(g, tx, c.y + 224, row, fontS(), kTextMut, kLeft);

  touchButton(g, kScanBtn.x, kScanBtn.y, kScanBtn.w, kScanBtn.h, "CAPTURE NEXT CODE", true);

  // Recent codes as chips.
  if (wf_ && wf_->historyCount() > 0) {
    text(g, 16, 480, "RECENT", fontS(), kTextMut, kLeft);
    int16_t px = 92;
    uint8_t shown = wf_->historyCount() < 4 ? wf_->historyCount() : 4;
    for (uint8_t i = 0; i < shown; i++) {
      int16_t pw = pill(g, px, 474, wf_->runAt(i).qr, fontS(), kTextHi, kSurfaceHi);
      px += pw + 10;
    }
  }
}

void VisionGuardUi::drawChecks_() {
  Arduino_GFX *g = CrowDisplay::canvas();
  if (!wf_ || !wf_->hasCurrent()) {
    text(g, kW / 2, 280, "No inspection yet", fontL(), kTextHi, kCenter);
    text(g, kW / 2, 312, "Tap SCAN, then CAPTURE NEXT CODE", fontS(), kTextMut, kCenter);
    return;
  }
  const InspectionRun &r = wf_->current();
  const uint8_t total = wf_->itemCount();

  text(g, 16, 84, (String("CHECKLIST  ") + r.qr).c_str(), fontL(), kTextHi, kLeft);

  float frac = total ? (float)r.passed / total : 0.0f;
  hBar(g, kChkProgress.x, kChkProgress.y, kChkProgress.w, kChkProgress.h, frac, kGreen);
  char cnt[16];
  int16_t px = kChkProgress.x + kChkProgress.w + 14;
  snprintf(cnt, sizeof(cnt), "P %u", r.passed);
  px += pill(g, px, kChkProgress.y - 5, cnt, fontS(), kBg, kGreen) + 8;
  snprintf(cnt, sizeof(cnt), "F %u", r.failed);
  px += pill(g, px, kChkProgress.y - 5, cnt, fontS(), kBg, r.failed ? kRed : kLine) + 8;
  snprintf(cnt, sizeof(cnt), "S %u", r.skipped);
  pill(g, px, kChkProgress.y - 5, cnt, fontS(), kBg, r.skipped ? kAmber : kLine);

  for (uint8_t i = 0; i < total; i++) {
    Rect row = chkRow(i);
    ItemState st = r.items[i];
    bool bad = (st == ITEM_FAIL);
    panel(g, row.x, row.y, row.w, row.h, 10, bad ? kSurfaceHi : kSurface, 1,
          bad ? kRed : kLine);
    text(g, row.x + 16, row.y + 8, wf_->itemName(i), fontM(), kTextHi, kLeft);
    text(g, row.x + 16, row.y + 29,
         fitText(g, wf_->itemDetail(i), fontS(), row.w - 150).c_str(), fontS(), kTextMut, kLeft);
    const char *lbl = itemStateLabel(st);
    int16_t pw = textWidth(g, lbl, fontS()) + 24;
    pill(g, row.x + row.w - pw - 12, row.y + (row.h - 28) / 2, lbl, fontS(), kBg, stateColor(st));
  }

  touchButton(g, kChkEvalBtn.x, kChkEvalBtn.y, kChkEvalBtn.w, kChkEvalBtn.h, "RE-EVALUATE", false);
  touchButton(g, kChkViewBtn.x, kChkViewBtn.y, kChkViewBtn.w, kChkViewBtn.h, "VIEW RESULT", true);

  const Rect &s = kChkSummary;
  panel(g, s.x, s.y, s.w, s.h, 12, kSurface, 1, kLine);
  text(g, s.x + 16, s.y + 14, "SUMMARY", fontS(), kTextMut, kLeft);
  text(g, s.x + 16, s.y + 40, r.failStatus ? "FAIL" : "PASS", fontXL(),
       r.failStatus ? kRed : kGreen, kLeft);
  String reason[2];
  uint8_t rl = wrapText(g, r.reason, fontS(), s.w - 32, reason, 2);
  for (uint8_t i = 0; i < rl; i++) {
    text(g, s.x + 16, s.y + 96 + i * 20, reason[i].c_str(), fontS(), kTextHi, kLeft);
  }
  String note[3];
  uint8_t nl = wrapText(g, r.aiNote, fontS(), s.w - 32, note, 3);
  for (uint8_t i = 0; i < nl; i++) {
    text(g, s.x + 16, s.y + 150 + i * 20, note[i].c_str(), fontS(), kTextMut, kLeft);
  }
}

void VisionGuardUi::drawResult_() {
  Arduino_GFX *g = CrowDisplay::canvas();
  if (!wf_ || !wf_->hasCurrent()) {
    text(g, kW / 2, 280, "No result yet", fontL(), kTextHi, kCenter);
    text(g, kW / 2, 312, "Capture a code to run an inspection", fontS(), kTextMut, kCenter);
    return;
  }
  const InspectionRun &r = wf_->current();
  const uint8_t total = wf_->itemCount();
  const bool fail = r.failStatus;
  const uint16_t hue = fail ? kRed : kGreen;

  // Hero.
  const Rect &hero = kResHero;
  panel(g, hero.x, hero.y, hero.w, hero.h, 14, kSurfaceHi, 2, hue);
  text(g, hero.x + 24, hero.y + 24, fail ? "FAIL" : "PASS", fontXL(), hue, kLeft);
  text(g, hero.x + 24, hero.y + 96, fitText(g, r.qr, fontL(), hero.w - 48).c_str(),
       fontL(), kTextHi, kLeft);
  unsigned long ageS = r.seedAgoS + (millis() - r.createdMs) / 1000UL;
  char age[40];
  if (ageS >= 60) snprintf(age, sizeof(age), "%lum %lus ago", ageS / 60, ageS % 60);
  else            snprintf(age, sizeof(age), "%lus ago", ageS);
  text(g, hero.x + 24, hero.y + 128, age, fontS(), kTextMut, kLeft);
  char idbuf[32];
  snprintf(idbuf, sizeof(idbuf), "run #%lu", (unsigned long)r.id);
  text(g, hero.x + 24, hero.y + 152, idbuf, fontS(), kTextMut, kLeft);

  // Counts.
  const Rect &cc = kResCounts;
  panel(g, cc.x, cc.y, cc.w, cc.h, 12, kSurface, 1, kLine);
  struct { const char *l; uint8_t v; uint16_t c; } cols[3] = {
    {"PASS", r.passed, kGreen}, {"FAIL", r.failed, kRed}, {"SKIP", r.skipped, kAmber}};
  for (uint8_t i = 0; i < 3; i++) {
    int16_t colx = cc.x + cc.w / 6 + i * (cc.w / 3);
    char num[6];
    snprintf(num, sizeof(num), "%u", cols[i].v);
    text(g, colx, cc.y + 16, num, fontXL(), cols[i].c, kCenter);
    text(g, colx, cc.y + 66, cols[i].l, fontS(), kTextMut, kCenter);
  }

  touchButton(g, kResRescan.x, kResRescan.y, kResRescan.w, kResRescan.h, "RE-SCAN", true);

  // Reason + AI note.
  const Rect &nt = kResNote;
  panel(g, nt.x, nt.y, nt.w, nt.h, 12, kSurface, 1, kLine);
  text(g, nt.x + 16, nt.y + 14, "VERDICT", fontS(), kTextMut, kLeft);
  text(g, nt.x + 16, nt.y + 36, fitText(g, r.reason, fontL(), nt.w - 32).c_str(),
       fontL(), kTextHi, kLeft);
  text(g, nt.x + 16, nt.y + 74, "AI VISION NOTE", fontS(), kTextMut, kLeft);
  String note[2];
  uint8_t nl = wrapText(g, r.aiNote, fontS(), nt.w - 32, note, 2);
  for (uint8_t i = 0; i < nl; i++) {
    text(g, nt.x + 16, nt.y + 98 + i * 20, note[i].c_str(), fontS(), kTextHi, kLeft);
  }

  // Per-item summary grid (2 columns).
  const Rect &it = kResItems;
  panel(g, it.x, it.y, it.w, it.h, 12, kSurface, 1, kLine);
  text(g, it.x + 16, it.y + 14, "CHECK SUMMARY", fontS(), kTextMut, kLeft);
  const int16_t colW = (it.w - 32) / 2;
  const int16_t rowH = 44;
  for (uint8_t i = 0; i < total; i++) {
    uint8_t col = i % 2, rr = i / 2;
    int16_t x0 = it.x + 16 + col * colW;
    int16_t y0 = it.y + 44 + rr * rowH;
    statusDot(g, x0 + 8, y0 + 8, 5, stateColor(r.items[i]));
    text(g, x0 + 24, y0, fitText(g, wf_->itemName(i), fontS(), colW - 90).c_str(),
         fontS(), kTextHi, kLeft);
    text(g, x0 + colW - 16, y0, itemStateLabel(r.items[i]), fontS(), stateColor(r.items[i]),
         kRight);
  }
}

void VisionGuardUi::drawHistory_() {
  Arduino_GFX *g = CrowDisplay::canvas();
  const uint8_t n = wf_ ? wf_->historyCount() : 0;
  const uint8_t pages = n ? (uint8_t)((n + kHistPageSize - 1) / kHistPageSize) : 1;
  if (histPage_ >= pages) histPage_ = pages - 1;

  char title[40];
  snprintf(title, sizeof(title), "AUDIT LOG  -  %u runs", n);
  text(g, 16, 84, title, fontL(), kTextHi, kLeft);

  if (n == 0) {
    text(g, kW / 2, 280, "No runs recorded", fontL(), kTextMut, kCenter);
    return;
  }

  const uint8_t sel = wf_->currentAgeIndex();
  const uint8_t total = wf_->itemCount();
  for (uint8_t i = 0; i < kHistPageSize; i++) {
    const uint8_t age = histPage_ * kHistPageSize + i;
    if (age >= n) break;
    const InspectionRun &r = wf_->runAt(age);
    Rect row = histRow(i);
    bool selected = (age == sel);
    panel(g, row.x, row.y, row.w, row.h, 10, selected ? kSurfaceHi : kSurface, 1,
          selected ? kAccent : kLine);

    unsigned long ageS = r.seedAgoS + (millis() - r.createdMs) / 1000UL;
    char when[24];
    if (ageS >= 60) snprintf(when, sizeof(when), "%lum%02lus", ageS / 60, ageS % 60);
    else            snprintf(when, sizeof(when), "%lus", ageS);
    text(g, row.x + 16, row.y + 15, when, fontS(), kTextMut, kLeft);
    text(g, row.x + 110, row.y + 13, r.qr, fontM(), kTextHi, kLeft);

    // Right side: passed/total then a status chip.
    char pt[12];
    snprintf(pt, sizeof(pt), "%u/%u", r.passed, total);
    const char *verdict = r.failStatus ? "FAIL" : "PASS";
    uint16_t vc = r.failStatus ? kRed : kGreen;
    int16_t pw = textWidth(g, verdict, fontS()) + 24;
    pill(g, row.x + row.w - pw - 16, row.y + (row.h - 28) / 2, verdict, fontS(), kBg, vc);
    text(g, row.x + row.w - pw - 40, row.y + 15, pt, fontS(), kTextMut, kRight);
  }

  // Pager.
  if (pages > 1) {
    touchButton(g, kHistPrev.x, kHistPrev.y, kHistPrev.w, kHistPrev.h, "< PREV", histPage_ > 0);
    touchButton(g, kHistNext.x, kHistNext.y, kHistNext.w, kHistNext.h, "NEXT >",
                histPage_ + 1 < pages);
    char pg[20];
    snprintf(pg, sizeof(pg), "page %u / %u", histPage_ + 1, pages);
    text(g, kW / 2, kHistPrev.y + 14, pg, fontS(), kTextMut, kCenter);
  } else {
    text(g, kW / 2, kHistPrev.y + 14, "tap a row to re-open its result", fontS(), kTextMut,
         kCenter);
  }
}

#endif  // USE_DISPLAY && CONFIG_IDF_TARGET_ESP32P4
