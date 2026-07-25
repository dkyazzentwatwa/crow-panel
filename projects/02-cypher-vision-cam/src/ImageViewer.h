#ifndef VISION_CAM_IMAGE_VIEWER_H
#define VISION_CAM_IMAGE_VIEWER_H

#include "../config/ProjectConfig.h"
#include <Arduino.h>
#include <CrowPanelShared.h>

// Shows a stored JPEG on the panel, using the ESP32-P4's hardware JPEG decoder
// and the PPA scaler.
//
// Both are the same blocks the capture path uses in the other direction, so
// this costs no new silicon and very little code: read the file, hand it to the
// decoder, scale the result to fit the screen, blit.
//
// Deliberately synchronous and one-shot. Opening a picture is a deliberate act
// with an obvious "now I am looking at a photo" mode - there is no reason to
// stream it or keep it live, and while a still is on screen the viewfinder is
// not needed. Decoding a 1024x600 JPEG takes single-digit milliseconds on this
// hardware, so the loop hitch is not perceptible.

class ImageViewer {
 public:
  // Allocates the decoder and its working buffers, sized for the largest image
  // this camera produces. Safe to call more than once.
  bool begin(uint16_t maxWidth, uint16_t maxHeight);

  // Reads `path` from SD, decodes it, and draws it centred and aspect-correct
  // on the panel. Returns false and sets lastError() on any failure.
  bool show(const char *path);

  // True while an image is displayed. The UI uses this to route the next tap to
  // "close the picture" rather than to whatever is underneath.
  bool showing() const { return showing_; }
  void close() { showing_ = false; }

  const char *lastError() const { return lastError_; }
  const String &currentName() const { return currentName_; }

 private:
  bool ready_ = false;
  bool showing_ = false;
  String currentName_;
  const char *lastError_ = "not started";

  uint8_t *fileBuf_ = nullptr;    // compressed JPEG, read from SD
  size_t fileBufBytes_ = 0;
  uint16_t *pixelBuf_ = nullptr;  // decoded RGB565
  size_t pixelBufBytes_ = 0;
};

#endif
