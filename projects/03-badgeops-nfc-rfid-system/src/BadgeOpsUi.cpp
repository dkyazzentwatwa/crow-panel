#include "BadgeOpsUi.h"

#include <CrowPanelShared.h>

// Timing shared by the display and headless builds.
static const uint32_t kResultHoldMs = 3000;  // RESULT auto-returns to TAP after this

// Reader descriptors are needed on every build (name matching in
// selectReaderByName / setReaderInfo), so they live outside the display guard.
namespace {
struct ReaderDesc {
  const char *key;        // serial / select name
  const char *shortName;  // header pill + card title
  const char *title;      // card headline
  const char *transport;  // one-line transport summary
  const char *wire1;
  const char *wire2;
};
const ReaderDesc kReaders[3] = {
  {"mock", "MOCK", "Mock badge reader", "No hardware - synthetic taps",
   "Emits a demo badge roughly every 4 s",
   "Drives every screen with no module attached"},
  {"pn532", "PN532", "PN532 NFC - I2C", "I2C on the touch bus (SDA 45 / SCL 46)",
   "Answers at 0x24 (GT911 sits at 0x5D / 0x14)",
   "Set BADGEOPS_PN532_IRQ/RESET in config/Pins.h"},
  {"mfrc522", "MFRC522", "MFRC522 RFID - SPI", "SPI on the wireless socket",
   "Default SS 10 / RST 9",
   "Remove any socket module before wiring it in"},
};
}  // namespace

// ===========================================================================
// Target-independent methods (compiled on every build). All draw/touch code is
// gated to USE_DISPLAY && ESP32P4 through the small guards below, so the
// headless build keeps identical Serial behavior.
// ===========================================================================

void BadgeOpsUi::markDirty() {
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  dirty_ = true;
#endif
}

void BadgeOpsUi::begin(const BadgeRegistry *registry, const char *zone) {
  registry_ = registry;
  if (zone != nullptr) {
    zone_ = zone;
  }
  const UiTheme &theme = defaultUiTheme();
  Logger::info("ui", String("theme=") + theme.name +
                         " screens=Tap,Result,Registry,Attendance,Readers");
#if USE_DISPLAY
  // manualFlush: draw a whole frame, then flush once (see DisplayBringup) - this
  // turns Arduino_GFX's per-pixel cache sync into one sync per frame.
  bool ok = CrowDisplay::begin(activeHardwareProfile(), "BadgeOps Access Terminal", true);
#if defined(CONFIG_IDF_TARGET_ESP32P4)
  ready_ = ok && CrowDisplay::canvas() != nullptr;
  dirty_ = true;
#else
  (void)ok;
#endif
#endif
}

void BadgeOpsUi::setReaderInfo(const char *driverName, uint8_t activeReaderIndex,
                               bool activeReady) {
  if (driverName != nullptr) {
    strlcpy(readerDriver_, driverName, sizeof(readerDriver_));
  }
  activeReader_ = activeReaderIndex < kReaderCount ? activeReaderIndex : 0;
  activeReady_ = activeReady;
  selectedReader_ = activeReader_;  // default the READERS screen to the live reader
  markDirty();
}

void BadgeOpsUi::renderTap(const BadgeRead &read) {
  Serial.print(F("[screen:tap] reader="));
  Serial.print(read.reader);
  Serial.print(F(" uid="));
  Serial.println(read.uid);
  lastUid_ = read.uid;
  lastReader_ = read.reader;
  markDirty();
}

void BadgeOpsUi::renderDecision(const AccessDecision &decision, const BadgeRecord &record,
                                bool found, bool present) {
  Serial.print(F("[screen:result:"));
  Serial.print(decision.status);
  Serial.print(F("] "));
  Serial.print(decision.message);
  if (found) {
    Serial.print(F(" role="));
    Serial.print(record.role);
  }
  Serial.println();

  lastDecision_ = decision;
  lastRecord_ = record;
  lastFound_ = found;
  haveDecision_ = true;
  pushAttendance(decision, record, found);

  // Present the RESULT overlay when a user asked for the tap (touch button or
  // serial), or when the panel is already on the idle kiosk view. While the
  // operator is browsing Registry/Attendance/Readers, the automatic mock
  // cadence records the decision without hijacking the screen.
  const bool onKiosk = (screen_ == SCR_TAP || screen_ == SCR_RESULT);
  if (present || onKiosk) {
    screen_ = SCR_RESULT;
    resultShownMs_ = millis();
  }
  markDirty();
}

void BadgeOpsUi::pushAttendance(const AccessDecision &d, const BadgeRecord &r, bool found) {
  AttEntry &e = att_[attHead_];
  e.ms = millis();
  e.granted = (d.status == "granted");
  strlcpy(e.uid, lastUid_.c_str(), sizeof(e.uid));
  strlcpy(e.name, found ? r.name.c_str() : "Unknown badge", sizeof(e.name));
  strlcpy(e.reason, d.reason.c_str(), sizeof(e.reason));
  attHead_ = (attHead_ + 1) % kAttCap;
  if (attCount_ < kAttCap) {
    attCount_++;
  }
}

const BadgeOpsUi::AttEntry &BadgeOpsUi::attAt(uint8_t visualIndex) const {
  // Newest lives at (attHead_ - 1); visualIndex 0 is the newest.
  uint8_t slot = (uint8_t)((attHead_ + kAttCap - 1 - visualIndex) % kAttCap);
  return att_[slot];
}

const char *BadgeOpsUi::lastDecisionUid() const {
  return attCount_ > 0 ? attAt(0).uid : "";
}

void BadgeOpsUi::showScreen(BadgeScreen screen) {
  if (screen >= SCR_COUNT) {
    return;
  }
  if (screen == SCR_REGISTRY) {
    regScroll_ = 0;
  }
  if (screen == SCR_ATTENDANCE) {
    attScroll_ = 0;
  }
  if (screen == SCR_RESULT) {
    resultShownMs_ = millis();
  }
  screen_ = screen;
  markDirty();
}

bool BadgeOpsUi::showScreenByName(const String &name) {
  String n = name;
  n.trim();
  n.toLowerCase();
  if (n == "tap") { showScreen(SCR_TAP); return true; }
  if (n == "result") { showScreen(SCR_RESULT); return true; }
  if (n == "registry" || n == "list" || n == "badges") { showScreen(SCR_REGISTRY); return true; }
  if (n == "badge" || n == "detail") { showScreen(SCR_BADGE); return true; }
  if (n == "attendance" || n == "log") { showScreen(SCR_ATTENDANCE); return true; }
  if (n == "readers" || n == "reader" || n == "settings") { showScreen(SCR_READERS); return true; }
  return false;
}

bool BadgeOpsUi::selectReaderByName(const String &name) {
  String n = name;
  n.trim();
  n.toLowerCase();
  for (uint8_t i = 0; i < kReaderCount; i++) {
    if (n == kReaders[i].key) {
      selectedReader_ = i;
      showScreen(SCR_READERS);
      return true;
    }
  }
  return false;
}

const char *BadgeOpsUi::screenName() const {
  switch (screen_) {
    case SCR_TAP: return "tap";
    case SCR_RESULT: return "result";
    case SCR_REGISTRY: return "registry";
    case SCR_BADGE: return "badge";
    case SCR_ATTENDANCE: return "attendance";
    case SCR_READERS: return "readers";
    default: return "tap";
  }
}

void BadgeOpsUi::printTouch(Print &out) {
  out.print(F("[touch] raw="));
  out.print(touch_.rawX());
  out.print(',');
  out.print(touch_.rawY());
  out.print(F(" mapped="));
  out.print(touch_.x());
  out.print(',');
  out.print(touch_.y());
  out.print(F(" down="));
  out.print(touch_.down() ? 1 : 0);
  out.print(F(" taps="));
  out.print(touch_.count());
  out.print(F(" screen="));
  out.println(screenName());
}

BadgeOpsEvent BadgeOpsUi::tick() {
  touch_.tick();
  BadgeOpsEvent ev;
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  if (ready_) {
    ev = handleTouch();
  }
#endif

  // RESULT auto-returns to TAP after the hold window (same on every build).
  if (screen_ == SCR_RESULT && (millis() - resultShownMs_) >= kResultHoldMs) {
    screen_ = SCR_TAP;
    markDirty();
  }

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  if (ready_) {
    const bool animate = (screen_ == SCR_TAP || screen_ == SCR_RESULT);
    if (dirty_ || (animate && (millis() - lastDrawMs_) >= 120)) {
      draw();
      dirty_ = false;
      lastDrawMs_ = millis();
    }
  }
#endif
  return ev;
}

// ===========================================================================
// Display + touch implementation (ESP32-P4 panel only).
// ===========================================================================
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)

#include <Arduino_GFX_Library.h>
#include <DashboardWidgets.h>

using namespace Widgets;

namespace {
constexpr int16_t kW = 1024;
constexpr int16_t kMargin = 24;

// Quick-tap demo buttons on the TAP screen (mirror the documented `tap`
// examples): active -> granted, suspended -> denied, unknown -> denied.
const char *const kQuickUid[3] = {"04:A1:22:9C", "C2:44:10:AA", "11:22:33:44"};
const char *const kQuickLabel[3] = {"TAP ACTIVE", "TAP SUSPENDED", "TAP UNKNOWN"};

const char *const kTabLabels[4] = {"TAP", "REGISTRY", "ATTENDANCE", "READERS"};
constexpr uint8_t kTabCount = 4;

struct Rect {
  int16_t x, y, w, h;
};
bool inRect(const Rect &r, int16_t x, int16_t y) {
  return hitRect(x, y, r.x, r.y, r.w, r.h);
}

// TAP
const Rect kTapHero = {24, 96, 976, 316};
Rect tapButton(uint8_t i) {
  const int16_t w = 300, gap = 38;
  return {int16_t(24 + i * (w + gap)), 436, w, 72};
}

// RESULT
const Rect kResBanner = {24, 96, 976, 104};
const Rect kResBadge = {24, 216, 600, 296};
const Rect kResReason = {648, 216, 352, 296};

// REGISTRY
constexpr int16_t kRegRowH = 68;
const Rect kRegList = {24, 96, 836, 420};
const Rect kRegUp = {884, 96, 116, 120};
const Rect kRegDown = {884, 396, 116, 120};
Rect regRow(uint8_t i) {
  return {kRegList.x, int16_t(kRegList.y + i * kRegRowH), kRegList.w, int16_t(kRegRowH - 8)};
}

// BADGE detail
const Rect kBadgeCard = {140, 104, 744, 300};
const Rect kBadgeBack = {140, 432, 300, 68};
const Rect kBadgeSim = {584, 432, 300, 68};

// ATTENDANCE
constexpr int16_t kAttRowH = 58;
const Rect kAttList = {24, 132, 836, 384};
const Rect kAttUp = {884, 132, 116, 120};
const Rect kAttDown = {884, 396, 116, 120};
Rect attRow(uint8_t i) {
  return {kAttList.x, int16_t(kAttList.y + i * kAttRowH), kAttList.w, int16_t(kAttRowH - 8)};
}

// READERS
Rect readerCard(uint8_t i) {
  const int16_t w = 300, gap = 38;
  return {int16_t(24 + i * (w + gap)), 96, w, 216};
}
const Rect kReaderDetail = {24, 332, 976, 180};

uint8_t selectedTab(BadgeScreen s) {
  switch (s) {
    case SCR_REGISTRY:
    case SCR_BADGE:
      return 1;
    case SCR_ATTENDANCE:
      return 2;
    case SCR_READERS:
      return 3;
    default:
      return 0;  // TAP + RESULT
  }
}

uint16_t statusColor(const String &status) {
  if (status == "active") return kGreen;
  if (status == "suspended" || status == "expired") return kAmber;
  return kTextMut;
}
}  // namespace

uint8_t BadgeOpsUi::visibleRegistryRows() const { return kRegList.h / kRegRowH; }
uint8_t BadgeOpsUi::visibleAttendanceRows() const { return kAttList.h / kAttRowH; }

BadgeOpsEvent BadgeOpsUi::handleTouch() {
  BadgeOpsEvent ev;
  if (!touch_.releasedEdge()) {
    return ev;
  }
  const int16_t x = touch_.releaseX();
  const int16_t y = touch_.releaseY();

  // The bottom tab bar is live on every screen.
  const int8_t tab = tabHit(x, y, kTabCount);
  if (tab >= 0) {
    switch (tab) {
      case 0: showScreen(SCR_TAP); break;
      case 1: showScreen(SCR_REGISTRY); break;
      case 2: showScreen(SCR_ATTENDANCE); break;
      case 3: showScreen(SCR_READERS); break;
    }
    return ev;
  }

  switch (screen_) {
    case SCR_TAP:
      for (uint8_t i = 0; i < 3; i++) {
        if (inRect(tapButton(i), x, y)) {
          ev.type = EV_TAP_UID;
          ev.arg = kQuickUid[i];
          ev.userInitiated = true;
          return ev;
        }
      }
      break;

    case SCR_RESULT:
      // Tapping anywhere in the content band dismisses back to TAP.
      if (y >= kChromeHeaderH && y < kChromeTabY) {
        showScreen(SCR_TAP);
      }
      break;

    case SCR_REGISTRY: {
      const uint8_t cnt = registry_ ? registry_->count() : 0;
      const uint8_t vis = visibleRegistryRows();
      if (inRect(kRegUp, x, y)) {
        if (regScroll_ > 0) { regScroll_--; markDirty(); }
        return ev;
      }
      if (inRect(kRegDown, x, y)) {
        if (cnt > vis && regScroll_ < (uint8_t)(cnt - vis)) { regScroll_++; markDirty(); }
        return ev;
      }
      for (uint8_t i = 0; i < vis && (uint8_t)(regScroll_ + i) < cnt; i++) {
        if (inRect(regRow(i), x, y)) {
          regSelected_ = regScroll_ + i;
          showScreen(SCR_BADGE);
          return ev;
        }
      }
      break;
    }

    case SCR_BADGE:
      if (inRect(kBadgeBack, x, y)) {
        showScreen(SCR_REGISTRY);
        return ev;
      }
      if (inRect(kBadgeSim, x, y) && registry_) {
        ev.type = EV_TAP_UID;
        ev.arg = registry_->at(regSelected_).uid;
        ev.userInitiated = true;
        return ev;
      }
      break;

    case SCR_ATTENDANCE: {
      const uint8_t vis = visibleAttendanceRows();
      if (inRect(kAttUp, x, y)) {
        if (attScroll_ > 0) { attScroll_--; markDirty(); }
        return ev;
      }
      if (inRect(kAttDown, x, y)) {
        if (attCount_ > vis && attScroll_ < (uint8_t)(attCount_ - vis)) { attScroll_++; markDirty(); }
        return ev;
      }
      break;
    }

    case SCR_READERS:
      for (uint8_t i = 0; i < kReaderCount; i++) {
        if (inRect(readerCard(i), x, y)) {
          selectedReader_ = i;
          markDirty();
          return ev;
        }
      }
      break;

    default:
      break;
  }
  return ev;
}

void BadgeOpsUi::draw() {
  Arduino_GFX *g = CrowDisplay::canvas();
  if (g == nullptr) {
    return;
  }
  g->fillScreen(kBg);

  const ReaderDesc &ar = kReaders[activeReader_];
  char sub[48];
  snprintf(sub, sizeof(sub), "Access terminal - zone %s", zone_.c_str());
  char pillText[24];
  if (activeReader_ == 0) {
    snprintf(pillText, sizeof(pillText), "MOCK");
  } else {
    snprintf(pillText, sizeof(pillText), "%s %s", ar.shortName, activeReady_ ? "READY" : "WAIT");
  }
  const uint16_t pillCol = (activeReader_ == 0) ? kAccent : (activeReady_ ? kGreen : kAmber);
  headerBar(g, "BADGEOPS", sub, pillText, pillCol);

  switch (screen_) {
    case SCR_TAP: drawTap(); break;
    case SCR_RESULT: drawResult(); break;
    case SCR_REGISTRY: drawRegistry(); break;
    case SCR_BADGE: drawBadge(); break;
    case SCR_ATTENDANCE: drawAttendance(); break;
    case SCR_READERS: drawReaders(); break;
    default: break;
  }

  tabBar(g, kTabLabels, kTabCount, selectedTab(screen_));
  CrowDisplay::flush();
}

void BadgeOpsUi::drawTap() {
  Arduino_GFX *g = CrowDisplay::canvas();
  panel(g, kTapHero.x, kTapHero.y, kTapHero.w, kTapHero.h, 18, kSurface, 1, kLine);

  // Animated waiting pulse: concentric rings, the phase ring in the accent.
  const int16_t cx = kW / 2, cy = 192;
  const int16_t radii[4] = {30, 52, 74, 94};
  const uint8_t phase = (uint8_t)((millis() / 260) % 4);
  for (int8_t i = 3; i >= 0; --i) {
    uint16_t col = kSurfaceHi;
    if ((uint8_t)i == phase) {
      col = kAccent;
    } else if ((uint8_t)i == (uint8_t)((phase + 3) % 4)) {
      col = kLine;
    }
    g->drawCircle(cx, cy, radii[i], col);
    g->drawCircle(cx, cy, radii[i] - 1, col);
  }
  // Badge glyph in the middle: a little card with a chip + magnetic stripe.
  panel(g, cx - 26, cy - 18, 52, 36, 6, kSurfaceHi, 1, kAccent);
  g->fillRoundRect(cx - 18, cy - 8, 16, 12, 2, kAccent);
  g->drawFastHLine(cx + 4, cy - 6, 16, kTextMut);
  g->drawFastHLine(cx + 4, cy + 0, 16, kTextMut);
  g->drawFastHLine(cx + 4, cy + 6, 12, kTextMut);

  text(g, cx, 300, "PRESENT A BADGE", fontXL(), kTextHi, kCenter);
  text(g, cx, 344, "Hold a badge near the reader", fontS(), kTextMut, kCenter);

  const ReaderDesc &ar = kReaders[activeReader_];
  char id[72];
  snprintf(id, sizeof(id), "%s  -  %s", ar.title,
           activeReady_ ? "waiting for badge" : "transport not ready");
  const uint16_t idCol = (activeReader_ == 0) ? kAccent : (activeReady_ ? kGreen : kAmber);
  text(g, cx, 368, id, fontS(), idCol, kCenter);

  if (haveDecision_) {
    const bool granted = (lastDecision_.status == "granted");
    char recap[80];
    snprintf(recap, sizeof(recap), "Last: %s  ->  %s", lastUid_.c_str(),
             granted ? "GRANTED" : "DENIED");
    text(g, cx, 390, recap, fontS(), granted ? kGreen : kRed, kCenter);
  }

  const uint16_t accents[3] = {kGreen, kAmber, kRed};
  for (uint8_t i = 0; i < 3; i++) {
    Rect r = tapButton(i);
    touchButton(g, r.x, r.y, r.w, r.h, kQuickLabel[i], true, accents[i]);
  }
}

void BadgeOpsUi::drawResult() {
  Arduino_GFX *g = CrowDisplay::canvas();
  const bool granted = (lastDecision_.status == "granted");
  const uint16_t accent = granted ? kGreen : kRed;

  panel(g, kResBanner.x, kResBanner.y, kResBanner.w, kResBanner.h, 16, accent, 0, accent);
  text(g, kW / 2, kResBanner.y + 34, granted ? "ACCESS GRANTED" : "ACCESS DENIED",
       fontXL(), kBg, kCenter);

  // Badge card.
  panel(g, kResBadge.x, kResBadge.y, kResBadge.w, kResBadge.h, 16, kSurface, 1, kLine);
  const int16_t bx = kResBadge.x + 28;
  if (lastFound_) {
    text(g, bx, kResBadge.y + 24, lastRecord_.name.c_str(), fontL(), kTextHi, kLeft);
    statusDot(g, kResBadge.x + kResBadge.w - 36, kResBadge.y + 32, 8, statusColor(lastRecord_.status));
    char line[72];
    snprintf(line, sizeof(line), "Role   %s", lastRecord_.role.c_str());
    text(g, bx, kResBadge.y + 78, line, fontM(), kTextMut, kLeft);
    snprintf(line, sizeof(line), "Badge  %s", lastRecord_.badgeId.c_str());
    text(g, bx, kResBadge.y + 116, line, fontM(), kTextMut, kLeft);
    snprintf(line, sizeof(line), "UID    %s", lastUid_.c_str());
    text(g, bx, kResBadge.y + 154, line, fontM(), kTextMut, kLeft);
    snprintf(line, sizeof(line), "Zones  %s", lastRecord_.allowedZones.c_str());
    text(g, bx, kResBadge.y + 192, line, fontM(), kTextMut, kLeft);
    pill(g, bx, kResBadge.y + 236, lastRecord_.status.c_str(), fontS(), kBg,
         statusColor(lastRecord_.status));
  } else {
    text(g, bx, kResBadge.y + 24, "UNKNOWN BADGE", fontL(), kRed, kLeft);
    char line[72];
    snprintf(line, sizeof(line), "UID    %s", lastUid_.c_str());
    text(g, bx, kResBadge.y + 96, line, fontM(), kTextMut, kLeft);
    text(g, bx, kResBadge.y + 150, "This UID is not enrolled in the", fontS(), kTextMut, kLeft);
    text(g, bx, kResBadge.y + 176, "badge registry.", fontS(), kTextMut, kLeft);
  }

  // Reason card.
  panel(g, kResReason.x, kResReason.y, kResReason.w, kResReason.h, 16, kSurfaceHi, 1, accent);
  const int16_t rx = kResReason.x + 24;
  text(g, rx, kResReason.y + 24, "POLICY", fontS(), kTextMut, kLeft);
  text(g, rx, kResReason.y + 56, granted ? "GRANTED" : "DENIED", fontL(), accent, kLeft);
  text(g, rx, kResReason.y + 104, lastDecision_.reason.c_str(), fontM(), kTextHi, kLeft);

  const uint32_t elapsed = millis() - resultShownMs_;
  const uint32_t remain = (elapsed >= kResultHoldMs) ? 0 : (kResultHoldMs - elapsed);
  char cd[32];
  snprintf(cd, sizeof(cd), "Returning in %lus", (unsigned long)((remain + 999) / 1000));
  text(g, rx, kResReason.y + 208, cd, fontS(), kTextMut, kLeft);
  text(g, rx, kResReason.y + 240, "Tap anywhere to dismiss", fontS(), kTextMut, kLeft);
}

void BadgeOpsUi::drawRegistry() {
  Arduino_GFX *g = CrowDisplay::canvas();
  const uint8_t cnt = registry_ ? registry_->count() : 0;
  const uint8_t vis = visibleRegistryRows();

  for (uint8_t i = 0; i < vis && (uint8_t)(regScroll_ + i) < cnt; i++) {
    const BadgeRecord &r = registry_->at(regScroll_ + i);
    Rect row = regRow(i);
    panel(g, row.x, row.y, row.w, row.h, 12, kSurface, 1, kLine);
    statusDot(g, row.x + 26, row.y + row.h / 2, 8, statusColor(r.status));
    text(g, row.x + 54, row.y + 12, r.name.c_str(), fontL(), kTextHi, kLeft);
    char meta[72];
    snprintf(meta, sizeof(meta), "%s  -  %s", r.role.c_str(), r.uid.c_str());
    text(g, row.x + 54, row.y + 38, meta, fontS(), kTextMut, kLeft);
    const int16_t pw = textWidth(g, r.status.c_str(), fontS()) + 24;
    pill(g, row.x + row.w - pw - 92, row.y + (row.h - 28) / 2, r.status.c_str(), fontS(), kBg,
         statusColor(r.status));
    text(g, row.x + row.w - 20, row.y + (row.h - 16) / 2, "INSPECT", fontS(), kAccent, kRight);
  }

  const bool canUp = regScroll_ > 0;
  const bool canDown = cnt > vis && regScroll_ < (uint8_t)(cnt - vis);
  touchButton(g, kRegUp.x, kRegUp.y, kRegUp.w, kRegUp.h, "UP", canUp, kAccent);
  touchButton(g, kRegDown.x, kRegDown.y, kRegDown.w, kRegDown.h, "DOWN", canDown, kAccent);
  const uint8_t last = (uint8_t)min((int)(regScroll_ + vis), (int)cnt);
  char cc[16];
  snprintf(cc, sizeof(cc), "%u-%u / %u", (unsigned)(cnt ? regScroll_ + 1 : 0), (unsigned)last,
           (unsigned)cnt);
  text(g, kRegUp.x + kRegUp.w / 2, 300, cc, fontS(), kTextMut, kCenter);
}

void BadgeOpsUi::drawBadge() {
  Arduino_GFX *g = CrowDisplay::canvas();
  if (!registry_) {
    return;
  }
  const BadgeRecord &r = registry_->at(regSelected_);
  const Rect c = kBadgeCard;
  panel(g, c.x, c.y, c.w, c.h, 16, kSurface, 1, statusColor(r.status));
  text(g, c.x + 32, c.y + 26, r.name.c_str(), fontL(), kTextHi, kLeft);
  const int16_t pw = textWidth(g, r.status.c_str(), fontS()) + 24;
  pill(g, c.x + c.w - pw - 32, c.y + 24, r.status.c_str(), fontS(), kBg, statusColor(r.status));

  const char *lk[3] = {"BADGE ID", "UID", "ROLE"};
  String lv[3] = {r.badgeId, r.uid, r.role};
  const char *rk[2] = {"STATUS", "ALLOWED ZONES"};
  String rv[2] = {r.status, r.allowedZones};
  const int16_t lx = c.x + 32, rx = c.x + 384;
  for (uint8_t i = 0; i < 3; i++) {
    const int16_t y = c.y + 92 + i * 66;
    text(g, lx, y, lk[i], fontS(), kTextMut, kLeft);
    text(g, lx, y + 24, lv[i].c_str(), fontM(), kTextHi, kLeft);
  }
  for (uint8_t i = 0; i < 2; i++) {
    const int16_t y = c.y + 92 + i * 66;
    text(g, rx, y, rk[i], fontS(), kTextMut, kLeft);
    const uint16_t vc = (i == 0) ? statusColor(r.status) : kTextHi;
    text(g, rx, y + 24, rv[i].c_str(), fontM(), vc, kLeft);
  }

  touchButton(g, kBadgeBack.x, kBadgeBack.y, kBadgeBack.w, kBadgeBack.h, "< BACK TO LIST", false,
              kAccent);
  touchButton(g, kBadgeSim.x, kBadgeSim.y, kBadgeSim.w, kBadgeSim.h, "SIMULATE TAP", true, kGreen);
}

void BadgeOpsUi::drawAttendance() {
  Arduino_GFX *g = CrowDisplay::canvas();
  text(g, kMargin, 96, "DECISION LOG", fontL(), kTextHi, kLeft);
  char sub[40];
  snprintf(sub, sizeof(sub), "%u recorded", (unsigned)attCount_);
  text(g, 860, 100, sub, fontS(), kTextMut, kRight);

  if (attCount_ == 0) {
    panel(g, kAttList.x, kAttList.y, kAttList.w, 120, 14, kSurface, 1, kLine);
    text(g, kW / 2, kAttList.y + 44, "No decisions yet", fontL(), kTextMut, kCenter);
    text(g, kW / 2, kAttList.y + 76, "Tap a badge on the TAP screen", fontS(), kTextMut, kCenter);
    return;
  }

  const uint8_t vis = visibleAttendanceRows();
  for (uint8_t i = 0; i < vis && (uint8_t)(attScroll_ + i) < attCount_; i++) {
    const AttEntry &e = attAt(attScroll_ + i);
    Rect row = attRow(i);
    const int16_t midY = row.y + (row.h - 16) / 2;
    panel(g, row.x, row.y, row.w, row.h, 10, kSurface, 1, kLine);
    statusDot(g, row.x + 22, row.y + row.h / 2, 7, e.granted ? kGreen : kRed);
    char t[16];
    snprintf(t, sizeof(t), "t+%lus", (unsigned long)(e.ms / 1000));
    text(g, row.x + 44, midY, t, fontS(), kTextMut, kLeft);
    text(g, row.x + 150, midY, e.name, fontM(), kTextHi, kLeft);
    text(g, row.x + 430, midY, e.uid, fontS(), kTextMut, kLeft);
    text(g, row.x + row.w - 20, midY, e.granted ? "GRANTED" : "DENIED", fontS(),
         e.granted ? kGreen : kRed, kRight);
  }

  const bool canUp = attScroll_ > 0;
  const bool canDown = attCount_ > vis && attScroll_ < (uint8_t)(attCount_ - vis);
  touchButton(g, kAttUp.x, kAttUp.y, kAttUp.w, kAttUp.h, "UP", canUp, kAccent);
  touchButton(g, kAttDown.x, kAttDown.y, kAttDown.w, kAttDown.h, "DOWN", canDown, kAccent);
}

void BadgeOpsUi::drawReaders() {
  Arduino_GFX *g = CrowDisplay::canvas();
  for (uint8_t i = 0; i < kReaderCount; i++) {
    const ReaderDesc &d = kReaders[i];
    Rect c = readerCard(i);
    const bool sel = (i == selectedReader_);
    const bool active = (i == activeReader_);
    panel(g, c.x, c.y, c.w, c.h, 14, sel ? kSurfaceHi : kSurface, sel ? 2 : 1,
          sel ? kAccent : kLine);
    text(g, c.x + 20, c.y + 20, d.shortName, fontL(), kTextHi, kLeft);
    text(g, c.x + 20, c.y + 54, d.title, fontS(), kTextMut, kLeft);
    text(g, c.x + 20, c.y + 92, d.transport, fontS(), kTextMut, kLeft);
    if (active) {
      const uint16_t col = (i == 0) ? kAccent : (activeReady_ ? kGreen : kAmber);
      const char *st = (i == 0) ? "ACTIVE - MOCK" : (activeReady_ ? "ACTIVE - READY" : "ACTIVE - WAIT");
      pill(g, c.x + 20, c.y + c.h - 44, st, fontS(), kBg, col);
    } else {
      pill(g, c.x + 20, c.y + c.h - 44, "INACTIVE", fontS(), kTextHi, kLine);
    }
    if (sel) {
      statusDot(g, c.x + c.w - 24, c.y + 24, 6, kAccent);
    }
  }

  const ReaderDesc &d = kReaders[selectedReader_];
  const Rect p = kReaderDetail;
  panel(g, p.x, p.y, p.w, p.h, 16, kSurface, 1, kLine);
  text(g, p.x + 24, p.y + 22, d.title, fontL(), kTextHi, kLeft);
  text(g, p.x + 24, p.y + 60, d.wire1, fontM(), kTextMut, kLeft);
  text(g, p.x + 24, p.y + 92, d.wire2, fontM(), kTextMut, kLeft);

  const char *note;
  if (selectedReader_ == 0) {
    note = "Mock reader drives every screen with no hardware attached.";
  } else if (selectedReader_ == activeReader_) {
    note = activeReady_ ? "Driver compiled and chip answered - not yet field-proven."
                        : "Driver compiled - chip has not answered (no hardware).";
  } else {
    note = "Not the compiled-active reader - enable it with its -DUSE_*_DRIVER flag.";
  }
  text(g, p.x + 24, p.y + 132, note, fontS(), kAmber, kLeft);
}

#endif  // USE_DISPLAY && CONFIG_IDF_TARGET_ESP32P4
