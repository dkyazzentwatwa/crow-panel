#ifndef CYPHER_DESK_APPLICATION_H
#define CYPHER_DESK_APPLICATION_H

#include "DeskOsTypes.h"
#include "DeskTouchKeyboard.h"

class DeskEventBus;
class DeskStorageService;
class DeskWifiService;
class DeskSettings;
class DeskTouchKeyboard;
class DeskTimeService;
class DeskAudioService;
class DeskWeatherService;
class DeskAppRouter;
class DeskWriterApplication;

struct DeskAppContext {
  DeskEventBus *events = nullptr;
  DeskStorageService *storage = nullptr;
  DeskWifiService *wifi = nullptr;
  DeskSettings *settings = nullptr;
  DeskTouchKeyboard *keyboard = nullptr;
  class DeskTouch *touch = nullptr;
  DeskTimeService *time = nullptr;
  DeskAudioService *audio = nullptr;
  DeskWeatherService *weather = nullptr;
  DeskAppRouter *router = nullptr;
  DeskWriterApplication *writer = nullptr;
};

class DeskApplication {
 public:
  virtual ~DeskApplication() = default;
  virtual DeskAppId id() const = 0;
  virtual const char *title() const = 0;
  virtual void begin(DeskAppContext &context) = 0;
  virtual void onEnter() {}
  virtual void onExit() {}
  virtual void tick(uint32_t nowMs) = 0;
  virtual bool handleTouch(const DeskTouchEvent &event) = 0;
  virtual void draw() = 0;
  virtual bool handleBack() { return false; }
  virtual void handleSerial(const String &, Print &) {}

  // True while the on-screen keyboard is up. CypherDeskOs only services the
  // keyboard against the touch tracker when the active app says so, which is
  // what keeps a stray tap in the lower half of a non-typing screen from being
  // read as a keystroke.
  virtual bool keyboardVisible() const { return false; }
  // Delivered for each key the keyboard produced this tick. Presses fire on
  // touch-DOWN; app chrome still commits on release.
  virtual void handleKey(const DeskKeyEvent &) {}
};

#endif
