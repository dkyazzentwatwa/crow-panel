#include "AcidGlassVisuals.h"

#include <CrowPanelShared.h>
#include <math.h>
#include <string.h>

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
#include <Arduino_GFX_Library.h>
#include <driver/ppa.h>
#include <esp_heap_caps.h>
#endif

namespace {

const char *const kSceneNames[kAcidSceneCount] = {
    "ASCII Plasma", "Glyph Rain", "Acid Tunnel", "Kaleidoscope",
    "Metaballs", "Reaction Diffusion", "Strange Attractor", "Moire Field",
    "Pixel Melt", "Star Warp", "Scope Garden", "Fractal Bloom",
};

const char *const kPaletteNames[kAcidPaletteCount] = {
    "Ultraviolet", "Toxic Candy", "Heat Death", "Cyber Lime",
    "Deep Ocean", "Solar Flare", "Cotton Acid", "Infrared",
    "Electric Ice", "Mushroom", "Monochrome", "RGB Riot",
};

struct Palette {
  uint8_t r0, g0, b0;
  uint8_t r1, g1, b1;
  uint8_t r2, g2, b2;
};

const Palette kPalettes[kAcidPaletteCount] = {
    {4, 0, 18, 133, 0, 255, 255, 54, 188},
    {2, 5, 8, 0, 255, 150, 255, 20, 210},
    {0, 0, 0, 255, 24, 0, 255, 238, 20},
    {0, 4, 0, 30, 255, 0, 210, 255, 30},
    {0, 2, 22, 0, 100, 210, 50, 255, 255},
    {8, 0, 18, 255, 60, 0, 255, 220, 80},
    {8, 0, 18, 255, 80, 190, 80, 240, 255},
    {0, 0, 0, 190, 0, 10, 255, 60, 0},
    {0, 8, 18, 0, 190, 255, 225, 255, 255},
    {5, 1, 0, 164, 58, 13, 225, 255, 90},
    {0, 0, 0, 110, 110, 110, 255, 255, 255},
    {0, 0, 25, 255, 0, 55, 0, 255, 230},
};

uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

uint16_t mix565(uint16_t a, uint16_t b, uint8_t amount) {
  const uint16_t inv = 255 - amount;
  uint16_t ar = (a >> 11) & 31, ag = (a >> 5) & 63, ab = a & 31;
  uint16_t br = (b >> 11) & 31, bg = (b >> 5) & 63, bb = b & 31;
  return static_cast<uint16_t>((((ar * inv + br * amount) / 255) << 11) |
                               (((ag * inv + bg * amount) / 255) << 5) |
                               ((ab * inv + bb * amount) / 255));
}

uint32_t square(int32_t value) { return static_cast<uint32_t>(value * value); }

int tinyIndex(char value) {
  if (value >= 'A' && value <= 'Z') return value - 'A';
  if (value >= '0' && value <= '9') return 26 + value - '0';
  switch (value) {
    case ' ': return 36;
    case '-': return 37;
    case '/': return 38;
    case '.': return 39;
    case ':': return 40;
    case '+': return 41;
    default: return 36;
  }
}

const uint8_t kTinyGlyphs[42][5] = {
    {7,5,7,5,5},{6,5,6,5,6},{7,4,4,4,7},{6,5,5,5,6},{7,4,6,4,7},{7,4,6,4,4},
    {7,4,5,5,7},{5,5,7,5,5},{7,2,2,2,7},{1,1,1,5,2},{5,5,6,5,5},{4,4,4,4,7},
    {5,7,7,5,5},{5,7,7,7,5},{7,5,5,5,7},{7,5,7,4,4},{7,5,5,7,1},{7,5,7,5,5},
    {7,4,7,1,7},{7,2,2,2,2},{5,5,5,5,7},{5,5,5,5,2},{5,5,7,7,5},{5,5,2,5,5},
    {5,5,2,2,2},{7,1,2,4,7},
    {7,5,5,5,7},{2,6,2,2,7},{7,1,7,4,7},{7,1,7,1,7},{5,5,7,1,1},
    {7,4,7,1,7},{7,4,7,5,7},{7,1,2,2,2},{7,5,7,5,7},{7,5,7,1,7},
    {0,0,0,0,0},{0,0,7,0,0},{1,1,2,4,4},{0,0,0,0,2},{0,2,0,2,0},{0,2,7,2,0},
};

}  // namespace

const char *AcidGlassVisuals::sceneName(uint8_t scene) {
  return kSceneNames[scene % kAcidSceneCount];
}

const char *AcidGlassVisuals::paletteName(uint8_t palette) {
  return kPaletteNames[palette % kAcidPaletteCount];
}

uint16_t AcidGlassVisuals::paletteColor(uint8_t palette, uint8_t value, uint8_t hue) {
  const Palette &p = kPalettes[palette % kAcidPaletteCount];
  const uint8_t v = value + hue;
  const bool upper = v >= 128;
  const uint8_t t = upper ? static_cast<uint8_t>((v - 128) * 2) : static_cast<uint8_t>(v * 2);
  const uint8_t inv = 255 - t;
  const uint8_t ar = upper ? p.r1 : p.r0;
  const uint8_t ag = upper ? p.g1 : p.g0;
  const uint8_t ab = upper ? p.b1 : p.b0;
  const uint8_t br = upper ? p.r2 : p.r1;
  const uint8_t bg = upper ? p.g2 : p.g1;
  const uint8_t bb = upper ? p.b2 : p.b1;
  return rgb565((ar * inv + br * t) / 255, (ag * inv + bg * t) / 255,
                (ab * inv + bb * t) / 255);
}

int8_t AcidGlassVisuals::sceneIndex(const String &name) {
  String wanted = name;
  wanted.trim();
  wanted.toLowerCase();
  for (uint8_t i = 0; i < kAcidSceneCount; ++i) {
    String candidate = kSceneNames[i];
    candidate.toLowerCase();
    if (wanted == candidate || wanted == String(i) || candidate.startsWith(wanted)) return i;
  }
  return -1;
}

int8_t AcidGlassVisuals::paletteIndex(const String &name) {
  String wanted = name;
  wanted.trim();
  wanted.toLowerCase();
  for (uint8_t i = 0; i < kAcidPaletteCount; ++i) {
    String candidate = kPaletteNames[i];
    candidate.toLowerCase();
    if (wanted == candidate || wanted == String(i) || candidate.startsWith(wanted)) return i;
  }
  return -1;
}

bool AcidGlassVisuals::begin() {
  for (uint16_t i = 0; i < 256; ++i) {
    sine_[i] = static_cast<uint8_t>(127.5f + sinf(i * TWO_PI / 256.0f) * 127.5f);
  }
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  const size_t bytes = static_cast<size_t>(kWidth) * kHeight * sizeof(uint16_t);
  frame_ = static_cast<uint16_t *>(heap_caps_aligned_alloc(
      64, bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT));
  internalBuffer_ = frame_ != nullptr;
  if (frame_ == nullptr) {
    frame_ = static_cast<uint16_t *>(
        heap_caps_aligned_alloc(64, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  }
  if (frame_ == nullptr) {
    lastError_ = "source buffer allocation failed";
    return false;
  }
  feedback_ = static_cast<uint16_t *>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM));
  memset(frame_, 0, bytes);
  if (feedback_ != nullptr) memset(feedback_, 0, bytes);
  lastError_ = feedback_ != nullptr ? "ok" : "feedback unavailable; running dry";

#if ACID_GLASS_USE_PPA
  ppa_client_config_t config = {};
  config.oper_type = PPA_OPERATION_SRM;
  config.max_pending_trans_num = 1;
  ppa_client_handle_t handle = nullptr;
  ppaLastError_ = ppa_register_client(&config, &handle);
  ppaReady_ = ppaLastError_ == ESP_OK;
  if (!ppaReady_) ppaFailures_++;
  ppa_ = handle;
#endif
  fpsWindowMs_ = millis();
  proofStartedMs_ = fpsWindowMs_;
  return true;
#else
  return true;
#endif
}

uint16_t AcidGlassVisuals::color_(uint8_t value, uint8_t palette, uint8_t hue) const {
  return paletteColor(palette, value, hue);
}

uint8_t AcidGlassVisuals::field_(uint8_t scene, int16_t x, int16_t y, uint32_t tick,
                                const AcidGlassState &state, const AudioFeatures &audio) {
  const int16_t cx = x - kWidth / 2;
  const int16_t cy = y - kHeight / 2;
  const uint8_t bass = audio.bands[0] + audio.bands[1];
  switch (scene) {
    case 0:
      return (wave_(x * 3 + tick) + wave_(y * 5 - tick * 2) +
              wave_((x + y) * 2 + tick + bass)) / 3;
    case 2: {
      const uint16_t r = abs(cx) + abs(cy) + 1;
      const uint8_t angle = static_cast<uint8_t>((cx * 64) / (abs(cy) + 8));
      return wave_(r * (2 + state.visual.zoom / 64) - tick * 3 + angle + bass);
    }
    case 3: {
      int16_t fx = abs(cx), fy = abs(cy);
      if (state.visual.symmetry > 4 && fy > fx) { int16_t swap = fx; fx = fy; fy = swap; }
      return wave_(fx * 5 + fy * 7 + tick * 2 + wave_(fx + tick));
    }
    case 4: {
      uint32_t sum = 0;
      for (uint8_t i = 0; i < 4; ++i) {
        int16_t bx = 128 + static_cast<int16_t>(wave_(tick * (i + 1) + i * 57)) / 3 - 42;
        int16_t by = 75 + static_cast<int16_t>(wave_(tick * (i + 2) + i * 91)) / 4 - 32;
        uint32_t d = square(x - bx) + square(y - by) + 20;
        sum += (32000UL + bass * 120UL) / d;
      }
      return static_cast<uint8_t>(min<uint32_t>(255, sum));
    }
    case 5: {
      if (feedback_ == nullptr) return wave_(x * 2 + y * 3 + tick);
      const uint16_t pos = static_cast<uint16_t>(y) * kWidth + x;
      const uint16_t left = feedback_[static_cast<uint16_t>(y) * kWidth + ((x + kWidth - 1) % kWidth)];
      const uint16_t up = feedback_[static_cast<uint16_t>((y + kHeight - 1) % kHeight) * kWidth + x];
      const uint8_t cell = static_cast<uint8_t>((feedback_[pos] ^ left ^ up) & 0xFF);
      return wave_(cell + tick + x * 2 - y * 3);
    }
    case 7: {
      const int16_t ax = cx + static_cast<int16_t>(wave_(tick)) / 4 - 32;
      const int16_t ay = cy + static_cast<int16_t>(wave_(tick + 80)) / 4 - 32;
      const int16_t bx = cx - static_cast<int16_t>(wave_(tick + 150)) / 3 + 42;
      const int16_t by = cy - static_cast<int16_t>(wave_(tick + 20)) / 4 + 32;
      const uint16_t da = static_cast<uint16_t>(sqrtf(square(ax) + square(ay)));
      const uint16_t db = static_cast<uint16_t>(sqrtf(square(bx) + square(by)));
      return wave_(da * 7 + db * 9 + tick);
    }
    case 9: {
      const uint16_t r = static_cast<uint16_t>(sqrtf(square(cx) + square(cy))) + 1;
      const int16_t ray = (cx * 127) / r;
      return wave_(r * 6 - tick * 5 + ray * 3 + audio.onset);
    }
    case 11: {
      float zx = 0.0f, zy = 0.0f;
      const float scale = 0.010f + (255 - state.visual.zoom) * 0.000025f;
      const float cr = cx * scale - 0.55f + (state.visual.macroX - 128) * 0.0015f;
      const float ci = cy * scale + (state.visual.macroY - 128) * 0.0015f;
      const uint8_t maxIterations = renderQuality_ == 0 ? 7 : (renderQuality_ == 1 ? 9 : 12);
      uint8_t iter = 0;
      for (; iter < maxIterations && zx * zx + zy * zy < 4.0f; ++iter) {
        float next = zx * zx - zy * zy + cr;
        zy = 2.0f * zx * zy + ci;
        zx = next;
      }
      return static_cast<uint8_t>(iter * 21 + tick);
    }
    default:
      return wave_(x * 2 + y * 3 + tick);
  }
}

void AcidGlassVisuals::renderAscii_(uint32_t tick, const AcidGlassState &state,
                                    const AudioFeatures &audio) {
  static const uint8_t glyphs[6][5] = {
      {0, 0, 0, 0, 0}, {0, 4, 0, 0, 0}, {0, 10, 0, 10, 0},
      {5, 2, 5, 2, 5}, {7, 5, 7, 5, 7}, {7, 7, 7, 7, 7},
  };
  const uint8_t hue = tick * state.visual.hueRate / 128;
  memset(frame_, 0, static_cast<size_t>(kWidth) * kHeight * 2);
  for (int16_t gy = 0; gy < kHeight; gy += 6) {
    for (int16_t gx = 0; gx < kWidth; gx += 4) {
      uint8_t v = field_(0, gx, gy, tick, state, audio);
      uint8_t gi = v / 43;
      if (gi > 5) gi = 5;
      uint16_t col = color_(v, state.palette, hue);
      for (uint8_t row = 0; row < 5; ++row) {
        uint8_t bits = glyphs[gi][row];
        for (uint8_t px = 0; px < 3; ++px) {
          if (bits & (1 << (2 - px))) frame_[(gy + row) * kWidth + gx + px] = col;
        }
      }
    }
  }
}

void AcidGlassVisuals::renderAttractor_(uint32_t tick, const AcidGlassState &state,
                                        const AudioFeatures &audio) {
  blendFeedback_(state.visual.trails > 220 ? 220 : state.visual.trails);
  float x = 0.1f + (state.visual.macroX - 128) * 0.0005f;
  float y = 0.0f;
  const float a = 1.2f + state.visual.macroY / 255.0f;
  const uint8_t hue = tick * state.visual.hueRate / 128;
  const uint16_t col = color_(200 + audio.peak / 4, state.palette, hue);
  const uint16_t points = renderQuality_ == 0 ? 2400 : (renderQuality_ == 1 ? 3600 : 5000);
  for (uint16_t i = 0; i < points; ++i) {
    const float nx = sinf(a * y) - cosf(1.7f * x);
    const float ny = sinf(1.3f * x) - cosf(a * y);
    x = nx;
    y = ny;
    int16_t px = 128 + static_cast<int16_t>(x * 47.0f);
    int16_t py = 75 + static_cast<int16_t>(y * 27.0f);
    if (px >= 0 && px < kWidth && py >= 0 && py < kHeight) frame_[py * kWidth + px] = col;
  }
}

void AcidGlassVisuals::renderPixelMelt_(uint32_t tick, const AcidGlassState &state,
                                       const AudioFeatures &audio) {
  const uint8_t hue = tick * state.visual.hueRate / 128;
  for (int16_t x = 0; x < kWidth; ++x) {
    uint8_t drop = 1 + ((wave_(x * 5 + tick) + audio.bands[x & 7]) >> 6);
    for (int16_t y = kHeight - 1; y >= 0; --y) {
      int16_t srcY = y - drop;
      uint16_t old = (feedback_ != nullptr && srcY >= 0)
                         ? feedback_[srcY * kWidth + x]
                         : 0;
      frame_[y * kWidth + x] = mix565(old, color_(wave_(x * 3 + tick), state.palette, hue), 18);
    }
  }
  for (uint8_t i = 0; i < 8; ++i) {
    int16_t bar = audio.bands[i] * kHeight / 255;
    int16_t x0 = i * 32;
    for (int16_t y = 0; y < bar; ++y) {
      frame_[(kHeight - 1 - y) * kWidth + x0 + (tick + y) % 31] =
          color_(180 + y, state.palette, hue + i * 21);
    }
  }
}

void AcidGlassVisuals::renderScopeGarden_(uint32_t tick, const AcidGlassState &state,
                                         const AudioFeatures &audio) {
  blendFeedback_(state.visual.trails);
  const uint8_t hue = tick * state.visual.hueRate / 128;
  for (uint8_t band = 0; band < kAcidBandCount; ++band) {
    int16_t base = 15 + band * 17;
    int16_t amp = 2 + audio.bands[band] / 12;
    uint16_t col = color_(80 + band * 22, state.palette, hue + band * 15);
    for (int16_t x = 1; x < kWidth; ++x) {
      int16_t y = base + ((static_cast<int16_t>(wave_(x * (band + 2) + tick * 2)) - 128) * amp >> 7);
      if (y >= 0 && y < kHeight) {
        frame_[y * kWidth + x] = col;
        if (audio.onset > 160 && y + 1 < kHeight) frame_[(y + 1) * kWidth + x] = col;
      }
    }
  }
}

void AcidGlassVisuals::blendFeedback_(uint8_t amount) {
  if (feedback_ == nullptr) return;
  const size_t pixels = static_cast<size_t>(kWidth) * kHeight;
  for (size_t i = 0; i < pixels; ++i) frame_[i] = mix565(0, feedback_[i], amount);
}

void AcidGlassVisuals::fillRect_(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
  const int16_t left = max<int16_t>(0, x);
  const int16_t top = max<int16_t>(0, y);
  const int16_t right = min<int16_t>(kWidth, x + w);
  const int16_t bottom = min<int16_t>(kHeight, y + h);
  for (int16_t row = top; row < bottom; ++row) {
    for (int16_t column = left; column < right; ++column) frame_[row * kWidth + column] = color;
  }
}

void AcidGlassVisuals::line_(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color) {
  const int16_t dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
  const int16_t dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
  int16_t error = dx + dy;
  while (true) {
    if (x0 >= 0 && x0 < kWidth && y0 >= 0 && y0 < kHeight) frame_[y0 * kWidth + x0] = color;
    if (x0 == x1 && y0 == y1) break;
    const int16_t twice = error * 2;
    if (twice >= dy) { error += dy; x0 += sx; }
    if (twice <= dx) { error += dx; y0 += sy; }
  }
}

void AcidGlassVisuals::text_(int16_t x, int16_t y, const char *text, uint16_t color) {
  if (text == nullptr) return;
  for (const char *cursor = text; *cursor != '\0'; ++cursor, x += 4) {
    int index = tinyIndex(*cursor >= 'a' && *cursor <= 'z' ? *cursor - 32 : *cursor);
    for (int16_t row = 0; row < 5; ++row) {
      uint8_t bits = kTinyGlyphs[index][row];
      for (int16_t column = 0; column < 3; ++column) {
        if ((bits & (1 << (2 - column))) != 0 && x + column >= 0 && x + column < kWidth &&
            y + row >= 0 && y + row < kHeight) {
          frame_[(y + row) * kWidth + x + column] = color;
        }
      }
    }
  }
}

void AcidGlassVisuals::number_(int16_t x, int16_t y, uint32_t value, uint16_t color) {
  char text[12];
  snprintf(text, sizeof(text), "%lu", static_cast<unsigned long>(value));
  text_(x, y, text, color);
}

void AcidGlassVisuals::slider_(int16_t y, const char *label, uint8_t value, uint16_t color) {
  text_(5, y, label, 0xFFFF);
  fillRect_(56, y + 1, 188, 3, 0x2104);
  fillRect_(56, y + 1, max<int16_t>(1, value * 188L / 255L), 3, color);
  number_(246, y, value, color);
}

void AcidGlassVisuals::composeOverlay_(const AcidGlassState &state,
                                       const AcidGlassOverlay &overlay) {
  if (!overlay.hud) return;
  const uint16_t black = 0x0000, white = 0xFFFF, pink = 0xF81F, lime = 0x87E0;
  fillRect_(0, 0, kWidth, 9, black);
  text_(4, 2, "ACID GLASS", pink);
  text_(48, 2, sceneName(state.scene), white);
  text_(146, 2, paletteName(state.palette), lime);
  text_(220, 2, overlay.ppaReady ? "PPA" : (overlay.ppaRequested ? "ERR" : "CPU"), white);

  fillRect_(0, 138, kWidth, 12, black);
  text_(4, 140, "F", white); number_(10, 140, fps_, lime);
  text_(24, 140, "T", white); number_(30, 140, overlay.targetFps, pink);
  text_(44, 140, "Q", white); number_(50, 140, overlay.qualityTier + 1, lime);
  text_(63, 140, "P", white); number_(69, 140, overlay.presentUs / 1000, white);
  text_(83, 140, overlay.sdReady ? "SD" : "--", overlay.sdReady ? lime : 0x7BEF);
  text_(96, 140, overlay.audioPlaying ? "PLAY" : "DEMO", overlay.audioPlaying ? pink : white);
  text_(120, 140, overlay.remoteReady ? "NET" : "OFF", overlay.remoteReady ? lime : 0x7BEF);
  const char *tabs[] = {"SCENE", "COLOR", "MOTION", "AUDIO"};
  for (uint8_t i = 0; i < 4; ++i) {
    const int16_t x = 158 + i * 24;
    const uint16_t color = overlay.sheet == i ? lime : white;
    text_(x, 140, tabs[i], color);
  }

  if (overlay.sheet == kAcidSheetClosed) {
    if (overlay.toast[0] != '\0') {
      fillRect_(82, 12, 96, 8, black);
      line_(82, 12, 177, 12, lime);
      line_(82, 19, 177, 19, lime);
      text_(86, 14, overlay.toast, lime);
    }
    return;
  }

  fillRect_(0, 90, kWidth, 48, black);
  line_(0, 90, 255, 90, pink);
  if (overlay.sheet == 0) {
    text_(6, 94, "SCENE", lime);
    text_(6, 103, "PREV", white); text_(68, 103, "NEXT", white);
    text_(130, 103, "PAL-", pink); text_(194, 103, "PAL+", pink);
    for (uint8_t slot = 0; slot < kAcidPresetCount; ++slot) {
      int16_t x = (slot % 8) * 32 + 3;
      int16_t y = 114 + (slot / 8) * 10;
      if (slot == state.scene) fillRect_(x - 1, y - 1, 28, 8, 0x2104);
      text_(x, y, "P", slot == state.scene ? lime : white);
      number_(x + 5, y, slot + 1, slot == state.scene ? lime : pink);
    }
  } else if (overlay.sheet == 1) {
    text_(6, 94, "COLOR", lime);
    slider_(103, "INT", state.visual.intensity, pink);
    slider_(112, "HUE", state.visual.hueRate, lime);
    slider_(121, "SAFE", state.safeFlash ? 255 : 0, lime);
    text_(6, 131, "TAP UPPER ROW: PALETTE -/+", white);
  } else if (overlay.sheet == 2) {
    text_(6, 94, "MOTION", lime);
    slider_(101, "SPD", state.visual.speed, pink);
    slider_(107, "ZOOM", state.visual.zoom, lime);
    slider_(113, "WARP", state.visual.warp, pink);
    slider_(119, "FEED", state.visual.feedback, lime);
    slider_(125, "TRAIL", state.visual.trails, pink);
    slider_(131, "PIX", state.visual.pixelScale, lime);
  } else {
    text_(6, 94, "AUDIO", lime);
    text_(6, 104, "PREV", white); text_(68, 104, "PLAY", lime);
    text_(130, 104, "STOP", pink); text_(194, 104, "NEXT", white);
    slider_(116, "VOL", state.volume * 255L / 100L, lime);
    slider_(126, "SENS", state.visual.audioSensitivity, pink);
    text_(6, 134, overlay.audioPlaying ? "LIVE ANALYSIS" : "INTERNAL BEAT READY", white);
  }
}

bool AcidGlassVisuals::render(const AcidGlassState &state, const AudioFeatures &audio,
                              const AcidGlassOverlay &overlay, uint32_t nowMs) {
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  if (frame_ == nullptr) {
    lastError_ = "source buffer unavailable";
    return false;
  }
  const uint32_t started = micros();
  renderQuality_ = overlay.qualityTier;
  const uint32_t tick = nowMs * (32 + state.visual.speed) / 1000;
  if (state.scene == 0 || state.scene == 1) {
    renderAscii_(tick + (state.scene == 1 ? nowMs / 9 : 0), state, audio);
  } else if (state.scene == 6) {
    renderAttractor_(tick, state, audio);
  } else if (state.scene == 8) {
    renderPixelMelt_(tick, state, audio);
  } else if (state.scene == 10) {
    renderScopeGarden_(tick, state, audio);
  } else {
    const uint8_t hue = tick * state.visual.hueRate / 128;
    for (int16_t y = 0; y < kHeight; ++y) {
      for (int16_t x = 0; x < kWidth; ++x) {
        uint8_t pixel = 1 + state.visual.pixelScale / 64;
        int16_t sx = (x / pixel) * pixel;
        int16_t sy = (y / pixel) * pixel;
        int16_t bend = ((static_cast<int16_t>(wave_(y * 3 + tick)) - 128) *
                        state.visual.warp) >> 10;
        sx = (sx + bend + kWidth) % kWidth;
        uint8_t value = field_(state.scene, sx, sy, tick, state, audio);
        value = static_cast<uint8_t>((static_cast<uint16_t>(value) * state.visual.intensity) >> 8);
        frame_[static_cast<size_t>(y) * kWidth + x] = color_(value, state.palette, hue);
      }
    }
    if (feedback_ != nullptr && state.visual.feedback > 0) {
      for (size_t i = 0; i < static_cast<size_t>(kWidth) * kHeight; ++i) {
        frame_[i] = mix565(frame_[i], feedback_[i], state.visual.feedback / 3);
      }
    }
  }
  // The public/default mode softens whole-frame luminance reversals by carrying
  // a quarter of the previous frame into the new one. Factory scenes never
  // deliberately strobe; this also makes scene changes less abrupt on camera.
  if (feedback_ != nullptr && state.safeFlash && frameCount_ > 0) {
    for (size_t i = 0; i < static_cast<size_t>(kWidth) * kHeight; ++i) {
      frame_[i] = mix565(frame_[i], feedback_[i], 64);
    }
  }
  if (feedback_ != nullptr && overlay.transition < 255) {
    const uint8_t previousAmount = static_cast<uint8_t>((255 - overlay.transition) * 180UL / 255UL);
    for (size_t i = 0; i < static_cast<size_t>(kWidth) * kHeight; ++i) {
      frame_[i] = mix565(frame_[i], feedback_[i], previousAmount);
    }
  }
  // Store art before adding chrome, so the cockpit never becomes feedback.
  if (feedback_ != nullptr) {
    memcpy(feedback_, frame_, static_cast<size_t>(kWidth) * kHeight * sizeof(uint16_t));
  }
  composeOverlay_(state, overlay);
  const uint32_t presentStarted = micros();
  if (!blit_()) {
    lastError_ = "CPU/PPA blit failed";
    return false;
  }
  lastPresentUs_ = micros() - presentStarted;

  frameCount_++;
  fpsWindowFrames_++;
  if (nowMs - fpsWindowMs_ >= 1000) {
    fps_ = static_cast<uint16_t>(fpsWindowFrames_ * 1000UL / (nowMs - fpsWindowMs_));
    fpsWindowMs_ = nowMs;
    fpsWindowFrames_ = 0;
  }
  lastFrameUs_ = micros() - started;
  lastError_ = feedback_ != nullptr ? "ok" : "feedback unavailable; running dry";
  return true;
#else
  (void)state;
  (void)audio;
  (void)nowMs;
  frameCount_++;
  return true;
#endif
}

bool AcidGlassVisuals::renderBringupProof(uint32_t nowMs) {
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  if (frame_ == nullptr) {
    lastError_ = "source buffer unavailable";
    return false;
  }
  const uint32_t started = micros();
  const uint32_t elapsed = nowMs - proofStartedMs_;
  for (int16_t y = 0; y < kHeight; ++y) {
    for (int16_t x = 0; x < kWidth; ++x) {
      uint16_t color;
      if (elapsed < 1200) {
        const bool bright = ((x / 16) ^ (y / 16)) & 1;
        color = bright ? 0xF81F : 0x07FF;
      } else {
        const uint8_t r = static_cast<uint8_t>(x + elapsed / 13);
        const uint8_t g = static_cast<uint8_t>(y * 2 + elapsed / 19);
        const uint8_t b = static_cast<uint8_t>(x + y + elapsed / 11);
        color = rgb565(r, g, b);
      }
      frame_[static_cast<size_t>(y) * kWidth + x] = color;
    }
  }
  if (!blit_()) {
    lastError_ = "CPU proof blit failed";
    return false;
  }
  frameCount_++;
  fpsWindowFrames_++;
  if (nowMs - fpsWindowMs_ >= 1000) {
    fps_ = static_cast<uint16_t>(fpsWindowFrames_ * 1000UL / (nowMs - fpsWindowMs_));
    fpsWindowMs_ = nowMs;
    fpsWindowFrames_ = 0;
  }
  lastFrameUs_ = micros() - started;
  lastError_ = "proof running";
  return true;
#else
  (void)nowMs;
  return false;
#endif
}

bool AcidGlassVisuals::blit_() {
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  Arduino_GFX *gfx = CrowDisplay::canvas();
  if (gfx == nullptr) return false;
  uint16_t *dst = static_cast<Arduino_DSI_Display *>(gfx)->getFramebuffer();
  if (dst == nullptr) return false;
#if ACID_GLASS_USE_PPA
  if (ppaReady_ && ppa_ != nullptr) {
    ppa_srm_oper_config_t op = {};
    op.in.buffer = frame_;
    op.in.pic_w = kWidth;
    op.in.pic_h = kHeight;
    op.in.block_w = kWidth;
    op.in.block_h = kHeight;
    op.in.srm_cm = PPA_SRM_COLOR_MODE_RGB565;
    op.out.buffer = dst;
    op.out.buffer_size = 1024UL * 600UL * sizeof(uint16_t);
    op.out.pic_w = 1024;
    op.out.pic_h = 600;
    op.out.block_offset_x = 0;
    op.out.block_offset_y = 0;
    op.out.srm_cm = PPA_SRM_COLOR_MODE_RGB565;
    op.scale_x = 4.0f;
    op.scale_y = 4.0f;
    op.rotation_angle = PPA_SRM_ROTATION_ANGLE_0;
    op.mode = PPA_TRANS_MODE_BLOCKING;
    ppaLastError_ = ppa_do_scale_rotate_mirror(static_cast<ppa_client_handle_t>(ppa_), &op);
    if (ppaLastError_ == ESP_OK) {
      return true;
    }
    ppaFailures_++;
    ppaReady_ = false;
  }
#endif
  for (int16_t y = 0; y < kHeight; ++y) {
    for (int16_t x = 0; x < kWidth; ++x) {
      uint16_t color = frame_[y * kWidth + x];
      int16_t dx = x * 4, dy = y * 4;
      for (uint8_t yy = 0; yy < 4; ++yy) {
        uint16_t *row = dst + static_cast<size_t>(dy + yy) * 1024 + dx;
        row[0] = row[1] = row[2] = row[3] = color;
      }
    }
  }
  return true;
#else
  return false;
#endif
}

void AcidGlassVisuals::printStatus(Print &out) const {
  out.print(F("[visuals] scene-engine=12 buffer=256x150 location="));
  out.print(internalBuffer_ ? F("internal") : F("psram"));
  out.print(F(" ppa="));
  out.print(ppaReady_ ? F("ready") : (ppaRequested_ ? F("failed/fallback") : F("disabled/fallback")));
  out.print(F(" ppa_failures="));
  out.print(ppaFailures_);
  out.print(F(" ppa_error="));
  out.print(ppaLastError_);
  out.print(F(" fps="));
  out.print(fps_);
  out.print(F(" frame_us="));
  out.print(lastFrameUs_);
  out.print(F(" feedback="));
  out.print(feedback_ != nullptr ? F("ready") : F("dry"));
  out.print(F(" status="));
  out.println(lastError_);
}
