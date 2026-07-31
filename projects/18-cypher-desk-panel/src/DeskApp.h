#ifndef CYPHER_DESK_PANEL_APP_H
#define CYPHER_DESK_PANEL_APP_H

#include "../config/ProjectConfig.h"
#include "DeskClock.h"
#include "DeskNavigator.h"
#include "DeskPrompts.h"
#include "DeskSettings.h"
#include "DeskStorage.h"
#include "DeskTextWrap.h"
#include "DeskTheme.h"
#include "DeskTouchKeyboard.h"

class DeskApp {
 public:
  void begin(bool initializeDisplay = true, class DeskWifiService *wifi = nullptr,
             class DeskStorageService *storageService = nullptr,
             class DeskAudioService *audio = nullptr,
             class DeskTouch *touch = nullptr);
  void tick();
  bool consumeOsHomeRequest();
  void reloadPreferences();
  void printStatus(Print &out) const;
  void printFiles(Print &out) const;
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
  void commandStats(Print &out) const;
  void commandSearch(const String &args, Print &out) const;
  void commandFind(const String &args, Print &out);
  void commandTime(const String &args, Print &out);
  void commandStorage(const String &args, Print &out);
  void printTouchDiagnostics(Print &out) const;

 private:
  enum ConfirmAction { kConfirmNone, kConfirmDelete, kConfirmDiscard, kConfirmSafeEject };

  DeskStorage storage_;
  DeskSettings settings_;
  DeskClock clock_;
  // The OS owns audio now: one DeskAudioService over one I2S channel. The
  // Writer used to carry its own DeskAudio with a second I2S instance on the
  // same pins, arbitrated by nothing.
  class DeskAudioService *audio_ = nullptr;
  DeskPrompts prompts_;
  DeskNavigator navigator_;
  DeskTouchKeyboard keyboard_;
  DeskEditorState editor_;
  // Soft word wrap. Rebuilt whenever the buffer changes, so the renderer,
  // the cursor and tap-to-place all agree on where the lines are.
  DeskTextWrap wrap_;
  bool wrapStale_ = true;
  String findQuery_;
  uint16_t findMatches_ = 0;
  DeskDocument notes_[CYPHER_DESK_MAX_NOTES];
  DeskFolder folders_[CYPHER_DESK_MAX_FOLDERS];
  uint16_t noteCount_ = 0;
  uint8_t folderCount_ = 0;
  uint16_t selected_ = 0;
  uint16_t pageOffset_ = 0;
  bool hasMore_ = false;
  String activeFolderPath_;
  String activeFolderTitle_;
  String messageTitle_;
  String messageBody_;
  DeskPage messageReturn_ = kDeskPageDesk;
  DeskPage editorReturn_ = kDeskPageDesk;
  DeskArchiveFilter archiveFilter_ = kDeskArchiveRecent;
  String archiveQuery_;
  bool archiveTagOnly_ = false;
  DeskInputPurpose inputPurpose_ = kDeskInputNone;
  String inputBuffer_;
  String pendingExtension_ = ".md";
  String pendingPath_;
  ConfirmAction confirmAction_ = kConfirmNone;
  String confirmTitle_;
  String confirmBody_;
  bool editingScrap_ = false;
  bool focusActive_ = false;
  bool focusPaused_ = false;
  uint16_t focusMinutes_ = 20;
  uint32_t focusStartedMs_ = 0;
  uint32_t focusPausedAtMs_ = 0;
  uint32_t focusPausedTotalMs_ = 0;
  uint32_t focusStartWords_ = 0;
  bool dirty_ = true;
  bool keyboardDirty_ = true;
  class DeskTouch *touch_ = nullptr;
  uint32_t touchCount_ = 0;
  bool osHomeRequested_ = false;
  int16_t lastRawX_ = 0;
  int16_t lastRawY_ = 0;
  int16_t lastTouchX_ = 0;
  int16_t lastTouchY_ = 0;

  const DeskThemePalette &theme() const;
  bool keyboardVisible() const;
  // Null-safe views onto the shared audio service. The Writer can be opened in
  // a build with no audio backend at all, so every read goes through these.
  void applyAudioPreferences();
  String audioStatus() const;
  const char *audioAmbienceName() const;
  const char *audioKeySoundName() const;
  void route(DeskPage page);
  void refreshNotes();
  uint16_t searchArchive(uint16_t offset, DeskDocument *out, uint16_t maxCount, bool *hasMore) const;
  String currentDirectory() const;
  void setMessage(const String &title, const String &body, DeskPage returnPage);
  void openFolder(const DeskFolder &folder);
  void beginTextInput(DeskInputPurpose purpose, const String &initial = "");
  void completeTextInput();
  void askConfirm(ConfirmAction action, const String &title, const String &body);
  void completeConfirm(bool accepted);
  void createNamedNote(const String &name, const char *extension);
  void createNote(const char *extension);
  void openNote(const DeskDocument &document, DeskPage returnPage = kDeskPageDesk);
  void openDaily();
  void openScrap();
  void moveScrapToNotebook();
  void saveEditor(bool announce);
  void autosaveEditor();
  void startFocus(uint16_t minutes);
  void finishFocus(bool completed);
  uint32_t focusElapsedMs() const;
  uint32_t focusRemainingSeconds() const;
  void writeFromPrompt();
  void performNoteAction(uint8_t action);
  void insertTextAtCursor(const String &text);
  void deleteAtCursor();
  void moveEditorLeft();
  void moveEditorRight();
  void moveEditorUp();
  void moveEditorDown();
  size_t lineStartForIndex(const String &text, size_t index) const;
  size_t lineEndForIndex(const String &text, size_t index) const;
  uint16_t lineNumberForIndex(const String &text, size_t index) const;
  uint16_t columnForIndex(const String &text, size_t index) const;
  size_t indexForLineAndColumn(const String &text, uint16_t line, uint16_t column) const;
  void ensureCursorVisible();
  void refreshWrap();
  // Maps a tap inside the text area onto a buffer index.
  bool placeCursorAt(int16_t x, int16_t y);
  // Jumps to the next occurrence after the cursor, wrapping once.
  bool findNext(const String &needle);
  void resetPreferredColumn();
  uint32_t countWords(const String &text) const;
  String titleFromBuffer() const;
  void applyKeyEvent(const DeskKeyEvent &key);
  void handleTap(int16_t x, int16_t y);
  int16_t calibrateX(int16_t rawX, int16_t rawY) const;
  int16_t calibrateY(int16_t rawX, int16_t rawY) const;

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  void drawIntroSplash();
  void drawIntroSplashStatic(class Arduino_GFX *g);
  void drawIntroSplashFrame(class Arduino_GFX *g, uint32_t elapsedMs);
  void draw();
  void drawHeader(class Arduino_GFX *g, const String &title, const String &subtitle,
                  bool showBack, bool showSave);
  void drawDock(class Arduino_GFX *g);
  void drawDesk(class Arduino_GFX *g);
  void drawNotebooks(class Arduino_GFX *g);
  void drawArchive(class Arduino_GFX *g);
  void drawFocus(class Arduino_GFX *g);
  void drawRitual(class Arduino_GFX *g);
  void drawSettings(class Arduino_GFX *g);
  void drawEditor(class Arduino_GFX *g);
  void drawTextInput(class Arduino_GFX *g);
  void drawMessage(class Arduino_GFX *g);
  void drawConfirm(class Arduino_GFX *g);
#endif
};

#endif
