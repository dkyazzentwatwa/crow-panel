#ifndef CYPHER_DESK_UTILITY_APPLICATION_H
#define CYPHER_DESK_UTILITY_APPLICATION_H

#include "DeskApplication.h"
#include "DeskOsTypes.h"
#include "DeskSystemServices.h"

class DeskUtilityApplication : public DeskApplication {
 public:
  explicit DeskUtilityApplication(DeskAppId appId);
  DeskAppId id() const override;
  const char *title() const override;
  void begin(DeskAppContext &context) override;
  void onEnter() override;
  void tick(uint32_t nowMs) override;
  bool handleTouch(const DeskTouchEvent &event) override;
  void draw() override;
  bool handleBack() override;
  void handleSerial(const String &command, Print &out) override;
  bool dirty() const;
  void clearDirty();
  void refreshDynamic();
  bool alarmEnabled() const;

 private:
  enum InputPurpose : uint8_t {
    kInputNone,
    kInputCalendarTitle,
    kInputCalendarEditTitle,
    kInputCalendarDate,
    kInputCalendarTime,
    kInputCalendarNotes,
    kInputContactName,
    kInputContactEditName,
    kInputContactOrganization,
    kInputContactPhone,
    kInputContactEmail,
    kInputContactNotes,
    kInputContactSearch,
    kInputFileFolder,
    kInputFileRename,
    kInputFileCopy,
    kInputFileMove,
    kInputWifiPassword,
    kInputHiddenSsid,
    kInputHiddenPassword
  };

  DeskAppId appId_;
  DeskAppContext *context_ = nullptr;
  bool dirty_ = true;
  uint32_t lastRefreshMs_ = 0;
  uint32_t lastLiveRefreshMs_ = 0;
  String settingsSnapshot_;
  String weatherSnapshot_;
  InputPurpose inputPurpose_ = kInputNone;
  String input_;
  String pendingSsid_;
  String contactQuery_;
  String pendingCalendarDate_;
  String fileDirectory_;
  String filePreview_;
  String pendingFilePath_;
  DeskStorageService::FileEntry fileEntries_[12];
  uint8_t fileEntryCount_ = 0;
  bool diagnosticsVisible_ = false;
  bool calendarListView_ = false;
  bool calendarMonthInitialized_ = false;
  int16_t calendarYear_ = 2026;
  uint8_t calendarMonth_ = 7;
  bool confirmingDelete_ = false;
  enum DeleteKind : uint8_t { kDeleteNone, kDeleteFile, kDeleteCalendar, kDeleteContact };
  DeleteKind deleteKind_ = kDeleteNone;
  uint8_t selected_ = 0;
  String calcDisplay_ = "0";
  double calcAccumulator_ = 0;
  char calcOperation_ = 0;
  bool calcFresh_ = true;
  bool stopwatchRunning_ = false;
  uint32_t stopwatchStartedMs_ = 0;
  uint32_t stopwatchElapsedMs_ = 0;
  uint16_t timerMinutes_ = 10;
  bool timerRunning_ = false;
  uint32_t timerStartedMs_ = 0;
  bool alarmEnabled_ = false;
  uint16_t alarmMinuteOfDay_ = 8 * 60;

  void beginInput(InputPurpose purpose, const String &initial = "");
  void finishInput();
  void cancelInput();
  void handleKeyboard(int16_t x, int16_t y);
  void pressCalculator(const String &key);
  void loadClockSettings();
  void saveClockSettings();
  void drawShell(const String &subtitle);
  void drawStatusBar();
  void drawClockDynamic();
  void drawSettingsDynamic();
  void drawRecorderDynamic();
  void refreshFiles();
  void drawDeleteConfirm();
  bool selectedContactMatches(uint8_t index) const;
  String settingsSnapshot() const;
  String weatherSnapshot() const;
  void drawInput();
  void drawToday();
  void drawCalendar();
  void drawCalendarMonth();
  void drawCalendarList();
  void drawContacts();
  void drawClock();
  void drawCalculator();
  void drawFiles();
  void drawSettings();
  void drawRecorder();
  void drawMusic();
  void drawPodcasts();
  void drawWeather();
  void initializeCalendarMonth();
  void shiftCalendarMonth(int8_t delta);
};

#endif
