#ifndef VISION_CAM_RECORDER_H
#define VISION_CAM_RECORDER_H

#include "../config/ProjectConfig.h"
#include <Arduino.h>
#include <CrowPanelShared.h>
#include "JpegEncoder.h"

// Stills and video clips to the SD card, using the P4's hardware JPEG encoder.
//
// Stills land in /DCIM as CAM_00001.JPG. Clips land in /DCIM as VID_00001.AVI,
// written as Motion-JPEG in an AVI container so they play in VLC or QuickTime
// with no post-processing - the alternative (a bag of numbered JPEGs) is not
// something anyone can watch.
//
// THE BINDING CONSTRAINT IS SD WRITE SPEED, not the encoder. 1-bit SD_MMC
// sustains roughly 700 KB/s. A full-resolution 1024x600 frame at q75 is about
// 70 KB, so recording natively at 10 fps would need ~700 KB/s with zero
// headroom. The recorder therefore scales frames down to the configured record
// size (PPA, in hardware) before encoding. Every one of these numbers is a
// target, not a measurement: the REC HUD reports the fps actually achieved and
// counts frames dropped for being late, rather than implying the target was met.

class CamRecorder {
 public:
  // MUST be called BEFORE CrowDisplay::begin(). Mounting SD_MMC after the DSI
  // framebuffer is live leaves this panel backlit but blank - device-proven on
  // Project 20, and the reason this is a separate call from begin().
  bool mountStorage();

  // Takes the shared encoder (the sketch owns it; the stream server uses the
  // same one) and allocates the clip index. Call after the camera is up. Safe
  // to call when storage never mounted; the recorder then reports why rather
  // than failing silently.
  bool begin(JpegEncoder *encoder);

  // --- Stills --------------------------------------------------------------

  // Encodes `frame` and writes it as the next CAM_*.JPG. Returns false and
  // sets lastError() on any failure.
  bool captureStill(const CrowCamera::Frame &frame);

  // --- Clips ---------------------------------------------------------------

  bool startClip();
  bool stopClip();
  bool recording() const { return recording_; }

  // Offer a frame to an in-progress recording. Frames arriving faster than the
  // configured record rate are skipped (not counted as drops - they were never
  // wanted). A frame that misses its slot because the card was still busy IS
  // counted, because that is the number that tells you the card cannot keep up.
  void offerFrame(const CrowCamera::Frame &frame);

  uint32_t clipElapsedSec() const;
  uint32_t clipFrames() const { return clipFrames_; }
  uint32_t droppedFrames() const { return droppedFrames_; }
  float measuredClipFps() const;

  // --- Gallery -------------------------------------------------------------

  struct MediaEntry {
    char name[24];
    uint32_t bytes;
    bool isVideo;
  };
  static constexpr uint8_t kMaxListed = 64;

  // Rescans /DCIM. Returns the number of entries found (capped at kMaxListed,
  // which the UI reports rather than pretending the list is complete).
  uint8_t refreshMediaList();
  uint8_t mediaCount() const { return mediaCount_; }
  bool mediaTruncated() const { return mediaTruncated_; }
  const MediaEntry &mediaAt(uint8_t index) const { return media_[index]; }

  // --- Status --------------------------------------------------------------
  bool storageReady() const { return storageReady_; }
  uint64_t freeBytes() const;
  const char *lastError() const { return lastError_; }
  void printStatus(Print &out) const;

 private:
#if USE_CAM_SD
  uint32_t nextIndexFor_(const char *prefix, const char *extension);

  bool writeAviHeader_(uint16_t width, uint16_t height, uint8_t fps);
  bool finalizeAvi_();
#endif

  JpegEncoder *encoder_ = nullptr;
  bool storageReady_ = false;
  bool recording_ = false;
  uint32_t clipStartMs_ = 0;
  uint32_t clipFrames_ = 0;
  uint32_t droppedFrames_ = 0;
  uint32_t lastFrameMs_ = 0;
  uint32_t stillIndex_ = 1;
  uint32_t clipIndex_ = 1;
  uint8_t mediaCount_ = 0;
  bool mediaTruncated_ = false;
  MediaEntry media_[kMaxListed];
  const char *lastError_ = "not started";
};

#endif
