// On-panel JPEG viewer: SD -> hardware decoder -> PPA scale -> framebuffer.
// COMPILE-VERIFIED on esp32:esp32:esp32p4 (core 3.3.8). NOT HARDWARE-VERIFIED -
// no stored image has been displayed on a physical panel.

#include "ImageViewer.h"

#if USE_CAM_SD && USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)

#include <Arduino_GFX_Library.h>
#include <SD_MMC.h>
#include <driver/jpeg_decode.h>
#include <driver/ppa.h>
#include <esp_heap_caps.h>

namespace {
using namespace Widgets;

jpeg_decoder_handle_t gDecoder = nullptr;
ppa_client_handle_t gScaler = nullptr;

constexpr int16_t kPanelW = 1024;
constexpr int16_t kPanelH = 600;

uint16_t *framebuffer() {
  Arduino_GFX *gfx = CrowDisplay::canvas();
  if (gfx == nullptr) return nullptr;
  return static_cast<Arduino_DSI_Display *>(gfx)->getFramebuffer();
}
}  // namespace

bool ImageViewer::begin(uint16_t maxWidth, uint16_t maxHeight) {
  if (ready_) return true;

  jpeg_decode_engine_cfg_t engine = {};
  engine.intr_priority = 0;
  engine.timeout_ms = 400;
  if (jpeg_new_decoder_engine(&engine, &gDecoder) != ESP_OK) {
    gDecoder = nullptr;
    lastError_ = "hardware JPEG decoder unavailable";
    Logger::warn("viewer", lastError_);
    return false;
  }

  if (gScaler == nullptr) {
    ppa_client_config_t cfg = {};
    cfg.oper_type = PPA_OPERATION_SRM;
    cfg.max_pending_trans_num = 1;
    if (ppa_register_client(&cfg, &gScaler) != ESP_OK) gScaler = nullptr;
  }

  // Input buffer holds the compressed file. Stills run 100-300 KB at q90, so
  // half the raw pixel count is a generous ceiling that still refuses anything
  // absurd rather than trying to allocate it.
  jpeg_decode_memory_alloc_cfg_t inCfg = {};
  inCfg.buffer_direction = JPEG_DEC_ALLOC_INPUT_BUFFER;
  fileBuf_ = (uint8_t *)jpeg_alloc_decoder_mem((size_t)maxWidth * maxHeight, &inCfg,
                                               &fileBufBytes_);

  // Output buffer holds the decoded RGB565 image at full size.
  jpeg_decode_memory_alloc_cfg_t outCfg = {};
  outCfg.buffer_direction = JPEG_DEC_ALLOC_OUTPUT_BUFFER;
  pixelBuf_ = (uint16_t *)jpeg_alloc_decoder_mem(
      (size_t)maxWidth * maxHeight * sizeof(uint16_t), &outCfg, &pixelBufBytes_);

  if (fileBuf_ == nullptr || pixelBuf_ == nullptr) {
    lastError_ = "out of memory for the image viewer";
    Logger::warn("viewer", lastError_);
    return false;
  }

  ready_ = true;
  lastError_ = "";
  Logger::info("viewer", String("ready (in ") + String((uint32_t)fileBufBytes_) +
                             " B, out " + String((uint32_t)pixelBufBytes_) + " B)");
  return true;
}

bool ImageViewer::show(const char *path) {
  if (!ready_) {
    lastError_ = "viewer not started";
    return false;
  }
  uint16_t *fb = framebuffer();
  if (fb == nullptr) {
    lastError_ = "no framebuffer";
    return false;
  }

  File file = SD_MMC.open(path, FILE_READ);
  if (!file || file.isDirectory()) {
    if (file) file.close();
    lastError_ = "could not open the file";
    return false;
  }
  const size_t size = file.size();
  if (size == 0 || size > fileBufBytes_) {
    file.close();
    lastError_ = "file is empty or too large to decode";
    return false;
  }
  const size_t read = file.read(fileBuf_, size);
  file.close();
  if (read != size) {
    lastError_ = "short read from the card";
    return false;
  }

  // Read the header first so the output size is known before decoding into a
  // buffer - a mismatch here is the difference between a picture and a
  // heap corruption.
  jpeg_decode_picture_info_t info = {};
  if (jpeg_decoder_get_info(fileBuf_, size, &info) != ESP_OK) {
    lastError_ = "not a readable JPEG";
    return false;
  }
  const size_t needed = (size_t)info.width * info.height * sizeof(uint16_t);
  if (needed > pixelBufBytes_) {
    lastError_ = "image is larger than the viewer buffer";
    return false;
  }

  jpeg_decode_cfg_t cfg = {};
  cfg.output_format = JPEG_DECODE_OUT_FORMAT_RGB565;
  // RGB element order must match what the DSI framebuffer expects. The capture
  // path proved this panel wants no byte swap, and the same holds here.
  cfg.rgb_order = JPEG_DEC_RGB_ELEMENT_ORDER_RGB;
  cfg.conv_std = JPEG_YUV_RGB_CONV_STD_BT601;

  uint32_t decoded = 0;
  if (jpeg_decoder_process(gDecoder, &cfg, fileBuf_, size, (uint8_t *)pixelBuf_,
                           pixelBufBytes_, &decoded) != ESP_OK) {
    lastError_ = "decode failed";
    return false;
  }

  // Fit inside the panel while preserving aspect - letterbox rather than
  // stretch. A photo shown at the wrong shape is the exact mistake the
  // viewfinder rework just fixed; repeating it here would be careless.
  const float scale = fminf((float)kPanelW / (float)info.width,
                            (float)kPanelH / (float)info.height);
  const int16_t outW = (int16_t)(info.width * scale);
  const int16_t outH = (int16_t)(info.height * scale);
  const int16_t outX = (kPanelW - outW) / 2;
  const int16_t outY = (kPanelH - outH) / 2;

  Arduino_GFX *g = CrowDisplay::canvas();
  g->fillScreen(kBg);  // clear the letterbox margins

  bool drawn = false;
  if (gScaler != nullptr) {
    ppa_srm_oper_config_t op = {};
    op.in.buffer = pixelBuf_;
    op.in.pic_w = info.width;
    op.in.pic_h = info.height;
    op.in.block_w = info.width;
    op.in.block_h = info.height;
    op.in.srm_cm = PPA_SRM_COLOR_MODE_RGB565;

    op.out.buffer = fb;
    op.out.buffer_size = (uint32_t)kPanelW * kPanelH * sizeof(uint16_t);
    op.out.pic_w = kPanelW;
    op.out.pic_h = kPanelH;
    op.out.block_offset_x = outX;
    op.out.block_offset_y = outY;
    op.out.srm_cm = PPA_SRM_COLOR_MODE_RGB565;

    op.rotation_angle = PPA_SRM_ROTATION_ANGLE_0;
    op.scale_x = scale;
    op.scale_y = scale;
    op.mode = PPA_TRANS_MODE_BLOCKING;
    drawn = (ppa_do_scale_rotate_mirror(gScaler, &op) == ESP_OK);
  }
  if (!drawn) {
    // No scaler: nearest-neighbour by hand. Slower and softer, but a picture.
    for (int16_t row = 0; row < outH; row++) {
      const uint32_t srcY = (uint32_t)(row / scale);
      if (srcY >= info.height) break;
      const uint16_t *src = pixelBuf_ + srcY * info.width;
      uint16_t *dst = fb + (uint32_t)(outY + row) * kPanelW + outX;
      for (int16_t col = 0; col < outW; col++) {
        const uint32_t srcX = (uint32_t)(col / scale);
        if (srcX >= info.width) break;
        dst[col] = src[srcX];
      }
    }
  }

  // Caption strip so it is obvious which file is on screen and how to leave.
  const char *slash = strrchr(path, '/');
  currentName_ = slash != nullptr ? slash + 1 : path;
  panel(g, 0, kPanelH - 52, kPanelW, 52, 0, kBg);
  text(g, 24, kPanelH - 38, currentName_.c_str(), fontM(), kTextHi, kLeft);
  char dims[48];
  snprintf(dims, sizeof(dims), "%ux%u   tap to close", (unsigned)info.width,
           (unsigned)info.height);
  text(g, kPanelW - 24, kPanelH - 36, dims, fontS(), kTextMut, kRight);

  CrowDisplay::flush();
  showing_ = true;
  lastError_ = "";
  return true;
}

#else  // no SD, no display, or not a P4

bool ImageViewer::begin(uint16_t, uint16_t) {
  lastError_ = "needs USE_CAM_SD and USE_DISPLAY";
  return false;
}
bool ImageViewer::show(const char *) { return false; }

#endif
