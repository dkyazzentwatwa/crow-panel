#ifndef ACID_GLASS_REMOTE_H
#define ACID_GLASS_REMOTE_H

#include "../config/ProjectConfig.h"
#include "AcidGlassTypes.h"

class AcidGlassAudio;
class AcidGlassVisuals;
class Print;

class AcidGlassRemote {
 public:
  using ControlHandler = bool (*)(void *context, const ControlEvent &event);

  bool begin(void *context, ControlHandler handler, const AcidGlassState *state,
             const AcidGlassAudio *audio, const AcidGlassVisuals *visuals);
  void tick();
  void end();
  void printStatus(Print &out) const;
  bool ready() const { return ready_; }
  const char *ssid() const { return ssid_; }
  const char *url() const { return url_; }
  uint8_t clients() const;

 private:
  void *context_ = nullptr;
  ControlHandler handler_ = nullptr;
  const AcidGlassState *state_ = nullptr;
  const AcidGlassAudio *audio_ = nullptr;
  const AcidGlassVisuals *visuals_ = nullptr;
  void *server_ = nullptr;
  bool ready_ = false;
  char ssid_[24] = {};
  char url_[32] = {};

  void servePage_();
  void serveHealth_();
  void serveState_();
  void handleControl_(bool presetRoute);
};

#endif
