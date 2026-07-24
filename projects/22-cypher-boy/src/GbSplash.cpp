#include "GbSplash.h"

#include "GbAudio.h"
#include "GbInput.h"
#include "GbTheme.h"
#include "GbUi.h"
#include <CrowPanelShared.h>

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
#include <Arduino_GFX_Library.h>

namespace {

const int16_t kCartW = 132;
const int16_t kCartH = 148;
const int16_t kCartX = (1024 - kCartW) / 2;
const int16_t kRestY = 176;   // where the cartridge lands
const int16_t kStartY = -kCartH - 8;

// Blend two RGB565 colours. Arduino_GFX has no alpha, so a fade is done by
// interpolating toward the background and redrawing.
uint16_t mix565(uint16_t a, uint16_t b, float t) {
  if (t <= 0.0f) return a;
  if (t >= 1.0f) return b;
  const int ar = (a >> 11) & 0x1F, ag = (a >> 5) & 0x3F, ab = a & 0x1F;
  const int br = (b >> 11) & 0x1F, bg = (b >> 5) & 0x3F, bb = b & 0x1F;
  const int r = ar + (int)((br - ar) * t);
  const int g = ag + (int)((bg - ag) * t);
  const int bl = ab + (int)((bb - ab) * t);
  return (uint16_t)((r << 11) | (g << 5) | bl);
}

// A Game Boy cartridge: body with the classic clipped top-right corner, a
// lighter label panel, and contact fingers along the bottom.
void drawCart(Arduino_GFX *g, int16_t x, int16_t y, const GbPalette &P) {
  const int16_t notch = 26;
  g->fillRoundRect(x, y, kCartW, kCartH, 12, P.accent);
  // Clip the top-right corner by painting the background back over it.
  g->fillTriangle(x + kCartW - notch, y, x + kCartW, y, x + kCartW,
                  y + notch, P.bg);
  g->drawLine(x + kCartW - notch, y, x + kCartW, y + notch, P.line);

  // Label panel.
  g->fillRoundRect(x + 16, y + 28, kCartW - 32, 62, 6, P.surface);
  g->drawRoundRect(x + 16, y + 28, kCartW - 32, 62, 6, P.line);

  // Contact fingers.
  for (int i = 0; i < 6; i++) {
    g->fillRect(x + 22 + i * 16, y + kCartH - 30, 10, 18, P.surfaceHi);
  }
}

}  // namespace
#endif  // USE_DISPLAY && P4

void GbSplash::wipe(bool leftToRight) {
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  Arduino_GFX *g = CrowDisplay::canvas();
  if (!g) return;
  const GbPalette &P = GbUi::palette();
  const int16_t step = 64;
  for (int16_t i = 0; i < 1024; i += step) {
    const int16_t x = leftToRight ? i : (1024 - step - i);
    g->fillRect(x, 0, step, 600, P.bg);
    CrowDisplay::flush(x, 0, step, 600);
  }
#else
  (void)leftToRight;
#endif
}

void GbSplash::run(GbAudio *audio, GbInput *input) {
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  Arduino_GFX *g = CrowDisplay::canvas();
  if (!g) return;
  using namespace Widgets;
  const GbPalette &P = GbUi::palette();

  // A touch at any point skips straight to the end.
  auto skipped = [&]() {
    if (!input) return false;
    input->tick();
    return input->down() || input->releasedEdge();
  };

  g->fillScreen(P.bg);
  CrowDisplay::flush();

  // --- Descent: ease-out drop, repainting only the band that changed --------
  int16_t prevY = kStartY;
  const int kFrames = 34;
  for (int f = 0; f <= kFrames; f++) {
    if (skipped()) break;
    // 1-(1-t)^3 lands softly instead of arriving at constant speed.
    const float t = (float)f / (float)kFrames;
    const float e = 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
    const int16_t y = (int16_t)(kStartY + (kRestY - kStartY) * e);

    const int16_t top = min(prevY, y);
    const int16_t bot = max(prevY, y) + kCartH;
    g->fillRect(kCartX, max<int16_t>(0, top), kCartW,
                min<int16_t>(600, bot) - max<int16_t>(0, top), P.bg);
    drawCart(g, kCartX, y, P);
    CrowDisplay::flush(kCartX, max<int16_t>(0, top), kCartW,
                       min<int16_t>(600, bot) - max<int16_t>(0, top));
    prevY = y;
    delay(12);
  }

  // Make sure it is settled even if frames were skipped.
  g->fillRect(kCartX, 0, kCartW, kRestY + kCartH, P.bg);
  drawCart(g, kCartX, kRestY, P);
  CrowDisplay::flush(kCartX, 0, kCartW, kRestY + kCartH);

  // --- Landing: chime + wordmark fade --------------------------------------
  if (audio) audio->playChime();

  const int16_t wordY = kRestY + kCartH + 46;
  const int16_t subY = wordY + 46;
  for (int i = 1; i <= 6; i++) {
    if (skipped()) break;
    const float t = (float)i / 6.0f;
    text(g, 512, wordY, "CYPHER BOY", fontXL(), mix565(P.bg, P.ink, t), kCenter);
    text(g, 512, subY, "GAME BOY / COLOR", fontS(), mix565(P.bg, P.accent, t), kCenter);
    CrowDisplay::flush(0, wordY - 8, 1024, (subY + 30) - (wordY - 8));
    delay(28);
  }
  // Final pass at full strength in case the fade was cut short.
  text(g, 512, wordY, "CYPHER BOY", fontXL(), P.ink, kCenter);
  text(g, 512, subY, "GAME BOY / COLOR", fontS(), P.accent, kCenter);
  CrowDisplay::flush(0, wordY - 8, 1024, (subY + 30) - (wordY - 8));

  for (int i = 0; i < 24; i++) {
    if (skipped()) break;
    delay(20);
  }

  wipe(true);
#else
  (void)audio;
  (void)input;
#endif
}
