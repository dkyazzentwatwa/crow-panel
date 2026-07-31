#ifndef CYPHER_DESK_MUSIC_APPLICATION_H
#define CYPHER_DESK_MUSIC_APPLICATION_H

#include "DeskApplication.h"
#include "DeskMediaLibrary.h"

// Music and Podcasts: one class, two DeskAppIds, different directory.
//
// Split out of DeskUtilityApplication, which was already 1,141 lines running
// eleven apps through one switch (appId_). What it had for these two was a
// five-row file list, a STOP button, and a caption that claimed "16 kHz MONO
// PCM ONLY" no matter what was on the card.
//
// Two views:
//   LIBRARY  - paged list with each track's real duration and format, and an
//              explicit reason next to anything that will not play.
//   NOW      - title, scrub bar you can drag, transport, shuffle/repeat, and
//              a volume stepper.
class DeskMusicApplication : public DeskApplication {
 public:
  explicit DeskMusicApplication(DeskAppId appId);

  DeskAppId id() const override;
  const char *title() const override;
  void begin(DeskAppContext &context) override;
  void onEnter() override;
  void tick(uint32_t nowMs) override;
  bool handleTouch(const DeskTouchEvent &event) override;
  void draw() override;
  bool handleBack() override;

  bool dirty() const;
  void clearDirty();

  // Serial surface, so an injected command drives the same state a tap does.
  void command(const String &args, Print &out);
  // What the launcher's now-playing pill shows.
  String nowPlayingLabel() const;
  bool playing() const;

 private:
  enum View : uint8_t { kLibrary, kNowPlaying };
  enum Repeat : uint8_t { kRepeatOff, kRepeatAll, kRepeatOne };
  static constexpr uint8_t kRowsPerPage = 6;

  DeskAppId appId_;
  DeskAppContext *context_ = nullptr;
  DeskMediaLibrary library_;
  View view_ = kLibrary;
  Repeat repeat_ = kRepeatOff;
  bool shuffle_ = false;
  uint8_t selected_ = 0;
  uint8_t pageOffset_ = 0;
  uint8_t current_ = 0xFF;  // index of the track that is loaded
  bool dirty_ = true;
  uint32_t lastProgressMs_ = 0;
  String status_;

  const char *directory() const;
  void refresh();
  bool play(uint8_t index);
  void advance(bool forward);
  void trackFinished();

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  void drawShell(const String &subtitle);
  void drawLibrary();
  void drawNowPlaying();
  void drawProgress();  // partial redraw of the scrub row only
#endif
};

#endif
