#ifndef CYPHER_DESK_VIDEO_APPLICATION_H
#define CYPHER_DESK_VIDEO_APPLICATION_H

#include "DeskApplication.h"
#include "DeskMediaLibrary.h"
#include "DeskVideoPlayer.h"

// The Video app: browse /cypher-puter/desk/video, play an MJPEG AVI with its
// audio track, scrub, and toggle the chrome away for a full-screen view.
//
// Plays clips produced by scripts/convert-crowpanel-video.sh, and also plays
// project 02's own VID_*.AVI recordings - those have no audio stream, so they
// run silently off the wall clock rather than being refused.
class DeskVideoApplication : public DeskApplication {
 public:
  DeskAppId id() const override { return kDeskAppVideo; }
  const char *title() const override { return "Video"; }
  void begin(DeskAppContext &context) override;
  void onEnter() override;
  void onExit() override;
  void tick(uint32_t nowMs) override;
  bool handleTouch(const DeskTouchEvent &event) override;
  void draw() override;
  bool handleBack() override;

  bool dirty() const { return dirty_; }
  void clearDirty() { dirty_ = false; }
  void command(const String &args, Print &out);
  bool playing() const { return player_.playing(); }

 private:
  enum View : uint8_t { kLibrary, kPlayer };

  DeskAppContext *context_ = nullptr;
  DeskMediaLibrary library_;
  DeskVideoPlayer player_;
  View view_ = kLibrary;
  bool chromeVisible_ = true;
  bool dirty_ = true;
  uint8_t selected_ = 0;
  uint8_t pageOffset_ = 0;
  uint8_t current_ = 0xFF;
  uint32_t lastChromeMs_ = 0;
  String status_;

  void refresh();
  bool play(uint8_t index);

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  void drawLibrary();
  void drawPlayer();
  void drawChrome();
#endif
};

#endif
