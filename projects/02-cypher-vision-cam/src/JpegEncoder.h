#ifndef VISION_CAM_JPEG_ENCODER_H
#define VISION_CAM_JPEG_ENCODER_H

#include "../config/ProjectConfig.h"
#include <Arduino.h>
#include <CrowPanelShared.h>

// RGB565 frame -> JPEG, using the ESP32-P4's hardware encoder, with an optional
// hardware down-scale (PPA) on the way in.
//
// Extracted from CamRecorder because the recorder is SD-gated and the stream
// server needs exactly the same operation without a card present. One encoder
// instance is shared by both: the JPEG peripheral is a single hardware block,
// and its working buffers are large enough (up to ~600 KB) that a second copy
// would be pure waste.
//
// Not thread-safe and not reentrant - it owns one input scratch buffer and one
// output buffer. Both consumers run from loop(), so a single owner passed to
// each is the whole synchronisation story.

class JpegEncoder {
 public:
  // `maxWidth`/`maxHeight` size the output buffer for the largest frame that
  // will ever be encoded (the full sensor frame, for stills).
  bool begin(uint16_t maxWidth, uint16_t maxHeight);

  // Encodes `frame`, scaling to outW x outH first when they differ from the
  // frame's own size. Returns encoded byte count, or 0 on failure.
  //
  // The result lives in data() and stays valid only until the next encode() -
  // callers write it out immediately rather than holding it.
  size_t encode(const CrowCamera::Frame &frame, uint16_t outW, uint16_t outH,
                uint8_t quality);

  // Rotation applied to everything encoded from here on, in degrees
  // counter-clockwise (0, 90, 180, 270).
  //
  // This exists for portrait shooting. The sensor always delivers 1024x600 with
  // its long axis first; a file written that way opens rotated on a computer,
  // which assumes the first axis is horizontal. Turning the device does not
  // change the buffer layout - only rotating the pixels does. At 90 or 270 the
  // encoded dimensions swap, so a portrait still is a true 600x1024 JPEG rather
  // than a landscape one with a note attached.
  void setRotation(uint16_t degrees) { rotation_ = degrees; }
  uint16_t rotation() const { return rotation_; }

  const uint8_t *data() const { return output_; }
  bool ready() const { return ready_; }
  const char *lastError() const { return lastError_; }

  // True when the hardware scaler is available. Without it, encode() falls back
  // to the frame's native size and the caller gets a larger JPEG than asked
  // for - worth surfacing rather than silently producing oversized files.
  bool canScale() const { return canScale_; }

 private:
  bool scaleTo_(const CrowCamera::Frame &frame, uint16_t outW, uint16_t outH);

  uint16_t *scratch_ = nullptr;   // scaled RGB565 input
  size_t scratchBytes_ = 0;
  uint16_t scratchW_ = 0;
  uint16_t scratchH_ = 0;
  uint8_t *output_ = nullptr;     // encoded JPEG
  size_t outputBytes_ = 0;
  bool ready_ = false;
  bool canScale_ = false;
  uint16_t rotation_ = 0;
  const char *lastError_ = "not started";
};

#endif
