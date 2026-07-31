#ifndef CYPHER_DESK_OS_H
#define CYPHER_DESK_OS_H

#include "DeskAppRouter.h"
#include "DeskSplash.h"
#if USE_CYPHER_DESK_MEDIA
#include "DeskMusicApplication.h"
#endif
#if USE_CYPHER_DESK_VIDEO
#include "DeskVideoApplication.h"
#endif
#include "DeskEventBus.h"
#include "DeskSettings.h"
#include "DeskSystemServices.h"
#include "DeskTouchKeyboard.h"
#include "DeskUtilityApplication.h"
#include "DeskWriterApplication.h"

class CypherDeskOs {
 public:
  CypherDeskOs();
  void begin();
  void tick();
  void printStatus(Print &out) const;
  void printFiles(Print &out);
  void printTouchDiagnostics(Print &out) const;

  void commandApp(const String &args, Print &out);
  void commandApps(Print &out) const;
  void commandWifi(const String &args, Print &out);
  void commandEvents(Print &out) const;
  void commandCalculator(const String &args, Print &out);
  void commandCalendar(const String &args, Print &out);
  void commandContacts(const String &args, Print &out);
  void commandAlarm(const String &args, Print &out);
  void commandMedia(const String &args, Print &out);
  void commandVideo(const String &args, Print &out);
  void commandAudio(const String &args, Print &out);
  void commandRecovery(const String &args, Print &out);
  void commandWeather(const String &args, Print &out);

  // Backward-compatible writer serial surface.
  void commandNew(const String &args);
  void commandOpen(const String &args);
  void commandType(const String &args);
  void commandSave();
  void commandBack();
  void commandDemo();
  void commandPage(const String &args);
  void commandDaily();
  void commandScrap();
  void commandFocus(const String &args);
  void commandRitual(const String &args);
  void commandTheme(const String &args);
  void commandSound(const String &args);
  void commandStats(Print &out);
  void commandSearch(const String &args, Print &out);
  void commandFind(const String &args, Print &out);
  void commandTime(const String &args, Print &out);
  void commandStorage(const String &args, Print &out);

 private:
  DeskEventBus events_;
  DeskDisplayService display_;
  // One touch source for the whole product. The Writer used to run a second,
  // press-edge loop over the same panel while the OS used release-edge, so the
  // launcher and the editor never felt the same.
  DeskTouch touch_;
  DeskStorageService storage_;
  DeskWifiService wifi_;
  DeskTimeService time_;
  DeskWeatherService weatherService_;
  DeskAudioService audio_;
  DeskSettings settings_;
  DeskTouchKeyboard keyboard_;
  DeskAppRouter router_;
  DeskWriterApplication writer_;
  DeskUtilityApplication today_;
  DeskUtilityApplication calendar_;
  DeskUtilityApplication contacts_;
  DeskUtilityApplication clock_;
  DeskUtilityApplication calculator_;
  DeskUtilityApplication files_;
  DeskUtilityApplication settingsApp_;
  DeskUtilityApplication recorder_;
  // Apps whose feature is compiled out are neither built nor registered, so a
  // silent or video-less binary does not carry their UI - and the launcher
  // never shows a tile that could only report that it does not work.
#if USE_CYPHER_DESK_MEDIA
  DeskMusicApplication music_;
  DeskMusicApplication podcasts_;
#endif
#if USE_CYPHER_DESK_VIDEO
  DeskVideoApplication video_;
#endif
  DeskUtilityApplication weather_;
  DeskAppContext context_;
  bool dirty_ = true;
  uint32_t lastStatusRefreshMs_ = 0;
  int16_t nowPlayingPillX_ = 0;
  int16_t nowPlayingPillW_ = 0;
  uint32_t lastActivityMs_ = 0;
  bool dimmed_ = false;
  DeskAppId lastApp_ = kDeskAppHome;
  // Tiles actually available in this build, filled at begin() from what
  // registered. Keeps drawHome() and handleHomeTouch() on one list.
  const struct DeskAppDescriptor *homeTiles_[16] = {};
  uint8_t homeTileCount_ = 0;

  void registerApplications();
  void ensureWriterOpen();
  void drawHome();
  void drawHomeStatus();
  String nowPlayingLabel() const;
  void drawActive();
  void flushFrame();
  void serviceKeyboard(DeskApplication *active);
  void serviceIdleDim(uint32_t nowMs);
  void deliverTaps(DeskApplication *active, DeskAppId current);
  void handleHomeTouch(const DeskTouchEvent &event);
  DeskAppId appIdFromName(String name) const;
  DeskUtilityApplication *utility(DeskAppId id);
#if USE_CYPHER_DESK_MEDIA
  DeskMusicApplication *music(DeskAppId id);
#endif
#if USE_CYPHER_DESK_VIDEO
  DeskVideoApplication *videoApp(DeskAppId id);
#endif
};

#endif
