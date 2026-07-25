// Hardware JPEG encoding with an optional PPA down-scale.
// COMPILE-VERIFIED on esp32:esp32:esp32p4 (core 3.3.8). NOT HARDWARE-VERIFIED -
// no JPEG produced here has been opened by anything.

#include "JpegEncoder.h"

#if defined(CONFIG_IDF_TARGET_ESP32P4)

#include <driver/jpeg_encode.h>
#include <driver/ppa.h>
#include <esp_heap_caps.h>

namespace {
jpeg_encoder_handle_t gEncoder = nullptr;
ppa_client_handle_t gScaler = nullptr;
uint8_t gUsers = 0;  // so a second JpegEncoder cannot steal the hardware
}  // namespace

bool JpegEncoder::begin(uint16_t maxWidth, uint16_t maxHeight) {
  if (ready_) return true;

  if (gEncoder == nullptr) {
    // The timeout must exceed one frame's encode time; 200 ms is generous here
    // and turns a wedged encoder into an error rather than a hang.
    jpeg_encode_engine_cfg_t engineConfig = {};
    engineConfig.intr_priority = 0;
    engineConfig.timeout_ms = 200;
    if (jpeg_new_encoder_engine(&engineConfig, &gEncoder) != ESP_OK) {
      gEncoder = nullptr;
      lastError_ = "hardware JPEG encoder unavailable";
      Logger::warn("jpeg", lastError_);
      return false;
    }
  }

  if (gScaler == nullptr) {
    ppa_client_config_t scalerConfig = {};
    scalerConfig.oper_type = PPA_OPERATION_SRM;
    scalerConfig.max_pending_trans_num = 1;
    if (ppa_register_client(&scalerConfig, &gScaler) != ESP_OK) {
      gScaler = nullptr;
      Logger::warn("jpeg", "no PPA client; frames encode at native size");
    }
  }
  canScale_ = (gScaler != nullptr);

  // Scratch holds the down-scaled input. Sized for the largest frame so any
  // requested output size fits.
  scratchBytes_ = (size_t)maxWidth * maxHeight * sizeof(uint16_t);
  scratch_ = (uint16_t *)heap_caps_calloc(1, scratchBytes_,
                                          MALLOC_CAP_SPIRAM | MALLOC_CAP_CACHE_ALIGNED);

  // The output buffer must come from jpeg_alloc_encoder_mem - it has alignment
  // and cache requirements a plain malloc does not meet. Half the raw pixel
  // count is a generous ceiling for a JPEG at any quality we use.
  jpeg_encode_memory_alloc_cfg_t memConfig = {};
  memConfig.buffer_direction = JPEG_ENC_ALLOC_OUTPUT_BUFFER;
  output_ = (uint8_t *)jpeg_alloc_encoder_mem((size_t)maxWidth * maxHeight, &memConfig,
                                              &outputBytes_);

  if (scratch_ == nullptr || output_ == nullptr) {
    lastError_ = "out of memory for JPEG buffers";
    Logger::warn("jpeg", lastError_);
    return false;
  }

  gUsers++;
  ready_ = true;
  lastError_ = "";
  Logger::info("jpeg", String("encoder ready (out buffer ") +
                           String((uint32_t)outputBytes_) + " B, scaler " +
                           (canScale_ ? "on" : "off") + ")");
  return true;
}

bool JpegEncoder::scaleTo_(const CrowCamera::Frame &frame, uint16_t outW, uint16_t outH) {
  if (gScaler == nullptr || scratch_ == nullptr) return false;

  // At 90 or 270 the PPA writes a rotated picture, so the OUTPUT picture is
  // as tall as the input is wide. Getting this backwards would have it write
  // outside the buffer.
  const bool swaps = (rotation_ == 90 || rotation_ == 270);
  const uint16_t picW = swaps ? outH : outW;
  const uint16_t picH = swaps ? outW : outH;

  ppa_srm_rotation_angle_t angle = PPA_SRM_ROTATION_ANGLE_0;
  switch (rotation_) {
    case 90: angle = PPA_SRM_ROTATION_ANGLE_90; break;
    case 180: angle = PPA_SRM_ROTATION_ANGLE_180; break;
    case 270: angle = PPA_SRM_ROTATION_ANGLE_270; break;
    default: break;
  }

  ppa_srm_oper_config_t op = {};
  op.in.buffer = frame.data;
  op.in.pic_w = frame.width;
  op.in.pic_h = frame.height;
  op.in.block_w = frame.width;
  op.in.block_h = frame.height;
  op.in.srm_cm = PPA_SRM_COLOR_MODE_RGB565;

  op.out.buffer = scratch_;
  op.out.buffer_size = scratchBytes_;
  op.out.pic_w = picW;
  op.out.pic_h = picH;
  op.out.srm_cm = PPA_SRM_COLOR_MODE_RGB565;

  op.rotation_angle = angle;
  // Scale is expressed against the UNROTATED axes - it is applied before the
  // rotation, not after.
  op.scale_x = (float)outW / (float)frame.width;
  op.scale_y = (float)outH / (float)frame.height;
  op.mode = PPA_TRANS_MODE_BLOCKING;

  if (ppa_do_scale_rotate_mirror(gScaler, &op) != ESP_OK) return false;
  scratchW_ = picW;
  scratchH_ = picH;
  return true;
}

size_t JpegEncoder::encode(const CrowCamera::Frame &frame, uint16_t outW, uint16_t outH,
                           uint8_t quality) {
  if (!ready_ || frame.data == nullptr) return 0;

  const uint16_t *source = frame.data;
  size_t sourceBytes = frame.bytes;
  uint16_t width = frame.width;
  uint16_t height = frame.height;

  // Rotation alone is reason enough to go through the PPA, even when the size
  // already matches - otherwise a portrait capture would silently come out
  // landscape.
  if (outW != frame.width || outH != frame.height || rotation_ != 0) {
    if (scaleTo_(frame, outW, outH)) {
      source = scratch_;
      // scratchW_/H_ already account for the 90/270 axis swap.
      width = scratchW_;
      height = scratchH_;
      sourceBytes = (size_t)width * height * sizeof(uint16_t);
    } else {
      // Scaling is an optimisation, not a requirement. Encoding at native size
      // still produces a valid JPEG - just a bigger one - so say so and carry
      // on rather than dropping the frame.
      lastError_ = "scale failed; encoded at native size";
    }
  }

  jpeg_encode_cfg_t config = {};
  config.width = width;
  config.height = height;
  config.src_type = JPEG_ENCODE_IN_FORMAT_RGB565;
  // 4:2:0 halves chroma resolution for a large size saving the eye barely
  // notices. 4:4:4 would roughly double every file for no visible gain at this
  // sensor's resolution.
  config.sub_sample = JPEG_DOWN_SAMPLING_YUV420;
  config.image_quality = quality;

  uint32_t encoded = 0;
  if (jpeg_encoder_process(gEncoder, &config, (const uint8_t *)source, sourceBytes,
                           output_, outputBytes_, &encoded) != ESP_OK) {
    lastError_ = "JPEG encode failed";
    return 0;
  }
  return encoded;
}

#else  // not an ESP32-P4

bool JpegEncoder::begin(uint16_t, uint16_t) {
  lastError_ = "hardware JPEG needs an ESP32-P4";
  return false;
}
size_t JpegEncoder::encode(const CrowCamera::Frame &, uint16_t, uint16_t, uint8_t) {
  return 0;
}
bool JpegEncoder::scaleTo_(const CrowCamera::Frame &, uint16_t, uint16_t) { return false; }

#endif
