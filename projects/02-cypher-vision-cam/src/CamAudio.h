#ifndef VISION_CAM_AUDIO_H
#define VISION_CAM_AUDIO_H

#include "../config/ProjectConfig.h"
#include <Arduino.h>
#include <CrowPanelShared.h>

// Short confirmation sounds through the onboard NS4168 amp and speaker.
//
// A camera that gives no feedback when it captures is genuinely worse to use:
// the viewfinder keeps running, so there is nothing on screen that says "that
// happened". A shutter click is the oldest solution to that problem and it
// still works.
//
// Cues are SYNTHESIZED, not sampled. A convincing shutter is a few milliseconds
// of filtered noise under a fast decay envelope - a couple of dozen lines and no
// asset to ship, keep on the card, or fail to find. It also means the sounds
// work with no SD card present, which matters because a missing card is exactly
// when you want the error cue.
//
// PLAYBACK RUNS ON ITS OWN TASK. Writing a 120 ms sound to I2S from loop()
// would block the render loop for 120 ms - the same mistake as the ISP
// statistics stalls that once made the touchscreen look dead. play() only posts
// a request and returns immediately.

enum class CamSound : uint8_t {
  Shutter = 0,   // photo captured
  RecordStart,   // clip opened
  RecordStop,    // clip closed
  Error,         // capture or card failure
};

class CamAudio {
 public:
  // Brings up I2S and the amplifier. Returns false if audio is unavailable;
  // every later call is then a no-op, because a camera with no speaker should
  // still be a camera.
  bool begin(const HardwareProfile &profile);

  // Queues a cue. Returns immediately - never blocks the caller.
  void play(CamSound sound);

  bool ready() const { return ready_; }
  bool enabled() const { return enabled_; }
  void setEnabled(bool on) { enabled_ = on; }

  // 0-100. Defaults high because the onboard speaker is small and a camera cue
  // has to be audible over whatever room you are standing in.
  void setVolume(uint8_t percent) { volume_ = percent > 100 ? 100 : percent; }
  uint8_t volume() const { return volume_; }
  const char *lastError() const { return lastError_; }

 private:
  static void taskTrampoline_(void *self);
  void taskLoop_();
  void render_(CamSound sound);

  bool ready_ = false;
  bool enabled_ = true;
  uint8_t volume_ = 90;
  const char *lastError_ = "not started";
  void *txChan_ = nullptr;
  void *queue_ = nullptr;
};

#endif
