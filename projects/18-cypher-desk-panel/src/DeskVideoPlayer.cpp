#include "DeskVideoPlayer.h"

#include "DeskSystemServices.h"

#if USE_CYPHER_DESK_VIDEO && USE_CYPHER_DESK_SD && USE_DISPLAY && \
    defined(CONFIG_IDF_TARGET_ESP32P4)
#define CYPHER_DESK_VIDEO_BACKEND 1
#else
#define CYPHER_DESK_VIDEO_BACKEND 0
#endif

#if CYPHER_DESK_VIDEO_BACKEND

#include <Arduino_GFX_Library.h>
#include <CrowPanelShared.h>
#include <SD_MMC.h>
#include <driver/jpeg_decode.h>
#include <driver/ppa.h>
#include <esp_heap_caps.h>
#include <math.h>

namespace {

jpeg_decoder_handle_t gDecoder = nullptr;
ppa_client_handle_t gScaler = nullptr;
uint8_t *gFrameBuffer = nullptr;  // one compressed JPEG frame
size_t gFrameCapacity = 0;
uint16_t *gPixels = nullptr;  // decoded RGB565
size_t gPixelCapacity = 0;
uint8_t *gAudioChunk = nullptr;  // one interleaved PCM chunk
size_t gAudioCapacity = 0;

File gClip;
// Bound once to the global File, which is reused across opens - the source
// holds a reference, so it cannot be reassigned per clip.
DeskAviFileSource gSource(gClip);
DeskAviReader gReader;

uint16_t *panelFramebuffer() {
  Arduino_GFX *gfx = CrowDisplay::canvas();
  if (gfx == nullptr) return nullptr;
  return static_cast<Arduino_DSI_Display *>(gfx)->getFramebuffer();
}

constexpr int16_t kPanelW = 1024;
constexpr int16_t kPanelH = 600;
constexpr size_t kAudioChunkBytes = 8192;

}  // namespace

bool DeskVideoPlayer::begin(String &reason) {
  if (ready_) return true;

  jpeg_decode_engine_cfg_t engine = {};
  engine.intr_priority = 0;
  // Generous relative to a 512x288 frame, but a stalled decode must not wedge
  // the UI loop.
  engine.timeout_ms = 200;
  if (jpeg_new_decoder_engine(&engine, &gDecoder) != ESP_OK) {
    gDecoder = nullptr;
    reason = "hardware JPEG decoder unavailable";
    status_ = reason;
    return false;
  }

  if (gScaler == nullptr) {
    ppa_client_config_t config = {};
    config.oper_type = PPA_OPERATION_SRM;
    config.max_pending_trans_num = 1;
    if (ppa_register_client(&config, &gScaler) != ESP_OK) gScaler = nullptr;
  }

  const size_t maxPixels =
      static_cast<size_t>(CYPHER_DESK_VIDEO_MAX_W) * CYPHER_DESK_VIDEO_MAX_H;

  // The decoder wants its buffers from its own allocator (alignment and cache
  // requirements), the same way project 02's still viewer does.
  jpeg_decode_memory_alloc_cfg_t inputConfig = {};
  inputConfig.buffer_direction = JPEG_DEC_ALLOC_INPUT_BUFFER;
  gFrameBuffer = static_cast<uint8_t *>(
      jpeg_alloc_decoder_mem(maxPixels / 2, &inputConfig, &gFrameCapacity));

  jpeg_decode_memory_alloc_cfg_t outputConfig = {};
  outputConfig.buffer_direction = JPEG_DEC_ALLOC_OUTPUT_BUFFER;
  gPixels = static_cast<uint16_t *>(
      jpeg_alloc_decoder_mem(maxPixels * sizeof(uint16_t), &outputConfig, &gPixelCapacity));

  gAudioChunk = static_cast<uint8_t *>(heap_caps_malloc(kAudioChunkBytes, MALLOC_CAP_SPIRAM));
  if (gAudioChunk == nullptr) gAudioChunk = static_cast<uint8_t *>(malloc(kAudioChunkBytes));
  gAudioCapacity = gAudioChunk != nullptr ? kAudioChunkBytes : 0;

  if (gFrameBuffer == nullptr || gPixels == nullptr || gAudioChunk == nullptr) {
    reason = "out of memory for video playback";
    status_ = reason;
    return false;
  }

  ready_ = true;
  status_ = String("ready (frame ") + static_cast<uint32_t>(gFrameCapacity) + " B, pixels " +
            static_cast<uint32_t>(gPixelCapacity) + " B)";
  reason = status_;
  return true;
}

void DeskVideoPlayer::end() {
  stop();
  ready_ = false;
}

bool DeskVideoPlayer::probe(const String &path, DeskAviInfo &info, String &reason) {
  File file = SD_MMC.open(path, FILE_READ);
  if (!file) {
    reason = "cannot open";
    return false;
  }
  DeskAviFileSource source(file);
  DeskAviReader reader;
  const bool ok = reader.open(source, reason);
  if (ok) info = reader.info();
  reader.close();
  file.close();
  return ok;
}

void DeskVideoPlayer::setWindow(int16_t x, int16_t y, int16_t w, int16_t h) {
  windowX_ = x;
  windowY_ = y;
  windowW_ = w;
  windowH_ = h;
}

bool DeskVideoPlayer::play(const String &path, DeskAudioService *audio, String &reason) {
  if (!ready_ && !begin(reason)) return false;
  stop();

  gClip = SD_MMC.open(path, FILE_READ);
  if (!gClip) {
    reason = "cannot open clip";
    status_ = reason;
    return false;
  }
  if (!gReader.open(gSource, reason)) {
    gClip.close();
    status_ = reason;
    return false;
  }
  info_ = gReader.info();
  if (info_.width > CYPHER_DESK_VIDEO_MAX_W || info_.height > CYPHER_DESK_VIDEO_MAX_H) {
    reason = String("clip is ") + info_.width + "x" + info_.height + "; the decoder is sized for " +
             CYPHER_DESK_VIDEO_MAX_W + "x" + CYPHER_DESK_VIDEO_MAX_H;
    gReader.close();
    gClip.close();
    status_ = reason;
    return false;
  }

  audio_ = audio;
  if (info_.hasAudio && audio_ != nullptr) {
    if (!audio_->openRawStream(info_.audioRate, info_.audioChannels, info_.audioBits,
                               kDeskAudioOwnerMusic)) {
      // Losing the audio track is not worth refusing the clip over - say so and
      // fall back to the millis() clock.
      info_.hasAudio = false;
    }
  }

  path_ = path;
  playing_ = true;
  paused_ = false;
  endOfStream_ = false;
  pendingFrame_ = false;
  nextFrameIndex_ = 0;
  dropped_ = 0;
  presented_ = 0;
  startedMs_ = millis();
  pausedTotalMs_ = 0;
  status_ = info_.describe();
  reason = status_;
  return true;
}

void DeskVideoPlayer::stop() {
  if (!playing_) return;
  playing_ = false;
  paused_ = false;
  pendingFrame_ = false;
  gReader.close();
  if (gClip) gClip.close();
  if (audio_ != nullptr) audio_->stopPlayback();
  audio_ = nullptr;
  status_ = "stopped";
}

void DeskVideoPlayer::setPaused(bool paused) {
  if (!playing_ || paused == paused_) return;
  paused_ = paused;
  if (paused) pausedAtMs_ = millis();
  else pausedTotalMs_ += millis() - pausedAtMs_;
  if (audio_ != nullptr && info_.hasAudio) audio_->setPaused(paused);
}

// Microseconds of presentation time elapsed. Audio is the master clock when
// the clip has a track; otherwise fall back to the wall clock.
uint64_t DeskVideoPlayer::clockMicros() const {
  if (info_.hasAudio && audio_ != nullptr) {
    return (audio_->streamPlayedFrames() * 1000000ULL) / DeskAudioEngine::kOutputRate;
  }
  const uint32_t now = paused_ ? pausedAtMs_ : millis();
  return static_cast<uint64_t>(now - startedMs_ - pausedTotalMs_) * 1000ULL;
}

uint32_t DeskVideoPlayer::positionMs() const {
  return static_cast<uint32_t>(clockMicros() / 1000ULL);
}

void DeskVideoPlayer::pumpChunks() {
  while (true) {
    DeskAviReader::ChunkKind kind = DeskAviReader::kNone;
    uint32_t size = 0;
    if (!gReader.peek(kind, size)) {
      endOfStream_ = true;
      return;
    }
    if (kind == DeskAviReader::kAudio) {
      if (!info_.hasAudio || audio_ == nullptr) {
        gReader.skip();
        continue;
      }
      // Only read what the mixer ring can take, so a full ring stops the SD
      // read instead of forcing a partial push we would have to remember.
      const uint32_t frameBytes = info_.audioChannels * (info_.audioBits / 8);
      if (audio_->rawFreeFrames() * frameBytes < size || size > gAudioCapacity) return;
      const size_t got = gReader.read(gAudioChunk, gAudioCapacity);
      if (got == 0) return;
      audio_->pushRaw(gAudioChunk, got);
      continue;
    }
    if (kind == DeskAviReader::kVideo) {
      if (pendingFrame_) return;  // one frame in flight at a time
      pendingBytes_ = gReader.read(gFrameBuffer, gFrameCapacity);
      if (pendingBytes_ == 0) {
        // Oversized or short frame: reader already skipped it. Count it as
        // dropped and keep the frame clock moving so audio stays in sync.
        ++dropped_;
        ++nextFrameIndex_;
        continue;
      }
      pendingIndex_ = nextFrameIndex_++;
      pendingFrame_ = true;
      continue;
    }
    return;
  }
}

bool DeskVideoPlayer::presentPending() {
  uint16_t *framebuffer = panelFramebuffer();
  if (framebuffer == nullptr) return false;

  jpeg_decode_picture_info_t picture = {};
  if (jpeg_decoder_get_info(gFrameBuffer, pendingBytes_, &picture) != ESP_OK) {
    ++dropped_;
    return false;
  }
  const size_t needed = static_cast<size_t>(picture.width) * picture.height * sizeof(uint16_t);
  if (needed > gPixelCapacity) {
    ++dropped_;
    return false;
  }

  jpeg_decode_cfg_t config = {};
  config.output_format = JPEG_DECODE_OUT_FORMAT_RGB565;
  // Settled on hardware by project 02: this panel wants no byte swap.
  config.rgb_order = JPEG_DEC_RGB_ELEMENT_ORDER_RGB;
  config.conv_std = JPEG_YUV_RGB_CONV_STD_BT601;
  uint32_t decoded = 0;
  if (jpeg_decoder_process(gDecoder, &config, gFrameBuffer, pendingBytes_,
                           reinterpret_cast<uint8_t *>(gPixels), gPixelCapacity,
                           &decoded) != ESP_OK) {
    ++dropped_;
    return false;
  }

  // Letterbox inside the window, never stretch.
  const float scale = fminf(static_cast<float>(windowW_) / picture.width,
                            static_cast<float>(windowH_) / picture.height);
  const int16_t outW = static_cast<int16_t>(picture.width * scale);
  const int16_t outH = static_cast<int16_t>(picture.height * scale);
  const int16_t outX = windowX_ + (windowW_ - outW) / 2;
  const int16_t outY = windowY_ + (windowH_ - outH) / 2;

  bool drawn = false;
  if (gScaler != nullptr) {
    ppa_srm_oper_config_t operation = {};
    operation.in.buffer = gPixels;
    operation.in.pic_w = picture.width;
    operation.in.pic_h = picture.height;
    operation.in.block_w = picture.width;
    operation.in.block_h = picture.height;
    operation.in.srm_cm = PPA_SRM_COLOR_MODE_RGB565;
    operation.out.buffer = framebuffer;
    operation.out.buffer_size = static_cast<uint32_t>(kPanelW) * kPanelH * sizeof(uint16_t);
    operation.out.pic_w = kPanelW;
    operation.out.pic_h = kPanelH;
    operation.out.block_offset_x = outX;
    operation.out.block_offset_y = outY;
    operation.out.srm_cm = PPA_SRM_COLOR_MODE_RGB565;
    operation.rotation_angle = PPA_SRM_ROTATION_ANGLE_0;
    operation.scale_x = scale;
    operation.scale_y = scale;
    operation.mode = PPA_TRANS_MODE_BLOCKING;
    drawn = ppa_do_scale_rotate_mirror(gScaler, &operation) == ESP_OK;
  }
  if (!drawn) {
    // No scaler: nearest-neighbour by hand. Slower and softer, but a picture.
    for (int16_t row = 0; row < outH; ++row) {
      const uint32_t sourceRow = static_cast<uint32_t>(row / scale);
      if (sourceRow >= picture.height) break;
      const uint16_t *source = gPixels + sourceRow * picture.width;
      uint16_t *destination = framebuffer + static_cast<uint32_t>(outY + row) * kPanelW + outX;
      for (int16_t column = 0; column < outW; ++column) {
        const uint32_t sourceColumn = static_cast<uint32_t>(column / scale);
        destination[column] = source[sourceColumn < picture.width ? sourceColumn : 0];
      }
    }
  }

  // Flush only the video window. The panel is single-framebuffer with no page
  // flip, so a full-screen flush per frame tears the chrome around it
  // (hardware risk register row 23).
  CrowDisplay::flush(outX, outY, outW, outH);
  ++presented_;
  return true;
}

void DeskVideoPlayer::tick() {
  if (!playing_ || paused_) return;

  pumpChunks();

  if (pendingFrame_) {
    const uint64_t due = static_cast<uint64_t>(pendingIndex_) * info_.microSecPerFrame;
    const uint64_t now = clockMicros();
    if (now >= due) {
      // More than one frame late means we cannot catch up by rendering it;
      // dropping keeps the clip in sync instead of accumulating lag.
      if (now - due > info_.microSecPerFrame) ++dropped_;
      else presentPending();
      pendingFrame_ = false;
    }
  }

  if (endOfStream_ && !pendingFrame_) {
    if (loop_) {
      gReader.rewind();
      nextFrameIndex_ = 0;
      endOfStream_ = false;
      startedMs_ = millis();
      pausedTotalMs_ = 0;
      if (info_.hasAudio && audio_ != nullptr) {
        audio_->openRawStream(info_.audioRate, info_.audioChannels, info_.audioBits,
                              kDeskAudioOwnerMusic);
      }
      return;
    }
    // Let the queued audio play out before tearing the stream down.
    if (!info_.hasAudio || audio_ == nullptr || !audio_->playing()) stop();
  }
}

#else  // CYPHER_DESK_VIDEO_BACKEND

bool DeskVideoPlayer::begin(String &reason) {
  reason = USE_CYPHER_DESK_VIDEO ? "video needs display and SD" : "video disabled at compile time";
  status_ = reason;
  return false;
}
void DeskVideoPlayer::end() {}
bool DeskVideoPlayer::probe(const String &, DeskAviInfo &, String &reason) {
  reason = "video disabled";
  return false;
}
bool DeskVideoPlayer::play(const String &, DeskAudioService *, String &reason) {
  reason = status_;
  return false;
}
void DeskVideoPlayer::stop() {}
void DeskVideoPlayer::setPaused(bool) {}
void DeskVideoPlayer::setWindow(int16_t, int16_t, int16_t, int16_t) {}
void DeskVideoPlayer::tick() {}
uint64_t DeskVideoPlayer::clockMicros() const { return 0; }
uint32_t DeskVideoPlayer::positionMs() const { return 0; }
bool DeskVideoPlayer::presentPending() { return false; }
void DeskVideoPlayer::pumpChunks() {}

#endif  // CYPHER_DESK_VIDEO_BACKEND
