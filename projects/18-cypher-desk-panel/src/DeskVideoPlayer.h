#ifndef CYPHER_DESK_VIDEO_PLAYER_H
#define CYPHER_DESK_VIDEO_PLAYER_H

#include "../config/ProjectConfig.h"
#include "DeskAviReader.h"
#include <Arduino.h>

class DeskAudioService;

// MJPEG playback: SD -> hardware JPEG decoder -> PPA scale -> framebuffer,
// with the audio track presented as the clock.
//
// No third-party dependency is needed. Core 3.3.8 already links
// libesp_driver_jpeg.a and libesp_driver_ppa.a for the ESP32-P4; project 02
// uses both for stills and for the camera viewfinder, and this is the same
// path in reverse.
//
// SYNC MODEL: audio is the master. Video frame N is presented when the audio
// engine's played-frame counter passes N * microSecPerFrame; a frame that is
// already late is dropped rather than queued, so a slow card or a heavy UI
// frame costs a dropped frame and never a growing lag. A clip with no audio
// track falls back to millis().
//
// THREADING: everything here runs in loop context. The mixer task only drains
// the ring this pushes into - it never touches the card.
class DeskVideoPlayer {
 public:
  bool begin(String &reason);
  void end();
  bool ready() const { return ready_; }

  // Reads the clip's headers without starting playback, for the library list.
  static bool probe(const String &path, DeskAviInfo &info, String &reason);

  bool play(const String &path, DeskAudioService *audio, String &reason);
  void stop();
  bool playing() const { return playing_; }
  void setPaused(bool paused);
  bool paused() const { return paused_; }
  void setLoop(bool loop) { loop_ = loop; }

  // Call every loop. Pumps chunks from the card and presents due frames.
  void tick();

  // Where the video lands on the panel. Set before play(); the frame is
  // letterboxed inside this rectangle, never stretched.
  void setWindow(int16_t x, int16_t y, int16_t w, int16_t h);

  const DeskAviInfo &info() const { return info_; }
  uint32_t positionMs() const;
  uint32_t droppedFrames() const { return dropped_; }
  uint32_t presentedFrames() const { return presented_; }
  String status() const { return status_; }

 private:
  bool ready_ = false;
  bool playing_ = false;
  bool paused_ = false;
  bool loop_ = false;
  bool pendingFrame_ = false;
  bool endOfStream_ = false;
  uint32_t pendingIndex_ = 0;
  size_t pendingBytes_ = 0;
  uint32_t nextFrameIndex_ = 0;
  uint32_t dropped_ = 0;
  uint32_t presented_ = 0;
  uint32_t startedMs_ = 0;
  uint32_t pausedAtMs_ = 0;
  uint32_t pausedTotalMs_ = 0;
  int16_t windowX_ = 0, windowY_ = 96, windowW_ = 1024, windowH_ = 408;
  DeskAviInfo info_;
  DeskAudioService *audio_ = nullptr;
  String status_ = "idle";
  String path_;

  uint64_t clockMicros() const;
  bool presentPending();
  void pumpChunks();
};

#endif
