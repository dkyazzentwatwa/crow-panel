#include "DeskSplash.h"

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)

#include "DeskWidgets.h"

#include <Arduino_GFX_Library.h>
#include <CrowPanelShared.h>

using namespace DeskUi;

namespace {

constexpr int16_t kScreenW = 1024;
constexpr int16_t kScreenH = 600;
constexpr uint8_t kTiles = 6;
constexpr int16_t kTileW = 96;
constexpr int16_t kTileH = 88;
constexpr int16_t kTileGap = 14;
constexpr int16_t kTileY = 330;
constexpr uint16_t kFadeMs = 420;
constexpr uint16_t kTileMs = 110;

// Six blocks, one per app family, so the animation says what the device is
// rather than just being motion.
const char *const kTileLabels[kTiles] = {"W", "T", "C", "F", "M", "V"};

}  // namespace

void DeskSplash::run(const DeskThemePalette &theme, const char *subtitle) {
  Arduino_GFX *g = CrowDisplay::canvas();
  if (g == nullptr) return;

  g->fillScreen(theme.background);
  CrowDisplay::flush();

  // Wordmark fades in from the background colour, so it emerges rather than
  // appearing. Redrawing text over itself would leave the previous weight
  // behind, so each step repaints the band.
  const uint32_t fadeStart = millis();
  uint32_t elapsed = 0;
  while (elapsed < kFadeMs) {
    elapsed = millis() - fadeStart;
    const uint8_t t = static_cast<uint8_t>(min<uint32_t>(255, elapsed * 255 / kFadeMs));
    g->fillRect(0, 200, kScreenW, 110, theme.background);
    Widgets::text(g, kScreenW / 2, 214, "CYPHER DESK", Widgets::fontXL(),
                  blend565(theme.background, theme.ink, t), Widgets::kCenter);
    smallText(g, kScreenW / 2, 272, subtitle, blend565(theme.background, theme.accent, t),
              Widgets::kCenter);
    CrowDisplay::flush(0, 200, kScreenW, 110);
    delay(16);
  }

  const int16_t totalWidth = kTiles * kTileW + (kTiles - 1) * kTileGap;
  const int16_t startX = (kScreenW - totalWidth) / 2;
  const uint16_t accents[4] = {theme.accent, theme.accent2, theme.success, theme.accent3};

  for (uint8_t i = 0; i < kTiles; ++i) {
    const int16_t x = startX + i * (kTileW + kTileGap);
    const uint16_t accent = accents[i % 4];
    // Lands lit in the accent, then settles into a normal key cap - the same
    // two-step project 21's keycaps use.
    Widgets::panel(g, x, kTileY, kTileW, kTileH, 12, accent, 2, accent);
    Widgets::text(g, x + kTileW / 2, kTileY + 30, kTileLabels[i], Widgets::fontXL(),
                  theme.onAccent, Widgets::kCenter);
    CrowDisplay::flush(x - 4, kTileY - 4, kTileW + 12, kTileH + 12);
    delay(kTileMs);

    Widgets::panel(g, x + 3, kTileY + 4, kTileW, kTileH, 12, theme.background);
    Widgets::panel(g, x, kTileY, kTileW, kTileH, 12, theme.panel, 2, accent);
    Widgets::text(g, x + kTileW / 2, kTileY + 30, kTileLabels[i], Widgets::fontXL(), theme.ink,
                  Widgets::kCenter);
    CrowDisplay::flush(x - 4, kTileY - 4, kTileW + 12, kTileH + 12);
  }

  // Accent rule under the tiles, drawn last so it reads as the deck settling.
  const int16_t ruleY = kTileY + kTileH + 26;
  for (uint8_t i = 0; i < 4; ++i) {
    g->fillRect(startX + i * (totalWidth / 4), ruleY, totalWidth / 4, 4, accents[i]);
  }
  CrowDisplay::flush(startX, ruleY, totalWidth, 4);
  delay(160);
}

#else

void DeskSplash::run(const DeskThemePalette &, const char *) {}

#endif
