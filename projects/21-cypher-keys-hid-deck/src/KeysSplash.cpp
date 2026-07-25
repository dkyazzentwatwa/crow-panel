#include "KeysSplash.h"

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)

#include <CrowPanelShared.h>
#include <Arduino_GFX_Library.h>
#include <DashboardWidgets.h>
#include "KeysLayout.h"

namespace {

using KeysLayout::kScreenW;

// Phase lengths, ms. Total ~1.15 s of animation: enough to register as an
// intro, short enough that nobody waits for it before typing.
constexpr uint16_t kFadeMs = 340;
constexpr uint16_t kCapsMs = 460;
constexpr uint16_t kHoldMs = 170;
constexpr uint8_t kFrameMs = 16;  // ~60 fps pacing

constexpr int16_t kTitleY = 170;      // wordmark, fontXL
constexpr int16_t kTaglineY = 228;    // "T O U C H   H I D   D E C K", fontS
constexpr int16_t kSubtitleY = 268;   // caller's line (HID backend mode), fontS
// The block the fade phase repaints (all three lines, nothing else).
constexpr int16_t kTitleBandY = kTitleY - 10;                  // 160
constexpr int16_t kTitleBandH = kSubtitleY + 26 - kTitleBandY;  // ..294

// Keycap row: the wordmark spelled out as ten switch caps.
constexpr uint8_t kCapCount = 10;
constexpr int16_t kCapW = 76, kCapH = 68, kCapGap = 10;
constexpr int16_t kCapRowW = kCapCount * kCapW + (kCapCount - 1) * kCapGap;  // 850
constexpr int16_t kCapX0 = (kScreenW - kCapRowW) / 2;  // 87
constexpr int16_t kCapY = 320;
constexpr int16_t kRuleY = kCapY + kCapH + 22;  // 410
const char kCapGlyphs[kCapCount + 1] = "CYPHERKEYS";

inline int16_t capX(uint8_t i) { return kCapX0 + i * (kCapW + kCapGap); }

// Mix two RGB565 colors: t=0 gives a, t=255 gives b. Channels blend in their
// native widths (5/6/5), exact enough for a fade and far cheaper than a
// round-trip through 8-bit RGB.
uint16_t blend565(uint16_t a, uint16_t b, uint8_t t) {
  int16_t ar = (a >> 11) & 0x1F, ag = (a >> 5) & 0x3F, ab = a & 0x1F;
  int16_t br = (b >> 11) & 0x1F, bg = (b >> 5) & 0x3F, bb = b & 0x1F;
  int16_t r = ar + (((br - ar) * t) >> 8);
  int16_t g = ag + (((bg - ag) * t) >> 8);
  int16_t bl = ab + (((bb - ab) * t) >> 8);
  return (uint16_t)((r << 11) | (g << 5) | bl);
}

void drawTitle(Arduino_GFX *g, const DeckTheme &t, uint8_t level,
               const char *subtitle) {
  Widgets::text(g, kScreenW / 2, kTitleY, "CYPHER KEYS", Widgets::fontXL(),
                blend565(t.bg, t.ink, level), Widgets::kCenter);
  Widgets::text(g, kScreenW / 2, kTaglineY, "T O U C H   H I D   D E C K",
                Widgets::fontS(), blend565(t.bg, t.accent, level),
                Widgets::kCenter);
  if (subtitle != nullptr && subtitle[0] != '\0') {
    Widgets::text(g, kScreenW / 2, kSubtitleY, subtitle, Widgets::fontS(),
                  blend565(t.bg, t.muted, level), Widgets::kCenter);
  }
}

// One keycap. `lit` is the moment it lands (accent fill, dark legend); it
// settles into the keyboard's own key fill as the next cap comes down, so the
// row ends up looking exactly like the keyboard the UI is about to draw.
void drawCap(Arduino_GFX *g, const DeckTheme &t, uint8_t i, bool lit) {
  int16_t x = capX(i);
  char label[2] = {kCapGlyphs[i], '\0'};
  g->fillRoundRect(x, kCapY, kCapW, kCapH, 10, lit ? t.accent : t.keyFill);
  g->drawRoundRect(x, kCapY, kCapW, kCapH, 10, lit ? t.accent : t.line);
  // fontL caps are ~17 px tall, so this top offset centers the legend in the
  // 68 px cap rather than sitting it high like a keyboard key legend would.
  Widgets::text(g, x + kCapW / 2, kCapY + 24, label, Widgets::fontL(),
                lit ? t.onAccent : t.ink, Widgets::kCenter);
}

}  // namespace

namespace KeysSplash {

void run(const DeckTheme &t, const char *subtitle) {
  Arduino_GFX *g = CrowDisplay::canvas();
  if (g == nullptr) {
    return;
  }

  g->fillScreen(t.bg);
  CrowDisplay::flush();

  // Phase 1: wordmark fades up out of the background.
  for (uint16_t elapsed = 0; elapsed < kFadeMs; elapsed += kFrameMs) {
    drawTitle(g, t, (uint8_t)((uint32_t)elapsed * 255 / kFadeMs), subtitle);
    CrowDisplay::flush(0, kTitleBandY, kScreenW, kTitleBandH);
    delay(kFrameMs);
  }
  drawTitle(g, t, 255, subtitle);
  CrowDisplay::flush(0, kTitleBandY, kScreenW, kTitleBandH);

  // Phase 2: the caps land left to right. Each step settles the previous cap
  // and lights the new one, so the flush covers just those two cells plus the
  // rule segment growing underneath - the cost per frame never grows with the
  // width of the row.
  uint16_t perCap = kCapsMs / kCapCount;
  if (perCap == 0) {
    perCap = 1;
  }
  for (uint8_t i = 0; i < kCapCount; ++i) {
    if (i > 0) {
      drawCap(g, t, (uint8_t)(i - 1), /*lit=*/false);
    }
    drawCap(g, t, i, /*lit=*/true);
    int16_t x0 = capX(i > 0 ? (uint8_t)(i - 1) : i);
    int16_t x1 = capX(i) + kCapW;
    g->drawFastHLine(x0, kRuleY, x1 - x0, t.accent);
    CrowDisplay::flush(x0, kCapY, x1 - x0, kRuleY - kCapY + 3);
    delay(perCap);
  }
  // Settle the last cap so the row reads as a keyboard at rest, not mid-press.
  drawCap(g, t, kCapCount - 1, /*lit=*/false);
  CrowDisplay::flush(capX(kCapCount - 1), kCapY, kCapW, kCapH);

  delay(kHoldMs);
}

}  // namespace KeysSplash

#else  // no display: nothing to animate

namespace KeysSplash {
void run(const DeckTheme &, const char *) {}
}  // namespace KeysSplash

#endif
