#include "GbUi.h"

#include "GbInput.h"
#include "GbTheme.h"
#include "GameBoyHost.h"
#include "GbVideo.h"
#include <CrowPanelShared.h>

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
#include <Arduino_GFX_Library.h>
#endif

namespace {
// The palette every draw call reads. Held here rather than passed through
// every signature: the theme changes rarely and a repaint always follows.
GbThemeId gActiveTheme = kGbThemeOpsTeal;
}  // namespace

bool GbUi::systemPlayable(EmuSystem sys) {
  switch (sys) {
    case kSysGameBoy: return true;
#if USE_GENESIS_CORE
    case kSysGenesis: return true;
#endif
#if USE_NES_CORE
    case kSysNes: return true;
#endif
    default: return false;
  }
}

void GbUi::setStateSource(const GbRomStore *roms, const int8_t *activeRom) {
  stateRoms_ = roms;
  stateRomIdx_ = activeRom;
}

void GbUi::setTheme(GbThemeId id) { gActiveTheme = id; }
GbThemeId GbUi::theme() { return gActiveTheme; }
const GbPalette &GbUi::palette() { return gbTheme(gActiveTheme); }

// Strip the extension and any trailing "(USA, Europe) (SGB Enhanced)" style
// region/feature tags, which push real ROM dumps well past the header width.
String GbUi::prettyTitle(const String &fileName) {
  String s = fileName;
  int dot = s.lastIndexOf('.');
  if (dot > 0) s = s.substring(0, dot);
  int paren = s.indexOf(" (");
  if (paren > 0) s = s.substring(0, paren);
  s.trim();
  return s.length() ? s : fileName;
}

int8_t GbUi::pickerHit(int16_t px, int16_t py, uint8_t romCount) {
  if (romCount == 0) return -1;
  if (px < kRowX || px >= (int16_t)(kRowX + kRowW)) return -1;
  if (py < kRowTop) return -1;
  const int16_t vis = (py - kRowTop) / kRowH;
  // Visible row within the current page; the caller adds page * kRowsPerPage.
  if (vis < 0 || vis >= (int16_t)kRowsPerPage) return -1;
  return (int8_t)vis;
}

bool GbUi::begin() {
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  // manualFlush: draws only touch the cached framebuffer until flush(), which
  // turns Arduino_GFX's per-pixel cache sync into one sync per frame.
  ready_ = CrowDisplay::begin(activeHardwareProfile(), "Cypher Boy", true);
  if (!ready_) Logger::error("gbui", "display bring-up failed");
  return ready_;
#else
  ready_ = false;
  return false;
#endif
}

namespace {
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
// Tiny cartridge glyph for a ROM row - the clipped corner reads as a Game Boy
// cart even at 18x22. Tinted differently for .gbc so colour titles stand out.
void drawCartIcon(Arduino_GFX *g, int16_t x, int16_t y, EmuSystem sys,
                  bool playable, const GbPalette &P) {
  const uint16_t body = !playable ? P.line : (sys == kSysGameBoy ? P.accent : P.success);
  g->fillRoundRect(x, y, 18, 22, 3, body);
  g->fillTriangle(x + 12, y, x + 18, y, x + 18, y + 6, P.surface);
  g->fillRect(x + 4, y + 5, 10, 8, P.surface);
}
#endif
}  // namespace

int8_t GbUi::pagerHit(int16_t px, int16_t py) {
  if (py < kPagerY || py >= kPagerY + kPagerH) return -1;
  if (px >= kRowX && px < kRowX + kPagerW) return 0;
  const int16_t nx = kRowX + kRowW - kPagerW;
  if (px >= nx && px < nx + kPagerW) return 1;
  return -1;
}

void GbUi::drawPicker(const GbRomStore &roms, int8_t selectedRow,
                      const uint8_t *order, const String *played,
                      uint8_t page) {
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  Arduino_GFX *g = CrowDisplay::canvas();
  if (!g || !ready_) return;
  using namespace Widgets;
  const GbPalette &P = GbUi::palette();

  g->fillScreen(P.bg);
  headerBar(g, "Cypher Boy", "Game Boy / GBC player",
            roms.ready() ? "SD READY" : "NO SD", roms.ready() ? P.success : P.warning);

  if (roms.count() == 0) {
    text(g, kRowX, kRowTop + 8, "No ROMs found.", fontL(), P.ink, kLeft);
    text(g, kRowX, kRowTop + 44, (String("Put .gb / .gbc files in ") + GB_ROM_DIR).c_str(),
         fontS(), P.muted, kLeft);
  } else {
    const uint8_t first = page * kRowsPerPage;
    for (uint8_t vis = 0; vis < kRowsPerPage; vis++) {
      const uint8_t row = first + vis;
      if (row >= roms.count()) break;
      const uint8_t idx = order ? order[row] : row;
      const int16_t y = kRowTop + vis * kRowH;
      const bool on = (int8_t)row == selectedRow;
      panel(g, kRowX, y, kRowW, kRowH - 8, 10, on ? P.surfaceHi : P.surface, 1,
            on ? P.accent : P.line);

      String n = roms.name(idx);
      const EmuSystem sys = roms.systemOf(idx);
      const bool playable = systemPlayable(sys);
      drawCartIcon(g, kRowX + 14, y + 16, sys, playable, P);

      text(g, kRowX + 44, y + 10, prettyTitle(n).c_str(), fontM(),
           !playable ? P.line : (on ? P.ink : P.muted), kLeft);
      // System badge, so a mixed library reads at a glance.
      text(g, kRowX + kRowW - 92, y + 18, GbRomStore::systemLabel(sys), fontS(),
           playable ? P.accent : P.line, kLeft);
      if (played) {
        text(g, kRowX + kRowW - 108, y + 18, played[row].c_str(), fontS(),
             on ? P.accent : P.muted, kRight);
      }
    }
  }

  // Pager. Always drawn so the control does not appear and vanish as the
  // library grows past one screenful.
  {
    const uint8_t pages = pageCount(roms.count());
    const bool canPrev = page > 0, canNext = (uint8_t)(page + 1) < pages;
    touchButton(g, kRowX, kPagerY, kPagerW, kPagerH, "< PREV", false,
                canPrev ? P.accent : P.line);
    touchButton(g, kRowX + kRowW - kPagerW, kPagerY, kPagerW, kPagerH, "NEXT >", false,
                canNext ? P.accent : P.line);
    char buf[32];
    snprintf(buf, sizeof(buf), "page %u/%u  -  %u ROMs", (unsigned)(page + 1),
             (unsigned)pages, (unsigned)roms.count());
    text(g, kRowX + kRowW / 2, kPagerY + 14, buf, fontS(), P.muted, kCenter);
  }

  // Right column: a short hint, then the live settings block. drawSettings()
  // repaints just the settings card so a +/- tap does not redraw the ROM list.
  text(g, kSetX, kRowTop + 6, "Tap a ROM to play", fontS(), P.muted, kLeft);
  text(g, kSetX, kRowTop + 32, "MENU pauses in-game", fontS(), P.muted, kLeft);

  if (!roms.ready()) {
    text(g, kRowX, 560, roms.status().c_str(), fontS(), P.warning, kLeft);
  }

  CrowDisplay::flush();
  lastHeld_ = 0;
#else
  (void)roms;
  (void)selectedRow;
  (void)order;
  (void)played;
  (void)page;
#endif
}

namespace {
// --- Pause overlay geometry -------------------------------------------------
// Deliberately OUTSIDE the display guard: the hit-testers are pure maths and
// must stay verifiable by the headless selftest, exactly like pickerHit() and
// GbInput::mapPoint().
//
// Two columns: save-state slot cards (with thumbnails) on the left, actions on
// the right, and the volume/brightness steppers full-width along the bottom.
const int16_t kOvX = 212, kOvY = 36, kOvW = 600, kOvH = 528;

// Left column: three slot cards, then SAVE / LOAD beneath them.
const int16_t kColL = kOvX + 28;
const int16_t kSlotCardW = 100, kSlotCardH = 100, kSlotPitch = 110;
const int16_t kSlotCardY = kOvY + 84;
const int16_t kSaveBtnY = kOvY + 196, kSaveBtnW = 155, kSaveBtnH = 50;

// Right column: the action stack.
const int16_t kColR = 590, kColRW = 194, kColRH = 46;
const int16_t kRowStep = 54;
const int16_t kRowResume = kOvY + 56;
const int16_t kRowQuit   = kOvY + 56 + kRowStep;
const int16_t kRowFF     = kOvY + 56 + kRowStep * 2;
const int16_t kRowSound  = kOvY + 56 + kRowStep * 3;
const int16_t kRowTheme  = kOvY + 56 + kRowStep * 4;

// Full-width steppers.
const int16_t kOvStepX = kOvX + 28, kOvStepW = kOvW - 56;
const int16_t kOvVolY = kOvY + 336;
const int16_t kOvBrightY = kOvY + 412;

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)

// Draw the D-pad as one continuous plus, not four disconnected squares - the
// four hitboxes form a cross with a hole in the middle, so the body is drawn
// from two crossing bars and the arms are highlighted individually.
void drawDpadBody(Arduino_GFX *g, uint32_t held) {
  using namespace Widgets;
  const GbPalette &P = GbUi::palette();
  int16_t dx, dy, dw, dh;
  GbInput::dpadBounds(dx, dy, dw, dh);
  const int16_t arm = dw / 3;

  // Two crossing bars so the pad reads as one continuous piece rather than
  // four floating squares.
  panel(g, dx + arm, dy, arm, dh, 14, P.surfaceHi, 1, P.line);
  panel(g, dx, dy + arm, dw, arm, 14, P.surfaceHi, 1, P.line);
  g->fillRect(dx + arm + 1, dy + arm + 1, arm - 2, arm - 2, P.surfaceHi);

  uint8_t n;
  const GbHitbox *L = GbInput::layout(n);
  for (uint8_t i = 0; i < n; i++) {
    if (L[i].shape != kCtlArm) continue;
    if (held & L[i].bit) {
      panel(g, L[i].x + 4, L[i].y + 4, L[i].w - 8, L[i].h - 8, 10, P.accent);
    }
  }
  g->fillCircle(dx + dw / 2, dy + dh / 2, arm / 5, P.line);
}

void drawRoundBtn(Arduino_GFX *g, int16_t cx, int16_t cy, int16_t r,
                  const char *label, bool on) {
  using namespace Widgets;
  const GbPalette &P = GbUi::palette();
  g->fillCircle(cx, cy, r, on ? P.accent : P.surfaceHi);
  g->drawCircle(cx, cy, r, on ? P.accent : P.line);
  g->drawCircle(cx, cy, r - 1, on ? P.accent : P.line);
  text(g, cx, cy - 11, label, fontL(), on ? P.bg : P.ink, kCenter);
}

// label + value on one line, [-] [bar] [+] on the next. Compact enough for the
// 300px picker card and the 500px overlay alike.
void drawStepper(Arduino_GFX *g, int16_t x, int16_t y, int16_t w,
                 const char *label, uint8_t value, bool dimmed) {
  using namespace Widgets;
  const GbPalette &P = GbUi::palette();
  const uint16_t ink = dimmed ? P.muted : P.ink;
  text(g, x, y, label, fontS(), ink, kLeft);
  char buf[8];
  snprintf(buf, sizeof(buf), "%d%%", (int)((value * 100 + 127) / 255));
  text(g, x + w, y, buf, fontS(), dimmed ? P.muted : P.accent, kRight);

  const int16_t by = y + 26, bh = 40;
  g->fillRoundRect(x, by, 44, bh, 8, P.surfaceHi);
  g->drawRoundRect(x, by, 44, bh, 8, P.line);
  text(g, x + 22, by + 12, "-", fontL(), ink, kCenter);

  g->fillRoundRect(x + w - 44, by, 44, bh, 8, P.surfaceHi);
  g->drawRoundRect(x + w - 44, by, 44, bh, 8, P.line);
  text(g, x + w - 22, by + 12, "+", fontL(), ink, kCenter);

  const int16_t barX = x + 54, barW = w - 108;
  hBar(g, barX, by + 14, barW, 12, value / 255.0f, dimmed ? P.muted : P.accent);
}

void drawPill(Arduino_GFX *g, int16_t x, int16_t y, int16_t w, int16_t h,
              const char *label, bool on) {
  using namespace Widgets;
  const GbPalette &P = GbUi::palette();
  g->fillRoundRect(x, y, w, h, h / 2, on ? P.accent : P.surfaceHi);
  g->drawRoundRect(x, y, w, h, h / 2, on ? P.accent : P.line);
  text(g, x + w / 2, y + (h - 14) / 2, label, fontS(), on ? P.bg : P.ink, kCenter);
}
#endif
}  // namespace

void GbUi::drawSettings(uint8_t volume, uint8_t brightness, bool muted) {
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  Arduino_GFX *g = CrowDisplay::canvas();
  if (!g || !ready_) return;
  using namespace Widgets;
  const GbPalette &P = GbUi::palette();

  const int16_t h = 44 + (kStepH + 12) * 2 + 44;
  panel(g, kSetX - 16, kSetY - 16, kSetW + 32, h + 20, 12, P.surface, 1, P.line);
  text(g, kSetX, kSetY, "Settings", fontL(), P.ink, kLeft);

  drawStepper(g, kSetX, volRowY(), kSetW, muted ? "VOLUME (MUTED)" : "VOLUME",
              volume, muted);
  drawStepper(g, kSetX, brightRowY(), kSetW, "BRIGHTNESS", brightness, false);

  const int16_t ty = themeRowY();
  text(g, kSetX, ty + 12, "THEME", fontS(), P.muted, kLeft);
  drawPill(g, kSetX + 96, ty, kSetW - 96, 44, P.name, false);

  CrowDisplay::flush(kSetX - 20, kSetY - 20, kSetW + 40, h + 28);
#else
  (void)volume; (void)brightness; (void)muted;
#endif
}

void GbUi::drawPlayChrome(const String &title, bool soundOn, bool fastForward) {
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  Arduino_GFX *g = CrowDisplay::canvas();
  if (!g || !ready_) return;
  using namespace Widgets;
  const GbPalette &P = GbUi::palette();

  g->fillScreen(P.bg);
  headerBar(g, prettyTitle(title).c_str(), fastForward ? "running - FAST FORWARD" : "running",
            soundOn ? "SOUND" : "MUTED", soundOn ? P.success : P.muted);

  drawDpadBody(g, 0);

  uint8_t n;
  const GbHitbox *L = GbInput::layout(n);
  for (uint8_t i = 0; i < n; i++) {
    const GbHitbox &b = L[i];
    if (b.shape == kCtlRound) {
      drawRoundBtn(g, b.x + b.w / 2, b.y + b.h / 2, b.w / 2, b.label, false);
    } else if (b.shape == kCtlPill) {
      drawPill(g, b.x, b.y, b.w, b.h, b.label, false);
    }
  }

  CrowDisplay::flush();
  lastHeld_ = 0;
#else
  (void)title; (void)soundOn; (void)fastForward;
#endif
}

void GbUi::drawSlotThumb(Arduino_GFX *g, int16_t x, int16_t y, uint8_t slot) {
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  using namespace Widgets;
  const GbPalette &P = GbUi::palette();
  const int16_t tw = GameBoyHost::kThumbW, th = GameBoyHost::kThumbH;
  const int16_t dx = x + (kSlotCardW - tw * 2) / 2, dy = y + 8;

  static uint16_t thumb[GameBoyHost::kThumbW * GameBoyHost::kThumbH];
  bool have = false;
  if (stateRoms_ && stateRomIdx_) {
    have = GameBoyHost::loadThumb(stateRoms_->statePath(*stateRomIdx_, slot), thumb);
  }
  if (!have) {
    g->fillRect(dx, dy, tw * 2, th * 2, P.bg);
    g->drawRect(dx, dy, tw * 2, th * 2, P.line);
    text(g, x + kSlotCardW / 2, dy + th - 8, "EMPTY", fontS(), P.muted, kCenter);
    return;
  }
  // Blit at 2x by widening one row at a time - an 80x72 buffer would be 11 KB
  // of internal SRAM for something only drawn while paused.
  uint16_t row[GameBoyHost::kThumbW * 2];
  for (int16_t sy = 0; sy < th; sy++) {
    const uint16_t *src = thumb + (size_t)sy * tw;
    for (int16_t sx = 0; sx < tw; sx++) {
      row[sx * 2] = src[sx];
      row[sx * 2 + 1] = src[sx];
    }
    g->draw16bitRGBBitmap(dx, dy + sy * 2, row, tw * 2, 1);
    g->draw16bitRGBBitmap(dx, dy + sy * 2 + 1, row, tw * 2, 1);
  }
  g->drawRect(dx - 1, dy - 1, tw * 2 + 2, th * 2 + 2, P.line);
#else
  (void)g; (void)x; (void)y; (void)slot;
#endif
}

void GbUi::drawPauseOverlay(uint8_t slot, bool soundOn, bool fastForward,
                            uint8_t volume, uint8_t brightness) {
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  Arduino_GFX *g = CrowDisplay::canvas();
  if (!g || !ready_) return;
  using namespace Widgets;
  const GbPalette &P = GbUi::palette();

  panel(g, kOvX, kOvY, kOvW, kOvH, 16, P.surface, 2, P.accent);
  text(g, kColL, kOvY + 16, "Paused", fontL(), P.ink, kLeft);
  text(g, kColL, kOvY + 60, "SAVE STATES", fontS(), P.muted, kLeft);

  // Slot cards, each showing the thumbnail captured when that state was saved.
  for (uint8_t i = 0; i < GB_STATE_SLOTS; i++) {
    const int16_t x = kColL + i * kSlotPitch;
    const bool on = (i == slot);
    panel(g, x, kSlotCardY, kSlotCardW, kSlotCardH, 10,
          on ? P.surfaceHi : P.bg, on ? 2 : 1, on ? P.accent : P.line);
    drawSlotThumb(g, x, kSlotCardY, i);
    char lbl[4];
    snprintf(lbl, sizeof(lbl), "%u", (unsigned)i);
    text(g, x + kSlotCardW / 2, kSlotCardY + kSlotCardH - 20, lbl, fontS(),
         on ? P.accent : P.muted, kCenter);
  }

  drawPill(g, kColL, kSaveBtnY, kSaveBtnW, kSaveBtnH, "SAVE", false);
  drawPill(g, kColL + kSaveBtnW + 10, kSaveBtnY, kSaveBtnW, kSaveBtnH, "LOAD", false);

  drawPill(g, kColR, kRowResume, kColRW, kColRH, "RESUME", false);
  drawPill(g, kColR, kRowQuit, kColRW, kColRH, "QUIT TO LIST", false);
  drawPill(g, kColR, kRowFF, kColRW, kColRH,
           fastForward ? "FAST FWD: ON" : "FAST FWD: OFF", fastForward);
  drawPill(g, kColR, kRowSound, kColRW, kColRH,
           soundOn ? "SOUND: ON" : "SOUND: OFF", soundOn);
  drawPill(g, kColR, kRowTheme, kColRW, kColRH, P.name, false);

  drawStepper(g, kOvStepX, kOvVolY, kOvStepW, "VOLUME", volume, !soundOn);
  drawStepper(g, kOvStepX, kOvBrightY, kOvStepW, "BRIGHTNESS", brightness, false);

  CrowDisplay::flush(kOvX - 4, kOvY - 4, kOvW + 8, kOvH + 8);
#else
  (void)slot; (void)soundOn; (void)fastForward; (void)volume; (void)brightness;
#endif
}

GbUi::OverlayAction GbUi::overlayHit(int16_t px, int16_t py) {
  auto inR = [&](int16_t y) {
    return px >= kColR && px < kColR + kColRW && py >= y && py < y + kColRH;
  };
  if (inR(kRowResume)) return kOvResume;
  if (inR(kRowQuit)) return kOvQuit;
  if (inR(kRowFF)) return kOvToggleFF;
  if (inR(kRowSound)) return kOvToggleSound;
  if (inR(kRowTheme)) return kOvNextTheme;

  if (py >= kSaveBtnY && py < kSaveBtnY + kSaveBtnH) {
    if (px >= kColL && px < kColL + kSaveBtnW) return kOvSaveState;
    const int16_t lx = kColL + kSaveBtnW + 10;
    if (px >= lx && px < lx + kSaveBtnW) return kOvLoadState;
  }
  return kOvNone;
}

bool GbUi::themeRowHit(int16_t px, int16_t py) {
  const int16_t y = themeRowY();
  return px >= kSetX && px < kSetX + kSetW && py >= y && py < y + 44;
}

GbUi::StepHit GbUi::stepperHit(int16_t px, int16_t py, int16_t x, int16_t y, int16_t w) {
  const int16_t by = y + 26, bh = 40;
  if (py < by || py >= by + bh) return kStepNone;
  if (px >= x && px < x + 44) return kStepMinus;
  if (px >= x + w - 44 && px < x + w) return kStepPlus;
  return kStepNone;
}

GbUi::StepHit GbUi::overlayVolHit(int16_t px, int16_t py) {
  return stepperHit(px, py, kOvStepX, kOvVolY, kOvStepW);
}

GbUi::StepHit GbUi::overlayBrightHit(int16_t px, int16_t py) {
  return stepperHit(px, py, kOvStepX, kOvBrightY, kOvStepW);
}

uint8_t GbUi::overlaySlotHit(int16_t px, int16_t py) {
  if (py < kSlotCardY || py >= kSlotCardY + kSlotCardH) return 0xFF;
  for (uint8_t i = 0; i < GB_STATE_SLOTS; i++) {
    const int16_t x = kColL + i * kSlotPitch;
    if (px >= x && px < x + kSlotCardW) return i;
  }
  return 0xFF;
}

void GbUi::drawPlayStatus(const String &msg) {
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  Arduino_GFX *g = CrowDisplay::canvas();
  if (!g || !ready_) return;
  using namespace Widgets;
  const GbPalette &P = GbUi::palette();
  // Right side of the header, clear of the title.
  // Parameter is `msg`, not `text`: Widgets::text() is the draw call and a
  // parameter of that name shadows it.
  const int16_t x = 560, y = 40, w = 1024 - 560 - 150, h = 26;
  g->fillRect(x, y, w, h, P.surface);
  text(g, x + w, y + 4, msg.c_str(), fontS(), P.muted, kRight);
  CrowDisplay::flush(x, y, w, h);
#else
  (void)msg;
#endif
}

void GbUi::drawNotice(const String &msg) {
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  Arduino_GFX *g = CrowDisplay::canvas();
  if (!g || !ready_) return;
  using namespace Widgets;
  const GbPalette &P = GbUi::palette();
  const int16_t y = kPagerY + kPagerH + 8, h = 40;
  panel(g, kRowX, y, kRowW, h, 8, P.surfaceHi, 1, P.warning);
  text(g, kRowX + 14, y + 11, msg.c_str(), fontS(), P.warning, kLeft);
  CrowDisplay::flush(kRowX, y, kRowW, h);
#else
  (void)msg;
#endif
}

void GbUi::drawButtonState(uint32_t heldBits) {
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  Arduino_GFX *g = CrowDisplay::canvas();
  if (!g || !ready_) return;
  if (heldBits == lastHeld_) return;  // only repaint controls that changed

  uint8_t n;
  const GbHitbox *L = GbInput::layout(n);

  // The D-pad is one drawing, so any arm change repaints the whole cross.
  const uint32_t dpadMask = GB_BTN_UP | GB_BTN_DOWN | GB_BTN_LEFT | GB_BTN_RIGHT;
  if ((heldBits & dpadMask) != (lastHeld_ & dpadMask)) {
    int16_t dx, dy, dw, dh;
    GbInput::dpadBounds(dx, dy, dw, dh);
    drawDpadBody(g, heldBits);
    CrowDisplay::flush(dx, dy, dw, dh);
  }

  for (uint8_t i = 0; i < n; i++) {
    const GbHitbox &b = L[i];
    if (b.shape == kCtlArm || b.bit == 0) continue;  // arms handled above, MENU never lights
    const bool wasOn = (lastHeld_ & b.bit) != 0;
    const bool isOn = (heldBits & b.bit) != 0;
    if (wasOn == isOn) continue;
    if (b.shape == kCtlRound) {
      drawRoundBtn(g, b.x + b.w / 2, b.y + b.h / 2, b.w / 2, b.label, isOn);
    } else {
      drawPill(g, b.x, b.y, b.w, b.h, b.label, isOn);
    }
    CrowDisplay::flush(b.x, b.y, b.w, b.h);
  }
  lastHeld_ = heldBits;
#else
  (void)heldBits;
#endif
}
