#include "NfcLabUi.h"

// ---------------------------------------------------------------------------
// Serial-parity helpers (compiled in every build; no GFX dependency).
// ---------------------------------------------------------------------------

const char *NfcLabUi::screenName() const {
  switch (screen_) {
    case SCR_SCAN:  return "scan";
    case SCR_NDEF:  return "ndef";
    case SCR_APDU:  return "apdu";
    case SCR_BADGE: return "badge";
    case SCR_FILES: return "files";
    default:        return "scan";
  }
}

void NfcLabUi::showScreen(NfcScreen s) {
  if (s >= SCR_COUNT) return;
  screen_ = s;
  markDirty();
}

void NfcLabUi::printTouch(Print &out) const {
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

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)

#include <Arduino_GFX_Library.h>
#include <DashboardWidgets.h>

using namespace Widgets;

namespace {

constexpr int16_t kW = kChromeW;                 // 1024
constexpr int16_t kContentTop = kChromeHeaderH;  // 72
constexpr int16_t kFooterLineY = 498;

struct Rect { int16_t x, y, w, h; };
bool inRect(const Rect &r, int16_t x, int16_t y) {
  return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

// Interactive controls (release-tested against these).
const Rect kScanBtn      = {664, 400, 336, 84};
const Rect kApduStage    = {24, 150, 976, 230};   // tapping the trace advances
const Rect kApduResetBtn = {24, 428, 180, 56};
const Rect kApduNextBtn  = {214, 428, 220, 56};

// Truncate `s` to fit `maxW` px in `font`, appending an ellipsis if clipped.
String clip(Arduino_GFX *g, const String &s, const GFXfont *font, int16_t maxW) {
  if (textWidth(g, s.c_str(), font) <= maxW) return s;
  String out = s;
  while (out.length() > 1 && textWidth(g, (out + "...").c_str(), font) > maxW) {
    out.remove(out.length() - 1);
  }
  return out + "...";
}

// Draw `s` word-wrapped inside `w`, up to `maxLines`. Returns lines used.
int drawWrapped(Arduino_GFX *g, int16_t x, int16_t y, int16_t w, const String &s,
                const GFXfont *font, uint16_t color, int16_t lineH, int maxLines) {
  int line = 0;
  String cur;
  int i = 0;
  const int n = s.length();
  while (i < n && line < maxLines) {
    const int sp = s.indexOf(' ', i);
    const String tok = (sp < 0) ? s.substring(i) : s.substring(i, sp);
    const String trial = cur.length() ? cur + " " + tok : tok;
    if (cur.length() == 0 || textWidth(g, trial.c_str(), font) <= w) {
      cur = trial;
    } else {
      text(g, x, y + line * lineH, cur.c_str(), font, color, kLeft);
      line++;
      cur = tok;
    }
    if (sp < 0) { i = n; break; }
    i = sp + 1;
  }
  if (line < maxLines && cur.length()) {
    text(g, x, y + line * lineH, cur.c_str(), font, color, kLeft);
    line++;
  }
  return line;
}

String firstWord(const String &s) {
  const int sp = s.indexOf(' ');
  return sp < 0 ? s : s.substring(0, sp);
}

// Short human label for an NDEF payload string ("uri ..." -> "URI").
String ndefKind(const NdefPreview &n) {
  const String head = firstWord(n.payload);
  if (head == "uri")  return "URI";
  if (head == "text") return "TEXT";
  if (head == "ndef") return "NDEF";
  return n.recordType.length() ? "REC" : "-";
}

// The rest of an NDEF payload after its leading kind word.
String ndefBody(const NdefPreview &n) {
  const int sp = n.payload.indexOf(' ');
  return sp < 0 ? n.payload : n.payload.substring(sp + 1);
}

// One read-only Type 4 exchange step. Responses for NLEN/NDEF come from the
// live SafeApduRead so the trace reflects the actual scanned tag.
void apduStepInfo(uint8_t step, const NfcLabState &st, String &name, String &cmd,
                  String &resp, String &note) {
  switch (step) {
    case 0:
      name = "SELECT NDEF APP";
      cmd = "00 A4 04 00 07 D2 76 00 00 85 01 01 00";
      resp = "90 00";
      note = "Select the NFC Forum Type 4 NDEF application by AID.";
      break;
    case 1:
      name = "SELECT CC FILE";
      cmd = "00 A4 00 0C 02 E1 03";
      resp = "90 00";
      note = "Select the capability container file (E103).";
      break;
    case 2:
      name = "READ CC";
      cmd = "00 B0 00 00 0F";
      resp = "00 0F 20 00 3B 00 34 04 06 E1 04 00 FF 00 00 90 00";
      note = "Read the 15-byte CC: NDEF file E104, read access.";
      break;
    case 3:
      name = "SELECT NDEF FILE";
      cmd = "00 A4 00 0C 02 E1 04";
      resp = "90 00";
      note = "Select the NDEF file (E104), public read.";
      break;
    case 4: {
      const uint16_t nlen = st.hasApdu ? st.apdu.ndefLength : 0;
      name = "READ NLEN";
      cmd = "00 B0 00 00 02";
      char r[20];
      snprintf(r, sizeof(r), "%02X %02X 90 00", (nlen >> 8) & 0xFF, nlen & 0xFF);
      resp = r;
      note = "NDEF message length = " + String(nlen) + " bytes.";
      break;
    }
    case 5:
    default: {
      const uint16_t nlen = st.hasApdu ? st.apdu.ndefLength : 0;
      uint16_t shown = nlen;
      if (shown > NFC_LAB_MAX_NDEF_PREVIEW_BYTES) shown = NFC_LAB_MAX_NDEF_PREVIEW_BYTES;
      name = "READ NDEF";
      char c[20];
      snprintf(c, sizeof(c), "00 B0 00 02 %02X", shown & 0xFF);
      cmd = c;
      resp = st.hasApdu ? ("[" + st.apdu.preview + "] 90 00") : "90 00";
      note = "Read a bounded NDEF preview (" + String(shown) + " of " +
             String(nlen) + " bytes).";
      break;
    }
  }
}

}  // namespace

// ---------------------------------------------------------------------------
// Lifecycle + frame loop
// ---------------------------------------------------------------------------

bool NfcLabUi::begin(const char *readerLabel) {
  ready_ = CrowDisplay::begin(activeHardwareProfile(), "NFC FIELD LAB",
                              /*manualFlush=*/true) &&
           CrowDisplay::canvas() != nullptr;
  dirty_ = true;
  Logger::info("nfc-ui", ready_ ? String("touch console ready; reader ") + readerLabel
                                : "display unavailable; serial only");
  return ready_;
}

void NfcLabUi::markDirty() { dirty_ = true; }

NfcLabEvent NfcLabUi::tick(NfcLabState &st) {
  if (!ready_) return EVT_NONE;
  touch_.tick();
  NfcLabEvent ev = handleTouch_(st);
  if (dirty_) {
    draw_(st);
    dirty_ = false;
  }
  return ev;
}

NfcLabEvent NfcLabUi::handleTouch_(NfcLabState &st) {
  (void)st;
  if (!touch_.releasedEdge()) return EVT_NONE;
  const int16_t x = touch_.releaseX();
  const int16_t y = touch_.releaseY();

  // Bottom tab strip switches screens (view-only; no app-state change).
  const int8_t tab = tabHit(x, y, SCR_COUNT);
  if (tab >= 0) {
    if ((NfcScreen)tab != screen_) {
      screen_ = (NfcScreen)tab;
      dirty_ = true;
    }
    return EVT_NONE;
  }

  switch (screen_) {
    case SCR_SCAN:
      if (inRect(kScanBtn, x, y)) { dirty_ = true; return EVT_SCAN_NEXT; }
      break;
    case SCR_APDU:
      if (inRect(kApduResetBtn, x, y)) { dirty_ = true; return EVT_APDU_RESET; }
      if (inRect(kApduNextBtn, x, y) || inRect(kApduStage, x, y)) {
        dirty_ = true;
        return EVT_APDU_STEP;
      }
      break;
    default:
      break;
  }
  return EVT_NONE;
}

void NfcLabUi::draw_(NfcLabState &st) {
  Arduino_GFX *g = CrowDisplay::canvas();
  if (!g) return;
  g->fillScreen(kBg);
  drawChrome_(st);
  switch (screen_) {
    case SCR_SCAN:  drawScan_(st);  break;
    case SCR_NDEF:  drawNdef_(st);  break;
    case SCR_APDU:  drawApdu_(st);  break;
    case SCR_BADGE: drawBadge_(st); break;
    case SCR_FILES: drawFiles_(st); break;
    default: break;
  }
  drawFooter_();
  CrowDisplay::flush();
}

void NfcLabUi::drawChrome_(NfcLabState &st) {
  Arduino_GFX *g = CrowDisplay::canvas();
  char sub[72];
  snprintf(sub, sizeof(sub), "BADGEOPS PRO   reader: %s", st.readerLabel.c_str());
  headerBar(g, "NFC FIELD LAB", sub, "READ-ONLY", kAmber);
  static const char *const kTabs[SCR_COUNT] = {"SCAN", "NDEF", "APDU", "BADGE", "FILES"};
  tabBar(g, kTabs, SCR_COUNT, (uint8_t)screen_, kAccent);
}

void NfcLabUi::drawFooter_() {
  Arduino_GFX *g = CrowDisplay::canvas();
  g->drawFastHLine(24, kFooterLineY, kW - 48, kLine);
  text(g, kW / 2, kFooterLineY + 8,
       "READ-ONLY LAB   SELECT + READ BINARY only   no writes, no payment or proprietary AIDs",
       fontS(), kTextMut, kCenter);
}

// ---------------------------------------------------------------------------
// SCAN
// ---------------------------------------------------------------------------

void NfcLabUi::drawScan_(NfcLabState &st) {
  Arduino_GFX *g = CrowDisplay::canvas();

  // UID hero card.
  panel(g, 24, 84, 616, 300, 16, kSurface, 1, kLine);
  text(g, 48, 108, "TAG UID", fontS(), kTextMut, kLeft);
  if (st.hasTag) {
    text(g, 48, 138, clip(g, st.tag.uid, fontXL(), 560).c_str(), fontXL(), kTextHi, kLeft);
    text(g, 48, 214, clip(g, st.tag.tagType, fontL(), 560).c_str(), fontL(), kAccent, kLeft);

    String tech = st.tag.reader;
    if (st.tag.capacityBytes > 0) tech += "   " + String(st.tag.capacityBytes) + " B usable";
    text(g, 48, 250, "TECHNOLOGY", fontS(), kTextMut, kLeft);
    text(g, 48, 274, clip(g, tech, fontM(), 560).c_str(), fontM(), kTextHi, kLeft);

    text(g, 48, 320, "READ AT", fontS(), kTextMut, kLeft);
    char at[32];
    snprintf(at, sizeof(at), "t+%lus", (unsigned long)(st.tag.readAtMs / 1000));
    text(g, 48, 344, at, fontM(), kTextHi, kLeft);
  } else {
    text(g, 48, 150, "NO TAG", fontXL(), kTextMut, kLeft);
    text(g, 48, 226, "Present a tag or tap SCAN to read the next one.",
         fontM(), kTextMut, kLeft);
  }

  // Active-reader card.
  panel(g, 664, 84, 336, 300, 16, kSurface, 1, kLine);
  text(g, 688, 108, "ACTIVE READER", fontS(), kTextMut, kLeft);
  statusDot(g, 976, 116, 7, st.hasTag ? kGreen : kAmber);
  text(g, 688, 140, clip(g, st.readerLabel, fontL(), 288).c_str(), fontL(), kTextHi, kLeft);
  text(g, 688, 186, st.hasTag ? "TAG PRESENT" : "WAITING", fontS(),
       st.hasTag ? kGreen : kAmber, kLeft);
  text(g, 688, 214, "NDEF preview", fontS(), kTextMut, kLeft);
  text(g, 976, 214, st.ndefSupported ? "yes" : "no", fontS(),
       st.ndefSupported ? kGreen : kTextMut, kRight);
  text(g, 688, 240, "Type 4 APDU", fontS(), kTextMut, kLeft);
  text(g, 976, 240, st.apduSupported ? "yes" : "no", fontS(),
       st.apduSupported ? kGreen : kTextMut, kRight);
  text(g, 688, 300, "Inspection only. UID-only is", fontS(), kTextMut, kLeft);
  text(g, 688, 322, "not secure access control.", fontS(), kTextMut, kLeft);

  // At-a-glance summary strip.
  panel(g, 24, 400, 616, 84, 14, kSurfaceHi, 1, kLine);
  const int16_t cellW = 616 / 3;
  const char *labels[3] = {"NDEF", "BADGE", "FILES"};
  String vals[3];
  vals[0] = !st.ndefSupported ? "UID only" : (st.hasNdef ? ndefKind(st.ndef) : "-");
  if (!st.badgeEvaluated) vals[1] = "-";
  else vals[1] = st.badge.status == "granted" ? "GRANT" : "DENY";
  vals[2] = "3 on-tag";
  uint16_t vcol[3] = {kTextHi, st.badgeEvaluated && st.badge.status == "granted" ? kGreen
                                  : st.badgeEvaluated ? kRed : kTextMut, kTextHi};
  for (int i = 0; i < 3; i++) {
    const int16_t cx = 24 + cellW * i + cellW / 2;
    text(g, cx, 416, labels[i], fontS(), kTextMut, kCenter);
    text(g, cx, 442, clip(g, vals[i], fontL(), cellW - 20).c_str(), fontL(), vcol[i], kCenter);
  }

  // SCAN action button.
  touchButton(g, kScanBtn.x, kScanBtn.y, kScanBtn.w, kScanBtn.h,
              st.hasTag ? "SCAN NEXT TAG" : "SCAN TAG", true, kAccent);
}

// ---------------------------------------------------------------------------
// NDEF
// ---------------------------------------------------------------------------

void NfcLabUi::drawNdef_(NfcLabState &st) {
  Arduino_GFX *g = CrowDisplay::canvas();

  panel(g, 24, 84, 976, 300, 16, kSurface, 1, kLine);
  text(g, 48, 108, "PUBLIC NDEF RECORD", fontS(), kTextMut, kLeft);

  if (!st.ndefSupported) {
    text(g, 48, 170, "Reader is UID-only", fontL(), kAmber, kLeft);
    text(g, 48, 214, "The active reader does not expose a Type 4 NDEF file.",
         fontM(), kTextMut, kLeft);
    text(g, 48, 244, "Enable the PN532 driver or the mock reader to preview NDEF.",
         fontM(), kTextMut, kLeft);
  } else if (!st.hasNdef) {
    text(g, 48, 170, "No record read", fontL(), kTextMut, kLeft);
    text(g, 48, 214, "Scan a tag that carries a public NDEF message.",
         fontM(), kTextMut, kLeft);
  } else {
    // Record-type pill.
    const String kind = ndefKind(st.ndef);
    pill(g, 48, 140, kind.c_str(), fontS(), kBg, kAccent);
    text(g, 48 + textWidth(g, kind.c_str(), fontS()) + 40, 146,
         clip(g, st.ndef.recordType, fontS(), 700).c_str(), fontS(), kTextMut, kLeft);

    text(g, 48, 196, "PAYLOAD", fontS(), kTextMut, kLeft);
    drawWrapped(g, 48, 222, 904, ndefBody(st.ndef), fontL(), kTextHi, 34, 3);

    text(g, 48, 332, "BYTES", fontS(), kTextMut, kLeft);
    text(g, 140, 331, String(st.ndef.byteCount).c_str(), fontM(), kTextHi, kLeft);
    text(g, 280, 332, "SOURCE", fontS(), kTextMut, kLeft);
    text(g, 372, 331, clip(g, st.ndef.source, fontM(), 300).c_str(), fontM(), kTextHi, kLeft);
  }

  // Detail strip: reinforce the read-only nature.
  panel(g, 24, 400, 976, 84, 14, kSurfaceHi, 1, kLine);
  text(g, 48, 420, "DECODED PREVIEW", fontS(), kTextMut, kLeft);
  text(g, 48, 446,
       "Bytes are read and decoded for display only. The lab never writes a record back to the tag.",
       fontS(), kTextMut, kLeft);
}

// ---------------------------------------------------------------------------
// APDU stepper
// ---------------------------------------------------------------------------

void NfcLabUi::drawApdu_(NfcLabState &st) {
  Arduino_GFX *g = CrowDisplay::canvas();

  if (!st.apduSupported) {
    panel(g, 24, 84, 976, 300, 16, kSurface, 1, kLine);
    text(g, 48, 120, "TYPE 4 EXCHANGE UNAVAILABLE", fontS(), kTextMut, kLeft);
    text(g, 48, 176, "Reader is UID-only", fontL(), kAmber, kLeft);
    text(g, 48, 220, "This reader cannot run the Type 4 NDEF read exchange.",
         fontM(), kTextMut, kLeft);
    return;
  }

  uint8_t step = st.apduStep;
  if (step >= kApduStepCount) step = kApduStepCount - 1;
  String name, cmd, resp, note;
  apduStepInfo(step, st, name, cmd, resp, note);

  // Step header + progress dots.
  char hdr[40];
  snprintf(hdr, sizeof(hdr), "STEP %u / %u", step + 1, kApduStepCount);
  text(g, 24, 92, hdr, fontS(), kTextMut, kLeft);
  text(g, 24, 112, name.c_str(), fontL(), kTextHi, kLeft);
  for (uint8_t i = 0; i < kApduStepCount; i++) {
    const int16_t dx = 760 + i * 40;
    if (i <= step) statusDot(g, dx, 108, 8, kAccent);
    else { g->drawCircle(dx, 108, 8, kLine); }
  }

  // Command (C-APDU) panel.
  panel(g, 24, 150, 470, 230, 14, kSurface, 1, kLine);
  text(g, 48, 172, "COMMAND  (C-APDU)", fontS(), kTextMut, kLeft);
  drawWrapped(g, 48, 210, 422, cmd, fontM(), kAccent, 30, 3);
  drawWrapped(g, 48, 316, 422, note, fontS(), kTextMut, 20, 3);

  // Response (R-APDU) panel.
  panel(g, 530, 150, 470, 230, 14, kSurface, 1, kLine);
  text(g, 554, 172, "RESPONSE  (R-APDU)", fontS(), kTextMut, kLeft);
  drawWrapped(g, 554, 210, 422, resp, fontM(), kTextHi, 30, 4);
  text(g, 976, 172, "SW 90 00", fontS(), kGreen, kRight);

  // Reader-provided trace (proves the panel mirrors the driver output).
  if (st.hasApdu && st.apdu.trace.length()) {
    text(g, 214 + 220 + 16, 440, clip(g, "trace: " + st.apdu.trace, fontS(), 340).c_str(),
         fontS(), kTextMut, kLeft);
  }

  // Controls.
  touchButton(g, kApduResetBtn.x, kApduResetBtn.y, kApduResetBtn.w, kApduResetBtn.h,
              "RESET", false, kAccent);
  touchButton(g, kApduNextBtn.x, kApduNextBtn.y, kApduNextBtn.w, kApduNextBtn.h,
              step + 1 < kApduStepCount ? "NEXT STEP" : "RESTART", true, kAccent);
}

// ---------------------------------------------------------------------------
// BADGE decision
// ---------------------------------------------------------------------------

void NfcLabUi::drawBadge_(NfcLabState &st) {
  Arduino_GFX *g = CrowDisplay::canvas();

  // Verdict banner colored by decision.
  uint16_t vcol = kTextMut;
  const char *verdict = "NO DECISION";
  if (st.badgeEvaluated) {
    if (st.badge.status == "granted") { vcol = kGreen; verdict = "ACCESS GRANTED"; }
    else if (st.badgeMatched)         { vcol = kAmber; verdict = "ACCESS DENIED"; }
    else                              { vcol = kRed;   verdict = "ACCESS DENIED"; }
  }
  panel(g, 24, 84, 976, 120, 16, kSurfaceHi, 2, vcol);
  statusDot(g, 72, 144, 16, vcol);
  text(g, 108, 116, verdict, fontXL(), vcol, kLeft);
  if (st.badgeEvaluated) {
    text(g, 976, 122, clip(g, st.badge.summary, fontL(), 520).c_str(), fontL(), kTextHi, kRight);
  }

  // Details card.
  panel(g, 24, 220, 976, 264, 16, kSurface, 1, kLine);
  text(g, 48, 244, "DECIDING ON UID", fontS(), kTextMut, kLeft);
  text(g, 48, 272, st.hasTag ? clip(g, st.tag.uid, fontL(), 500).c_str() : "-",
       fontL(), kTextHi, kLeft);

  if (!st.badgeEvaluated) {
    text(g, 48, 330, "Scan a tag, then open BADGE to evaluate it.", fontM(), kTextMut, kLeft);
  } else if (st.badgeMatched) {
    struct Row { const char *label; String value; } rows[] = {
      {"NAME",   st.badgeRecord.name},
      {"ROLE",   st.badgeRecord.role},
      {"STATUS", st.badgeRecord.status},
      {"ZONES",  st.badgeRecord.allowedZones},
      {"BADGE",  st.badgeRecord.badgeId},
    };
    for (int i = 0; i < 5; i++) {
      const int16_t ry = 322 + i * 32;
      text(g, 48, ry, rows[i].label, fontS(), kTextMut, kLeft);
      uint16_t rc = kTextHi;
      if (i == 2) rc = (st.badgeRecord.status == "active") ? kGreen : kAmber;
      text(g, 180, ry - 1, clip(g, rows[i].value, fontM(), 780).c_str(), fontM(), rc, kLeft);
    }
  } else {
    text(g, 48, 322, "UID is not in the demo registry.", fontL(), kRed, kLeft);
    drawWrapped(g, 48, 364, 904, st.badge.detail, fontM(), kTextMut, 30, 2);
  }

  text(g, 48, 452, "Demo policy only. A UID match is identification, not authentication.",
       fontS(), kTextMut, kLeft);
}

// ---------------------------------------------------------------------------
// FILES / applications
// ---------------------------------------------------------------------------

void NfcLabUi::drawFiles_(NfcLabState &st) {
  Arduino_GFX *g = CrowDisplay::canvas();

  panel(g, 24, 84, 976, 400, 16, kSurface, 1, kLine);

  text(g, 48, 106, "ON-TAG APPLICATION & FILES", fontS(), kTextMut, kLeft);
  struct FileRow { const char *name; const char *id; String meta; uint16_t col; };
  const bool t4 = st.apduSupported;
  const uint16_t okCol = t4 ? kGreen : kTextMut;
  const String ndefMeta = (t4 && st.hasApdu) ? String(st.apdu.ndefLength) + " B  read-only"
                                             : String("read-only");
  FileRow tagRows[] = {
    {"NDEF Application", "AID D2 76 00 00 85 01 01", t4 ? String("selected") : String("n/a"),
      t4 ? kAccent : kTextMut},
    {"Capability Container", "File E103", t4 ? String("15 B  read") : String("n/a"), okCol},
    {"NDEF File", "File E104", ndefMeta, okCol},
  };
  for (int i = 0; i < 3; i++) {
    const int16_t ry = 130 + i * 46;
    panel(g, 48, ry, 928, 40, 8, kSurfaceHi, 0, kLine);
    statusDot(g, 68, ry + 20, 5, tagRows[i].col);
    text(g, 88, ry + 12, tagRows[i].name, fontM(), kTextHi, kLeft);
    text(g, 420, ry + 14, tagRows[i].id, fontS(), kTextMut, kLeft);
    text(g, 960, ry + 14, tagRows[i].meta.c_str(), fontS(), tagRows[i].col, kRight);
  }

  text(g, 48, 288, "EXPORTED ARTIFACTS  (mock SD)", fontS(), kTextMut, kLeft);
  struct Artifact { const char *name; const char *meta; };
  Artifact arts[] = {
    {"scanlog.csv", "append-only"},
    {"dmp001.json", "UID + tech"},
    {"ndef_url.txt", "decoded payload"},
    {"audit.txt", "decisions"},
  };
  for (int i = 0; i < 4; i++) {
    const int16_t col = i % 2;
    const int16_t row = i / 2;
    const int16_t rx = 48 + col * 468;
    const int16_t ry = 312 + row * 46;
    panel(g, rx, ry, 448, 40, 8, kSurfaceHi, 0, kLine);
    text(g, rx + 16, ry + 12, arts[i].name, fontM(), kTextHi, kLeft);
    text(g, rx + 432, ry + 14, arts[i].meta, fontS(), kTextMut, kRight);
  }

  text(g, 48, 452, "Files are enumerated read-only; the lab creates no records and writes nothing back.",
       fontS(), kTextMut, kLeft);
}

#else  // ---------------- non-display stubs (headless build) ----------------

bool NfcLabUi::begin(const char *readerLabel) {
  Logger::info("nfc-ui", String("serial-only build; reader ") + readerLabel);
  return false;
}
void NfcLabUi::markDirty() {}
NfcLabEvent NfcLabUi::tick(NfcLabState &) { return EVT_NONE; }

#endif  // USE_DISPLAY && CONFIG_IDF_TARGET_ESP32P4
