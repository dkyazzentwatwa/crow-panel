#include "DeskApp.h"

#include "DeskKeyboardLayout.h"
#include "DeskSystemServices.h"
#include "DeskTouch.h"

#include <CrowPanelShared.h>

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
#include <Arduino_GFX_Library.h>
#include <U8g2lib.h>
#include <math.h>
#endif

namespace {
constexpr int16_t kScreenW = 1024;
constexpr int16_t kScreenH = 600;
constexpr int16_t kHeaderH = 82;
constexpr int16_t kDockY = 526;
constexpr uint32_t kSplashMs = 5000;

bool inside(int16_t x, int16_t y, int16_t rx, int16_t ry, int16_t rw, int16_t rh) {
  return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}
int16_t clampCoordinate(long value, int16_t maxValue) {
  if (value < 0) return 0;
  if (value > maxValue) return maxValue;
  return static_cast<int16_t>(value);
}
int16_t mapAxis(int16_t value, int16_t inMin, int16_t inMax, int16_t outMax) {
  if (inMax == inMin) return 0;
  return clampCoordinate(static_cast<long>(value - inMin) * outMax / (inMax - inMin), outMax);
}
const char *pageName(DeskPage page) {
  switch (page) {
    case kDeskPageDesk: return "desk";
    case kDeskPageNotebooks: return "notebooks";
    case kDeskPageScrap: return "scrap";
    case kDeskPageFocus: return "focus";
    case kDeskPageRitual: return "ritual";
    case kDeskPageArchive: return "archive";
    case kDeskPageSettings: return "settings";
    case kDeskPageEditor: return "editor";
    case kDeskPageMessage: return "message";
    case kDeskPageTextInput: return "input";
    case kDeskPageConfirm: return "confirm";
    default: return "?";
  }
}
}  // namespace

const DeskThemePalette &DeskApp::theme() const { return deskTheme(settings_.theme()); }

void DeskApp::applyAudioPreferences() {
  if (audio_ == nullptr) return;
  audio_->setKeySound(settings_.keySound());
  audio_->setVolume(settings_.volume());
  audio_->setAmbience(settings_.ambience());
}
String DeskApp::audioStatus() const {
  return audio_ != nullptr ? audio_->status() : String("audio unavailable");
}
const char *DeskApp::audioAmbienceName() const {
  return audio_ != nullptr ? audio_->ambienceName()
                           : DeskAudioService::ambienceName(settings_.ambience());
}
const char *DeskApp::audioKeySoundName() const {
  return audio_ != nullptr ? audio_->keySoundName() : DeskKeyClick::soundName(settings_.keySound());
}

void DeskApp::begin(bool initializeDisplay, DeskWifiService *wifi,
                    DeskStorageService *storageService, DeskAudioService *audio,
                    DeskTouch *touch) {
  audio_ = audio;
  touch_ = touch;
  settings_.begin();
  storage_.begin(storageService);
  clock_.begin(&settings_, wifi);
  prompts_.begin();
  String replacement = storage_.readTextFile(String(CYPHER_DESK_ROOT_DIR) + "/prompts.txt", 48000);
  if (replacement.length()) prompts_.loadReplacement(replacement);
  // The service is already up - CypherDeskOs::begin() started it before the
  // Writer ever opens. Just apply the stored preferences.
  applyAudioPreferences();
  keyboard_.reset(); keyboardDirty_ = true;
  navigator_.reset();
  refreshNotes();
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  if (initializeDisplay && CrowDisplay::begin(activeHardwareProfile(), "CYPHER DESK")) drawIntroSplash();
#endif
  dirty_ = true;
}

bool DeskApp::consumeOsHomeRequest() {
  bool requested = osHomeRequested_;
  osHomeRequested_ = false;
  return requested;
}

void DeskApp::reloadPreferences() {
  settings_.begin();
  applyAudioPreferences();
  dirty_ = true;
}

bool DeskApp::keyboardVisible() const {
  const DeskPage page = navigator_.active();
  return page == kDeskPageEditor || page == kDeskPageTextInput;
}

void DeskApp::tick() {
  clock_.tick();
  autosaveEditor();
  static uint32_t lastSecond = 0;
  if (focusActive_ && !focusPaused_) {
    if (focusRemainingSeconds() == 0) finishFocus(true);
    if (millis() - lastSecond >= 1000) { lastSecond = millis(); dirty_ = true; }
  }
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  // CrowDisplay::tick() and the touch poll both belong to CypherDeskOs now.
  // This used to run its own pair over the same panel, on press edges while
  // the rest of the OS used release edges.
  if (CrowDisplay::canvas() == nullptr || touch_ == nullptr) return;

  // Keys fire on touch-DOWN and draw their own press art.
  if (keyboardVisible()) {
    keyboard_.service(*touch_, CrowDisplay::canvas(), theme(), audio_);
    DeskKeyEvent key;
    while (keyboard_.nextEvent(key)) applyKeyEvent(key);
    if (keyboard_.consumeRedraw()) { keyboardDirty_ = true; dirty_ = true; }
  }

  if (dirty_) { draw(); dirty_ = false; }

  // Chrome commits on release, and only for fingers no key claimed.
  for (uint8_t i = 0; i < DeskTouch::kMaxContacts; ++i) {
    const DeskTouch::Contact &contact = touch_->contact(i);
    if (!contact.releasedEdge || contact.owner >= 0) continue;
    lastRawX_ = touch_->lastRawX();
    lastRawY_ = touch_->lastRawY();
    lastTouchX_ = contact.x;
    lastTouchY_ = contact.y;
    ++touchCount_;
    handleTap(lastTouchX_, lastTouchY_);
  }
#endif
}

void DeskApp::route(DeskPage page) {
  if (page == kDeskPageScrap) { openScrap(); return; }
  if (page == kDeskPageNotebooks) { activeFolderPath_ = ""; activeFolderTitle_ = ""; }
  navigator_.dock(page);
  selected_ = 0;
  pageOffset_ = 0;
  refreshNotes();
  dirty_ = true;
}

String DeskApp::currentDirectory() const {
  return activeFolderPath_.length() ? activeFolderPath_ : String(CYPHER_DESK_NOTES_DIR);
}

void DeskApp::refreshNotes() {
  folderCount_ = storage_.listFolders(folders_, CYPHER_DESK_MAX_FOLDERS);
  if (navigator_.active() == kDeskPageArchive) {
    noteCount_ = archiveQuery_.length() ? searchArchive(pageOffset_, notes_, 6, &hasMore_)
                                        : storage_.queryArchive(archiveFilter_, pageOffset_, notes_, 6, &hasMore_);
  } else if (navigator_.active() == kDeskPageDesk) {
    noteCount_ = storage_.queryArchive(kDeskArchiveRecent, 0, notes_, 3, &hasMore_);
  } else {
    uint8_t visibleFolders = !activeFolderPath_.length() && pageOffset_ == 0
                                 ? min(static_cast<uint8_t>(6), folderCount_) : 0;
    uint8_t noteLimit = 6 - visibleFolders;
    noteCount_ = noteLimit ? storage_.listNotesPage(currentDirectory(), pageOffset_, notes_, noteLimit, &hasMore_) : 0;
    if (!noteLimit) hasMore_ = true;
  }
  uint8_t visibleFolders = !activeFolderPath_.length() && pageOffset_ == 0
                               ? min(static_cast<uint8_t>(6), folderCount_) : 0;
  uint16_t items = visibleFolders + noteCount_;
  if (selected_ >= items && items) selected_ = items - 1;
  dirty_ = true;
}
uint16_t DeskApp::searchArchive(uint16_t offset, DeskDocument *out, uint16_t maxCount, bool *hasMore) const {
  if (hasMore != nullptr) *hasMore = false;
  if (out == nullptr || !maxCount || !archiveQuery_.length()) return 0;
  String needle = archiveQuery_; needle.toLowerCase();
  if (archiveTagOnly_ && !needle.startsWith("#")) needle = "#" + needle;
  String directories[3 + CYPHER_DESK_MAX_FOLDERS] = {CYPHER_DESK_NOTES_DIR,
      String(CYPHER_DESK_NOTES_DIR) + "/daily", String(CYPHER_DESK_NOTES_DIR) + "/scraps"};
  uint8_t directoryCount = 3;
  for (uint8_t i = 0; i < folderCount_ && directoryCount < 3 + CYPHER_DESK_MAX_FOLDERS; ++i)
    directories[directoryCount++] = folders_[i].path;
  uint16_t skipped = 0;
  uint16_t found = 0;
  for (uint8_t d = 0; d < directoryCount; ++d) {
    DeskDocument candidates[CYPHER_DESK_PAGE_SIZE];
    uint16_t candidateCount = storage_.listNotes(directories[d], candidates, CYPHER_DESK_PAGE_SIZE);
    for (uint16_t i = 0; i < candidateCount; ++i) {
      String haystack = candidates[i].title + "\n" + storage_.readTextFile(candidates[i].path);
      haystack.toLowerCase();
      if (haystack.indexOf(needle) < 0) continue;
      if (skipped < offset) { ++skipped; continue; }
      if (found < maxCount) out[found++] = candidates[i];
      else { if (hasMore != nullptr) *hasMore = true; return found; }
    }
  }
  return found;
}

void DeskApp::setMessage(const String &title, const String &body, DeskPage returnPage) {
  messageTitle_ = title;
  messageBody_ = body;
  messageReturn_ = returnPage;
  navigator_.go(kDeskPageMessage, true);
  dirty_ = true;
}

void DeskApp::openFolder(const DeskFolder &folder) {
  activeFolderPath_ = folder.path;
  activeFolderTitle_ = folder.title;
  selected_ = 0;
  pageOffset_ = 0;
  navigator_.dock(kDeskPageNotebooks);
  refreshNotes();
}

void DeskApp::beginTextInput(DeskInputPurpose purpose, const String &initial) {
  inputPurpose_ = purpose;
  inputBuffer_ = initial;
  keyboard_.reset(); keyboardDirty_ = true;
  navigator_.go(kDeskPageTextInput, true);
  dirty_ = true;
}

void DeskApp::completeTextInput() {
  inputBuffer_.trim();
  if (!inputBuffer_.length() && inputPurpose_ != kDeskInputWifiPassword && inputPurpose_ != kDeskInputSearch) {
    setMessage("A NAME HELPS", "Type something first.", kDeskPageNotebooks);
    return;
  }
  DeskInputPurpose purpose = inputPurpose_;
  inputPurpose_ = kDeskInputNone;
  if (purpose == kDeskInputNotebook) {
    String path = storage_.createNamedFolder(inputBuffer_);
    navigator_.dock(kDeskPageNotebooks);
    refreshNotes();
    setMessage(path.length() ? "NOTEBOOK READY" : "NAME ALREADY USED",
               path.length() ? storage_.titleFromFolderName(storage_.basename(path)) : inputBuffer_,
               kDeskPageNotebooks);
  } else if (purpose == kDeskInputNoteName) {
    navigator_.back();
    createNamedNote(inputBuffer_, pendingExtension_.c_str());
  } else if (purpose == kDeskInputRename) {
    String newPath;
    bool ok = storage_.renameNote(pendingPath_, inputBuffer_, newPath);
    navigator_.back();
    refreshNotes();
    setMessage(ok ? "RENAMED" : "RENAME FAILED", ok ? newPath : "That name may already exist.",
               navigator_.active());
  } else if (purpose == kDeskInputSearch) {
    archiveQuery_ = inputBuffer_;
    navigator_.back();
    pageOffset_ = 0;
    refreshNotes();
  } else if (purpose == kDeskInputWifiSsid) {
    settings_.setWifiSsid(inputBuffer_);
    navigator_.dock(kDeskPageSettings);
    setMessage("WI-FI SAVED", "Used only for optional time sync.", kDeskPageSettings);
  } else if (purpose == kDeskInputWifiPassword) {
    settings_.setWifiPassword(inputBuffer_);
    navigator_.dock(kDeskPageSettings);
    setMessage("PASSWORD SAVED", "Stored locally and never printed.", kDeskPageSettings);
  }
}

void DeskApp::askConfirm(ConfirmAction action, const String &title, const String &body) {
  confirmAction_ = action;
  confirmTitle_ = title;
  confirmBody_ = body;
  navigator_.go(kDeskPageConfirm, true);
  dirty_ = true;
}

void DeskApp::completeConfirm(bool accepted) {
  ConfirmAction action = confirmAction_;
  confirmAction_ = kConfirmNone;
  if (!accepted) { navigator_.back(); dirty_ = true; return; }
  if (action == kConfirmDelete) {
    bool ok = storage_.removeNote(pendingPath_);
    navigator_.back();
    refreshNotes();
    setMessage(ok ? "DELETED" : "DELETE FAILED", storage_.basename(pendingPath_), navigator_.active());
  } else if (action == kConfirmDiscard) {
    storage_.removeNote(editor_.document.path);
    editingScrap_ = false;
    navigator_.dock(kDeskPageDesk);
    refreshNotes();
  } else if (action == kConfirmSafeEject) {
    if (audio_) audio_->setAmbience(0);
    bool ok = storage_.safeEject();
    navigator_.dock(kDeskPageSettings);
    setMessage(ok ? "SD IS SAFE" : "NOTHING TO EJECT", storage_.status(), kDeskPageSettings);
  }
}

void DeskApp::createNamedNote(const String &name, const char *extension) {
  DeskDocument document;
  document.extension = String(extension) == ".txt" ? ".txt" : ".md";
  document.path = storage_.namedNotePath(currentDirectory(), name, document.extension.c_str());
  document.title = name;
  if (!storage_.saveTextFile(document.path, "")) {
    setMessage("CREATE FAILED", storage_.sourceLabel(), navigator_.active());
    return;
  }
  openNote(document, kDeskPageNotebooks);
}

void DeskApp::createNote(const char *extension) {
  pendingExtension_ = String(extension) == ".txt" ? ".txt" : ".md";
  beginTextInput(kDeskInputNoteName, "");
}

void DeskApp::openNote(const DeskDocument &document, DeskPage returnPage) {
  editorReturn_ = returnPage;
  editor_.document = document;
  editor_.buffer = storage_.readTextFile(document.path);
  editor_.cursor = editor_.buffer.length();
  editor_.topLine = 0;
  editor_.leftColumn = 0;
  editor_.preferredColumn = columnForIndex(editor_.buffer, editor_.cursor);
  editor_.dirty = false;
  editor_.lastEditMs = millis();
  editor_.lastSaveMs = millis();
  DeskNoteMetadata meta = storage_.metadataFor(document.path);
  meta.lastOpened = millis() / 1000;
  storage_.setMetadata(meta);
  settings_.setLastDocument(document.path);
  keyboard_.reset(); keyboardDirty_ = true;
  navigator_.go(kDeskPageEditor);
  ensureCursorVisible();
  dirty_ = true;
}

void DeskApp::openDaily() {
  String path = storage_.dailyPath(clock_.isoDate());
  if (!storage_.exists(path)) {
    String pretty = clock_.prettyDate();
    pretty.replace(" 0", " ");
    String body = "# " + pretty + "\n\n## Morning page\n\n";
    if (!storage_.saveTextFile(path, body)) {
      setMessage("DAILY PAGE FAILED", storage_.status(), kDeskPageDesk);
      return;
    }
  }
  openNote({path, clock_.prettyDate(), ".md"}, kDeskPageDesk);
}

void DeskApp::openScrap() {
  String path = storage_.createScrapPath();
  if (!storage_.saveTextFile(path, "")) {
    setMessage("SCRAP JAR FULL", storage_.status(), kDeskPageDesk);
    return;
  }
  editingScrap_ = true;
  openNote({path, "New scrap", ".md"}, kDeskPageDesk);
}

void DeskApp::moveScrapToNotebook() {
  if (!editingScrap_) return;
  if (!folderCount_) {
    setMessage("NO NOTEBOOK YET", "Create a notebook first.", kDeskPageEditor);
    return;
  }
  saveEditor(false);
  String newPath;
  if (storage_.moveNote(editor_.document.path, folders_[0].path, newPath)) {
    editor_.document.path = newPath;
    editingScrap_ = false;
    setMessage("MOVED TO " + folders_[0].title, storage_.basename(newPath), kDeskPageEditor);
  } else {
    setMessage("MOVE FAILED", "A file with that name may already exist.", kDeskPageEditor);
  }
}

void DeskApp::saveEditor(bool announce) {
  if (!editor_.document.path.length()) return;
  if (storage_.saveTextFile(editor_.document.path, editor_.buffer)) {
    editor_.dirty = false;
    editor_.lastSaveMs = millis();
    if (announce) setMessage("SAVED", storage_.basename(editor_.document.path), kDeskPageEditor);
  } else if (announce) setMessage("SAVE FAILED", storage_.status(), kDeskPageEditor);
  dirty_ = true;
}

void DeskApp::autosaveEditor() {
  if (navigator_.active() != kDeskPageEditor || !editor_.dirty) return;
  if (millis() - editor_.lastEditMs < CYPHER_DESK_AUTOSAVE_MS) return;
  saveEditor(false);
}

void DeskApp::startFocus(uint16_t minutes) {
  focusMinutes_ = minutes;
  settings_.setFocusMinutes(minutes);
  String path = settings_.lastDocument();
  if (!path.length() || !storage_.exists(path)) {
    path = storage_.namedNotePath(CYPHER_DESK_NOTES_DIR, "focus-page", ".md");
    storage_.saveTextFile(path, "# Focus page\n\n");
  }
  openNote({path, storage_.titleFromPath(path), storage_.extensionFromPath(path)}, kDeskPageFocus);
  focusActive_ = true;
  focusPaused_ = false;
  focusStartedMs_ = millis();
  focusPausedTotalMs_ = 0;
  focusStartWords_ = countWords(editor_.buffer);
  dirty_ = true;
}

uint32_t DeskApp::focusElapsedMs() const {
  if (!focusActive_) return 0;
  uint32_t now = focusPaused_ ? focusPausedAtMs_ : millis();
  return now - focusStartedMs_ - focusPausedTotalMs_;
}
uint32_t DeskApp::focusRemainingSeconds() const {
  uint32_t total = static_cast<uint32_t>(focusMinutes_) * 60;
  uint32_t elapsed = focusElapsedMs() / 1000;
  return elapsed >= total ? 0 : total - elapsed;
}

void DeskApp::finishFocus(bool completed) {
  if (!focusActive_) return;
  saveEditor(false);
  DeskSession session;
  session.date = clock_.isoDate();
  uint32_t elapsedMinutes = (focusElapsedMs() + 59999) / 60000;
  session.durationMinutes = completed ? focusMinutes_ : elapsedMinutes;
  session.startingWords = focusStartWords_;
  session.endingWords = countWords(editor_.buffer);
  session.wordDelta = static_cast<int32_t>(session.endingWords) - session.startingWords;
  session.documentPath = editor_.document.path;
  session.completed = completed;
  storage_.logSession(session);
  focusActive_ = false;
  focusPaused_ = false;
  navigator_.dock(kDeskPageFocus);
  setMessage(completed ? "SESSION COMPLETE" : "SESSION SAVED",
             String(session.durationMinutes) + " quiet minutes // " + session.wordDelta + " words",
             kDeskPageFocus);
}

void DeskApp::writeFromPrompt() {
  String name = "prompt-" + clock_.isoDate();
  String path = storage_.namedNotePath(CYPHER_DESK_NOTES_DIR, name, ".md");
  String body = "# Writing prompt\n\n> " + prompts_.current() + "\n\n";
  if (!storage_.saveTextFile(path, body)) {
    setMessage("PROMPT NOTE FAILED", storage_.status(), kDeskPageRitual);
    return;
  }
  openNote({path, "Writing prompt", ".md"}, kDeskPageRitual);
  focusActive_ = true;
  focusPaused_ = false;
  focusMinutes_ = settings_.focusMinutes();
  focusStartedMs_ = millis();
  focusPausedTotalMs_ = 0;
  focusStartWords_ = countWords(editor_.buffer);
}

void DeskApp::performNoteAction(uint8_t action) {
  if (!noteCount_) return;
  uint8_t visibleFolders = !activeFolderPath_.length() && pageOffset_ == 0 &&
                                   navigator_.active() == kDeskPageNotebooks
                               ? min(static_cast<uint8_t>(6), folderCount_) : 0;
  uint16_t noteIndex = activeFolderPath_.length() || navigator_.active() == kDeskPageArchive
                           ? selected_ : selected_ >= visibleFolders ? selected_ - visibleFolders : 0xffff;
  if (noteIndex >= noteCount_) return;
  DeskDocument document = notes_[noteIndex];
  if (action == 0) openNote(document, navigator_.active());
  else if (action == 1) { storage_.toggleFavorite(document.path); refreshNotes(); }
  else if (action == 2) { storage_.toggleFinished(document.path); refreshNotes(); }
  else if (action == 3) {
    String exportPath;
    bool ok = storage_.exportCopy(document.path, clock_.isoDate(), exportPath);
    String detail = ok ? exportPath : storage_.sourceLabel();
    if (ok && !storage_.persistent()) detail += " // RAM demo copy is temporary";
    setMessage(ok ? "COPY EXPORTED" : "EXPORT FAILED", detail, navigator_.active());
  } else if (action == 4) {
    String newPath;
    uint8_t target = 0;
    while (target < folderCount_ && folders_[target].path == currentDirectory()) ++target;
    bool ok = target < folderCount_ && storage_.moveNote(document.path, folders_[target].path, newPath);
    refreshNotes();
    setMessage(ok ? "NOTE MOVED" : "MOVE NEEDS A NOTEBOOK",
               ok ? folders_[target].title : "Create another notebook or resolve a name conflict.",
               navigator_.active());
  } else if (action == 5) {
    pendingPath_ = document.path;
    beginTextInput(kDeskInputRename, document.title);
  } else if (action == 6) {
    pendingPath_ = document.path;
    askConfirm(kConfirmDelete, "DELETE THIS NOTE?", document.title + " will be removed.");
  }
}

size_t DeskApp::lineStartForIndex(const String &text, size_t index) const {
  index = min(index, text.length());
  while (index > 0 && text[index - 1] != '\n') --index;
  return index;
}
size_t DeskApp::lineEndForIndex(const String &text, size_t index) const {
  index = min(index, text.length());
  while (index < text.length() && text[index] != '\n') ++index;
  return index;
}
uint16_t DeskApp::lineNumberForIndex(const String &text, size_t index) const {
  uint16_t line = 0;
  for (size_t i = 0; i < index && i < text.length(); ++i) if (text[i] == '\n') ++line;
  return line;
}
uint16_t DeskApp::columnForIndex(const String &text, size_t index) const {
  return index - lineStartForIndex(text, index);
}
size_t DeskApp::indexForLineAndColumn(const String &text, uint16_t line, uint16_t column) const {
  size_t index = 0;
  uint16_t currentLine = 0;
  while (currentLine < line && index < text.length()) if (text[index++] == '\n') ++currentLine;
  return min(index + column, lineEndForIndex(text, index));
}
void DeskApp::ensureCursorVisible() {
  editor_.cursor = min(editor_.cursor, editor_.buffer.length());
  uint16_t line = lineNumberForIndex(editor_.buffer, editor_.cursor);
  uint16_t column = columnForIndex(editor_.buffer, editor_.cursor);
  uint8_t visible = focusActive_ ? 5 : 8;
  if (focusActive_) editor_.topLine = line > 2 ? line - 2 : 0;
  else {
    if (line < editor_.topLine) editor_.topLine = line;
    if (line >= editor_.topLine + visible) editor_.topLine = line - visible + 1;
  }
  if (column < editor_.leftColumn) editor_.leftColumn = column;
  if (column >= editor_.leftColumn + 72) editor_.leftColumn = column - 71;
}
void DeskApp::resetPreferredColumn() { editor_.preferredColumn = columnForIndex(editor_.buffer, editor_.cursor); }
void DeskApp::insertTextAtCursor(const String &text) {
  if (!text.length() || editor_.buffer.length() + text.length() > CYPHER_DESK_MAX_EDITOR_LEN) return;
  editor_.buffer = editor_.buffer.substring(0, editor_.cursor) + text + editor_.buffer.substring(editor_.cursor);
  editor_.cursor += text.length();
  editor_.dirty = true;
  editor_.lastEditMs = millis();
  editor_.document.title = titleFromBuffer();
  resetPreferredColumn();
  ensureCursorVisible();
  dirty_ = true;
}
void DeskApp::deleteAtCursor() {
  if (!editor_.cursor) return;
  editor_.buffer.remove(editor_.cursor - 1, 1);
  --editor_.cursor;
  editor_.dirty = true;
  editor_.lastEditMs = millis();
  editor_.document.title = titleFromBuffer();
  resetPreferredColumn();
  ensureCursorVisible();
  dirty_ = true;
}
void DeskApp::moveEditorLeft() { if (editor_.cursor) --editor_.cursor; resetPreferredColumn(); ensureCursorVisible(); dirty_ = true; }
void DeskApp::moveEditorRight() { if (editor_.cursor < editor_.buffer.length()) ++editor_.cursor; resetPreferredColumn(); ensureCursorVisible(); dirty_ = true; }
void DeskApp::moveEditorUp() {
  uint16_t line = lineNumberForIndex(editor_.buffer, editor_.cursor);
  if (line) editor_.cursor = indexForLineAndColumn(editor_.buffer, line - 1, editor_.preferredColumn);
  ensureCursorVisible(); dirty_ = true;
}
void DeskApp::moveEditorDown() {
  uint16_t line = lineNumberForIndex(editor_.buffer, editor_.cursor);
  editor_.cursor = indexForLineAndColumn(editor_.buffer, line + 1, editor_.preferredColumn);
  ensureCursorVisible(); dirty_ = true;
}
uint32_t DeskApp::countWords(const String &text) const {
  uint32_t words = 0;
  bool inWord = false;
  for (size_t i = 0; i < text.length(); ++i) {
    bool word = isalnum(static_cast<unsigned char>(text[i]));
    if (word && !inWord) ++words;
    inWord = word;
  }
  return words;
}
String DeskApp::titleFromBuffer() const {
  int start = 0;
  while (start < static_cast<int>(editor_.buffer.length())) {
    int end = editor_.buffer.indexOf('\n', start);
    if (end < 0) end = editor_.buffer.length();
    String line = editor_.buffer.substring(start, end);
    line.trim();
    while (line.startsWith("#") || line.startsWith(">")) { line.remove(0, 1); line.trim(); }
    if (line.length()) return line.substring(0, min(static_cast<int>(line.length()), 36));
    start = end + 1;
  }
  return storage_.titleFromPath(editor_.document.path);
}

// Shift consumption, key clicks and layer state all live in the keyboard now;
// this just applies the resulting character or motion.
void DeskApp::applyKeyEvent(const DeskKeyEvent &key) {
  if (key.action == kDeskKeyNone) return;
  if (navigator_.active() == kDeskPageTextInput) {
    if (key.action == kDeskKeyText && inputBuffer_.length() < 64) inputBuffer_ += key.text;
    else if (key.action == kDeskKeyBackspace && inputBuffer_.length())
      inputBuffer_.remove(inputBuffer_.length() - 1);
    else if (key.action == kDeskKeyEnter) completeTextInput();
    dirty_ = true;
    return;
  }
  if (key.action == kDeskKeyText) insertTextAtCursor(key.text);
  else if (key.action == kDeskKeyBackspace) deleteAtCursor();
  else if (key.action == kDeskKeyEnter) insertTextAtCursor("\n");
  else if (key.action == kDeskKeyLeft) moveEditorLeft();
  else if (key.action == kDeskKeyRight) moveEditorRight();
  dirty_ = true;
}

void DeskApp::handleTap(int16_t x, int16_t y) {
  DeskPage page = navigator_.active();
  if (page == kDeskPageMessage) { navigator_.dock(messageReturn_); refreshNotes(); return; }
  if (page == kDeskPageConfirm) {
    if (inside(x, y, 274, 360, 210, 62)) completeConfirm(false);
    else if (inside(x, y, 540, 360, 210, 62)) completeConfirm(true);
    return;
  }
  if (page == kDeskPageTextInput) {
    if (inside(x, y, 16, 14, 124, 56)) { navigator_.back(); dirty_ = true; return; }
    if (inside(x, y, 866, 14, 142, 56)) { completeTextInput(); return; }
    return;  // keys are serviced on press edges in tick()
  }
  if (page == kDeskPageEditor) {
    if (focusActive_) {
      if (inside(x, y, 14, 12, 116, 52)) { finishFocus(false); return; }
      if (inside(x, y, 714, 12, 110, 52)) {
        if (focusPaused_) { focusPausedTotalMs_ += millis() - focusPausedAtMs_; focusPaused_ = false; }
        else { focusPaused_ = true; focusPausedAtMs_ = millis(); }
        dirty_ = true; return;
      }
      if (inside(x, y, 842, 12, 164, 52)) { focusMinutes_ += 5; dirty_ = true; return; }
    } else {
      if (inside(x, y, 16, 14, 124, 56)) {
        if (editingScrap_ && editor_.buffer.length()) saveEditor(false);
        commandBack(); return;
      }
      if (inside(x, y, 866, 14, 142, 56)) { saveEditor(true); return; }
      if (editingScrap_ && inside(x, y, 732, 14, 120, 56)) { moveScrapToNotebook(); return; }
      if (editingScrap_ && inside(x, y, 20, 268, 140, 38)) {
        if (editor_.buffer.length()) askConfirm(kConfirmDiscard, "DISCARD SCRAP?", "This text will be removed.");
        else { storage_.removeNote(editor_.document.path); editingScrap_ = false; route(kDeskPageDesk); }
        return;
      }
    }
    return;  // keys are serviced on press edges in tick()
  }

  if (page == kDeskPageDesk && inside(x, y, 618, 14, 118, 54)) {
    osHomeRequested_ = true;
    return;
  }
  if (inside(x, y, 748, 14, 118, 54)) { route(kDeskPageArchive); return; }
  if (inside(x, y, 878, 14, 130, 54)) { route(kDeskPageSettings); return; }
  if (inside(x, y, 16, 14, 124, 56) && (page == kDeskPageArchive || page == kDeskPageSettings)) {
    route(kDeskPageDesk); return;
  }

  if (y >= kDockY) {
    uint8_t slot = min(static_cast<int>(x / 204), 4);
    const DeskPage pages[] = {kDeskPageDesk, kDeskPageNotebooks, kDeskPageScrap, kDeskPageFocus, kDeskPageRitual};
    route(pages[slot]);
    return;
  }

  if (page == kDeskPageDesk) {
    if (inside(x, y, 28, 106, 620, 92)) {
      String path = settings_.lastDocument();
      if (path.length() && storage_.exists(path)) openNote({path, storage_.titleFromPath(path), storage_.extensionFromPath(path)}, kDeskPageDesk);
      else createNote(".md");
    } else if (inside(x, y, 28, 208, 620, 92)) openDaily();
    else if (inside(x, y, 28, 316, 196, 118) && noteCount_ > 0) openNote(notes_[0], kDeskPageDesk);
    else if (inside(x, y, 238, 316, 196, 118) && noteCount_ > 1) openNote(notes_[1], kDeskPageDesk);
    else if (inside(x, y, 448, 316, 196, 118) && noteCount_ > 2) openNote(notes_[2], kDeskPageDesk);
    else if (inside(x, y, 694, 406, 142, 56)) createNote(".txt");
    else if (inside(x, y, 850, 406, 142, 56)) createNote(".md");
    return;
  }

  if (page == kDeskPageNotebooks) {
    if (inside(x, y, 28, 94, 186, 48)) { beginTextInput(kDeskInputNotebook); return; }
    if (inside(x, y, 226, 94, 154, 48)) { createNote(".md"); return; }
    if (activeFolderPath_.length() && inside(x, y, 392, 94, 128, 48)) {
      activeFolderPath_ = ""; activeFolderTitle_ = ""; selected_ = 0; refreshNotes(); return;
    }
    if (activeFolderPath_.length() && inside(x, y, 532, 94, 128, 48)) {
      DeskNoteMetadata meta = storage_.metadataFor(activeFolderPath_);
      storage_.setNotebookColor(activeFolderPath_, meta.notebookColor + 1);
      dirty_ = true; return;
    }
    if (inside(x, y, 690, 452, 138, 48) && pageOffset_ > 0) {
      pageOffset_ = pageOffset_ > 6 ? pageOffset_ - 6 : 0; selected_ = 0; refreshNotes(); return;
    }
    if (inside(x, y, 842, 452, 138, 48) && hasMore_) {
      pageOffset_ += noteCount_ ? noteCount_ : 1; selected_ = 0; refreshNotes(); return;
    }
    uint8_t visibleFolders = !activeFolderPath_.length() && pageOffset_ == 0
                                 ? min(static_cast<uint8_t>(6), folderCount_) : 0;
    for (uint8_t row = 0; row < 6; ++row) {
      if (!inside(x, y, 24, 152 + row * 56, 642, 50)) continue;
      uint16_t index = row;
      uint16_t total = visibleFolders + noteCount_;
      if (index >= total) return;
      selected_ = index;
      if (selected_ < visibleFolders) openFolder(folders_[selected_]);
      else dirty_ = true;
      return;
    }
    const int16_t actionX[] = {690, 842};
    for (uint8_t action = 0; action < 7; ++action) {
      int16_t ax = actionX[action % 2];
      int16_t ay = 158 + (action / 2) * 66;
      if (inside(x, y, ax, ay, 138, 54)) { performNoteAction(action); return; }
    }
  }

  if (page == kDeskPageArchive) {
    if (inside(x, y, 690, 94, 138, 44)) { beginTextInput(kDeskInputSearch, archiveQuery_); return; }
    if (inside(x, y, 842, 94, 138, 44)) { archiveTagOnly_ = !archiveTagOnly_; pageOffset_ = 0; refreshNotes(); return; }
    for (uint8_t i = 0; i < 5; ++i) if (inside(x, y, 24 + i * 132, 94, 120, 44)) {
      archiveFilter_ = static_cast<DeskArchiveFilter>(i); archiveQuery_ = ""; selected_ = 0; refreshNotes(); return;
    }
    for (uint8_t row = 0; row < 6; ++row) if (inside(x, y, 24, 152 + row * 56, 642, 50) && row < noteCount_) {
      selected_ = row; dirty_ = true; return;
    }
    if (inside(x, y, 690, 452, 138, 48) && pageOffset_ > 0) {
      pageOffset_ = pageOffset_ > 6 ? pageOffset_ - 6 : 0; selected_ = 0; refreshNotes(); return;
    }
    if (inside(x, y, 842, 452, 138, 48) && hasMore_) {
      pageOffset_ += noteCount_; selected_ = 0; refreshNotes(); return;
    }
    const int16_t actionX[] = {690, 842};
    for (uint8_t action = 0; action < 7; ++action) {
      if (inside(x, y, actionX[action % 2], 158 + (action / 2) * 66, 138, 54)) {
        performNoteAction(action); return;
      }
    }
  }

  if (page == kDeskPageFocus) {
    const uint16_t options[] = {10, 20, 30, 45};
    for (uint8_t i = 0; i < 4; ++i) if (inside(x, y, 72 + i * 222, 140, 184, 88)) {
      focusMinutes_ = options[i]; settings_.setFocusMinutes(options[i]); dirty_ = true; return;
    }
    if (inside(x, y, 350, 276, 324, 72)) { startFocus(focusMinutes_); return; }
  }

  if (page == kDeskPageRitual) {
    for (uint8_t i = 0; i < 5; ++i) if (inside(x, y, 24 + i * 196, 96, 184, 48)) {
      prompts_.setCategory(static_cast<DeskPromptCategory>(i)); dirty_ = true; return;
    }
    if (inside(x, y, 40, 404, 150, 56)) { prompts_.shuffle(); dirty_ = true; return; }
    if (inside(x, y, 206, 404, 150, 56)) {
      const uint16_t options[] = {10, 20, 30, 45};
      uint8_t next = 0;
      for (uint8_t i = 0; i < 4; ++i) if (settings_.focusMinutes() == options[i]) next = (i + 1) % 4;
      settings_.setFocusMinutes(options[next]); focusMinutes_ = options[next]; dirty_ = true; return;
    }
    if (inside(x, y, 372, 404, 150, 56)) { settings_.setTheme(nextDeskTheme(settings_.theme())); dirty_ = true; return; }
    if (inside(x, y, 538, 404, 150, 56)) {
      settings_.setAmbience((settings_.ambience() + 1) % 5); if (audio_) audio_->setAmbience(settings_.ambience()); dirty_ = true; return;
    }
    if (inside(x, y, 716, 398, 260, 68)) { writeFromPrompt(); return; }
  }

  if (page == kDeskPageSettings) {
    if (inside(x, y, 56, 126, 410, 64)) { settings_.setTheme(nextDeskTheme(settings_.theme())); dirty_ = true; return; }
    if (inside(x, y, 56, 208, 410, 64)) { beginTextInput(kDeskInputWifiSsid, settings_.wifiSsid()); return; }
    if (inside(x, y, 56, 290, 410, 64)) { beginTextInput(kDeskInputWifiPassword, ""); return; }
    if (inside(x, y, 56, 372, 196, 64)) { clock_.previousDay(); dirty_ = true; return; }
    if (inside(x, y, 270, 372, 196, 64)) { clock_.nextDay(); dirty_ = true; return; }
    if (inside(x, y, 540, 126, 410, 64)) { settings_.setKeySound((settings_.keySound() + 1) % 4); if (audio_) audio_->setKeySound(settings_.keySound()); dirty_ = true; return; }
    if (inside(x, y, 540, 208, 410, 64)) { settings_.setAmbience((settings_.ambience() + 1) % 5); if (audio_) audio_->setAmbience(settings_.ambience()); dirty_ = true; return; }
    if (inside(x, y, 540, 290, 196, 64)) { clock_.confirmDate(); dirty_ = true; return; }
    if (inside(x, y, 754, 290, 196, 64)) { clock_.requestSync(); dirty_ = true; return; }
    if (inside(x, y, 540, 372, 410, 64)) { askConfirm(kConfirmSafeEject, "SAFE EJECT SD?", "Writes will stop until reboot."); return; }
  }
}

int16_t DeskApp::calibrateX(int16_t rawX, int16_t rawY) const {
  int16_t source = CYPHER_DESK_TOUCH_SWAP_XY ? rawY : rawX;
  int16_t value = mapAxis(source, CYPHER_DESK_TOUCH_MIN_X, CYPHER_DESK_TOUCH_MAX_X, kScreenW - 1);
  return CYPHER_DESK_TOUCH_INVERT_X ? kScreenW - 1 - value : value;
}
int16_t DeskApp::calibrateY(int16_t rawX, int16_t rawY) const {
  int16_t source = CYPHER_DESK_TOUCH_SWAP_XY ? rawX : rawY;
  int16_t value = mapAxis(source, CYPHER_DESK_TOUCH_MIN_Y, CYPHER_DESK_TOUCH_MAX_Y, kScreenH - 1);
  return CYPHER_DESK_TOUCH_INVERT_Y ? kScreenH - 1 - value : value;
}

void DeskApp::printStatus(Print &out) const {
  out.print(F("[desk] page=")); out.print(pageName(navigator_.active()));
  out.print(F(" source=")); out.print(storage_.sourceLabel());
  out.print(F(" notes=")); out.print(noteCount_);
  out.print(F(" theme=")); out.print(theme().name);
  out.print(F(" date=")); out.print(clock_.isoDate());
  out.print(F(" timeSource=")); out.print(clock_.sourceLabel());
  out.print(F(" dirty=")); out.println(editor_.dirty ? F("yes") : F("no"));
  out.print(F("[desk] storage=")); out.println(storage_.status());
  out.print(F("[desk] audio=")); out.println(audioStatus());
  out.println(F("[proof] touch=field-proven previous build; writer expansion=compile target; SD/time/audio require bench proof"));
}
void DeskApp::printFiles(Print &out) const {
  out.print(F("[desk] directory=")); out.println(currentDirectory());
  for (uint8_t i = 0; i < folderCount_; ++i) { out.print(F("  folder ")); out.print(i + 1); out.print(F(": ")); out.println(folders_[i].path); }
  for (uint16_t i = 0; i < noteCount_; ++i) { out.print(F("  note ")); out.print(i + 1); out.print(F(": ")); out.print(notes_[i].title); out.print(F("  ")); out.println(notes_[i].path); }
}
void DeskApp::commandNew(const String &args) { String v = args; v.toLowerCase(); createNote(v.indexOf("txt") >= 0 ? ".txt" : ".md"); }
void DeskApp::commandOpen(const String &args) { int index = args.toInt(); if (index > 0 && index <= noteCount_) openNote(notes_[index - 1], navigator_.active()); else Serial.println(F("[desk] usage: open <visible note number>")); }
void DeskApp::commandType(const String &args) { if (navigator_.active() != kDeskPageEditor) { Serial.println(F("[desk] open a note first")); return; } String text = args; text.replace("\\n", "\n"); insertTextAtCursor(text); }
void DeskApp::commandSave() { saveEditor(true); }
void DeskApp::commandBack() {
  DeskPage page = navigator_.active();
  if (page == kDeskPageEditor) {
    if (focusActive_) { finishFocus(false); return; }
    saveEditor(false);
    editingScrap_ = false;
    navigator_.dock(editorReturn_);
    refreshNotes();
  } else if (page == kDeskPageMessage) { navigator_.dock(messageReturn_); refreshNotes(); }
  else if (page == kDeskPageNotebooks && activeFolderPath_.length()) { activeFolderPath_ = ""; activeFolderTitle_ = ""; refreshNotes(); }
  else route(kDeskPageDesk);
  dirty_ = true;
}
void DeskApp::commandDemo() { route(kDeskPageDesk); if (noteCount_) openNote(notes_[0], kDeskPageDesk); else { String path = storage_.namedNotePath(CYPHER_DESK_NOTES_DIR, "demo-page", ".md"); storage_.saveTextFile(path, "# Demo page\n\n"); openNote({path, "Demo page", ".md"}, kDeskPageDesk); } insertTextAtCursor("\n## Captured on CrowPanel\n- A quiet place to keep the words moving\n"); }
void DeskApp::commandPage(const String &args) { String v = args; v.toLowerCase(); if (v == "desk") route(kDeskPageDesk); else if (v.startsWith("note")) route(kDeskPageNotebooks); else if (v == "scrap") openScrap(); else if (v == "focus") route(kDeskPageFocus); else if (v == "ritual") route(kDeskPageRitual); else if (v == "archive") route(kDeskPageArchive); else if (v == "settings") route(kDeskPageSettings); else Serial.println(F("[desk] page desk|notebooks|scrap|focus|ritual|archive|settings")); }
void DeskApp::commandDaily() { openDaily(); }
void DeskApp::commandScrap() { openScrap(); }
void DeskApp::commandFocus(const String &args) { int minutes = args.toInt(); if (minutes == 10 || minutes == 20 || minutes == 30 || minutes == 45) startFocus(minutes); else route(kDeskPageFocus); }
void DeskApp::commandRitual(const String &args) { String v = args; v.toLowerCase(); if (v == "shuffle") prompts_.shuffle(); else if (v == "write") writeFromPrompt(); else route(kDeskPageRitual); dirty_ = true; }
void DeskApp::commandTheme(const String &args) {
  String value = args;
  value.trim();
  if (value.equalsIgnoreCase("list")) {
    for (uint8_t i = 0; i < kDeskThemeCount; ++i) {
      Serial.print(F("[theme] "));
      Serial.print(i == settings_.theme() ? "* " : "  ");
      Serial.println(deskThemeName(static_cast<DeskThemeId>(i)));
    }
    return;
  }
  settings_.setTheme(value.length() ? deskThemeFromName(value) : nextDeskTheme(settings_.theme()));
  Serial.print(F("[theme] "));
  Serial.println(deskThemeName(settings_.theme()));
  dirty_ = true;
  keyboardDirty_ = true;
}
void DeskApp::commandSound(const String &args) { String v = args; v.toLowerCase(); if (v.startsWith("ambience ")) { uint8_t a = v.substring(9).toInt(); settings_.setAmbience(a); if (audio_) audio_->setAmbience(a); } else if (v.startsWith("key ")) { uint8_t k = v.substring(4).toInt(); settings_.setKeySound(k); if (audio_) audio_->setKeySound(k); } else if (v.startsWith("volume ")) { uint8_t volume = v.substring(7).toInt(); settings_.setVolume(volume); if (audio_) audio_->setVolume(volume); } Serial.println(audioStatus()); dirty_ = true; }
void DeskApp::commandStats(Print &out) const { DeskStats s = storage_.stats(clock_.isoDate()); out.print(F("[stats] today=")); out.print(s.quietMinutesToday); out.print(F("min week=")); out.print(s.quietMinutesWeek); out.print(F("min words=")); out.print(s.wordsAddedWeek); out.print(F(" completed=")); out.print(s.sessionsCompleted); out.print(F(" last=")); out.println(s.lastSession.length() ? s.lastSession : "none yet"); }
void DeskApp::commandSearch(const String &argsValue, Print &out) const {
  String query = argsValue;
  query.trim();
  bool tagOnly = query.startsWith("tag ");
  if (tagOnly) query = query.substring(4);
  query.toLowerCase();
  if (!query.length()) { out.println(F("[search] use: search <text> or search tag <tag>")); return; }
  const String directories[] = {CYPHER_DESK_NOTES_DIR, String(CYPHER_DESK_NOTES_DIR) + "/daily",
                                String(CYPHER_DESK_NOTES_DIR) + "/scraps"};
  uint16_t matches = 0;
  for (const String &directory : directories) {
    DeskDocument documents[CYPHER_DESK_PAGE_SIZE];
    uint16_t count = storage_.listNotes(directory, documents, CYPHER_DESK_PAGE_SIZE);
    for (uint16_t i = 0; i < count; ++i) {
      String haystack = documents[i].title + "\n" + storage_.readTextFile(documents[i].path);
      haystack.toLowerCase();
      String needle = tagOnly ? (query.startsWith("#") ? query : "#" + query) : query;
      if (haystack.indexOf(needle) < 0) continue;
      out.print(F("[search] ")); out.print(documents[i].path); out.print(F(" // ")); out.println(documents[i].title);
      ++matches;
    }
  }
  DeskFolder folders[CYPHER_DESK_MAX_FOLDERS];
  uint8_t folderCount = storage_.listFolders(folders, CYPHER_DESK_MAX_FOLDERS);
  for (uint8_t f = 0; f < folderCount; ++f) {
    DeskDocument documents[CYPHER_DESK_PAGE_SIZE];
    uint16_t count = storage_.listNotes(folders[f].path, documents, CYPHER_DESK_PAGE_SIZE);
    for (uint16_t i = 0; i < count; ++i) {
      String haystack = documents[i].title + "\n" + storage_.readTextFile(documents[i].path);
      haystack.toLowerCase();
      String needle = tagOnly ? (query.startsWith("#") ? query : "#" + query) : query;
      if (haystack.indexOf(needle) < 0) continue;
      out.print(F("[search] ")); out.print(documents[i].path); out.print(F(" // ")); out.println(documents[i].title);
      ++matches;
    }
  }
  out.print(F("[search] matches=")); out.println(matches);
}
void DeskApp::commandTime(const String &args, Print &out) { String v = args; v.toLowerCase(); if (v == "sync") clock_.requestSync(); else if (v == "prev") clock_.previousDay(); else if (v == "next") clock_.nextDay(); else if (v == "confirm") clock_.confirmDate(); out.print(F("[time] date=")); out.print(clock_.isoDate()); out.print(F(" source=")); out.print(clock_.sourceLabel()); out.print(F(" status=")); out.println(clock_.status()); dirty_ = true; }
void DeskApp::commandStorage(const String &args, Print &out) { String v = args; v.toLowerCase(); if (v == "eject") askConfirm(kConfirmSafeEject, "SAFE EJECT SD?", "Confirm on screen."); else if (v == "rebuild") { bool ok = storage_.rebuildMetadata(); out.println(ok ? F("[storage] metadata rebuilt") : F("[storage] rebuild unavailable")); } out.print(F("[storage] ")); out.println(storage_.status()); }
void DeskApp::printTouchDiagnostics(Print &out) const { out.print(F("[touch] count=")); out.print(touchCount_); out.print(F(" raw=")); out.print(lastRawX_); out.print(','); out.print(lastRawY_); out.print(F(" mapped=")); out.print(lastTouchX_); out.print(','); out.println(lastTouchY_); }

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
namespace {
void smallText(Arduino_GFX *g, int16_t x, int16_t topY, const String &text, uint16_t color,
               Widgets::Align align = Widgets::kLeft) {
  g->setFont(u8g2_font_cubic11_h_cjk);
  g->setUTF8Print(true);
  g->setTextSize(1);
  g->setTextColor(color);
  int16_t bx, by; uint16_t bw, bh;
  g->getTextBounds(text, 0, 0, &bx, &by, &bw, &bh);
  int16_t drawX = x - bx;
  if (align == Widgets::kCenter) drawX = x - static_cast<int16_t>(bw) / 2 - bx;
  if (align == Widgets::kRight) drawX = x - static_cast<int16_t>(bw) - bx;
  g->setCursor(drawX, topY - by);
  g->print(text);
}
String fitLabel(Arduino_GFX *g, String value, int16_t width, const GFXfont *font) {
  while (value.length() > 3 && Widgets::textWidth(g, value.c_str(), font) > width) value.remove(value.length() - 1);
  return value;
}
void button(Arduino_GFX *g, const DeskThemePalette &t, int16_t x, int16_t y, int16_t w,
            int16_t h, const String &label, bool active = false) {
  Widgets::panel(g, x + 3, y + 4, w, h, 11, t.background);
  Widgets::panel(g, x, y, w, h, 11, active ? t.panelHighlight : t.panel, active ? 3 : 1,
                 active ? t.accent2 : t.line);
  smallText(g, x + w / 2, y + h / 2 - 3, label, active ? t.ink : t.muted, Widgets::kCenter);
}
void card(Arduino_GFX *g, const DeskThemePalette &t, int16_t x, int16_t y, int16_t w,
          int16_t h, uint16_t accent) {
  Widgets::panel(g, x + 4, y + 5, w, h, 14, t.background);
  Widgets::panel(g, x, y, w, h, 14, t.panel, 2, accent);
}
void sparkle(Arduino_GFX *g, int16_t x, int16_t y, uint16_t color, uint16_t ink) {
  g->drawFastHLine(x - 7, y, 15, color);
  g->drawFastVLine(x, y - 7, 15, color);
  g->fillCircle(x, y, 2, ink);
}
}  // namespace

void DeskApp::drawIntroSplash() {
  Arduino_GFX *g = CrowDisplay::canvas();
  if (!g) return;
  drawIntroSplashStatic(g);
  uint32_t start = millis();
  while (millis() - start < kSplashMs) {
    drawIntroSplashFrame(g, millis() - start);
    delay(80);
  }
  drawIntroSplashFrame(g, kSplashMs);
}
void DeskApp::drawIntroSplashStatic(Arduino_GFX *g) {
  const DeskThemePalette &t = theme();
  g->fillScreen(t.background);
  for (int16_t y = 8; y < kScreenH; y += 18) g->drawFastHLine(0, y, kScreenW, t.shell);
  g->drawRoundRect(22, 20, 980, 560, 22, t.line);
  Widgets::pill(g, 48, 42, "LOFI WRITER", Widgets::fontS(), t.background, t.success);
  Widgets::pill(g, 830, 42, "SOFT BOOT", Widgets::fontS(), t.background, t.accent2);
  Widgets::text(g, 512, 54, "CYPHER DESK", Widgets::fontXL(), t.ink, Widgets::kCenter);
  smallText(g, 512, 112, "WAKING UP YOUR TINY WRITING STUDIO", t.accent, Widgets::kCenter);
  card(g, t, 282, 136, 460, 286, t.line);
  g->drawCircle(512, 278, 112, t.line);
  g->drawRoundRect(218, 510, 588, 24, 10, t.line);
  smallText(g, 512, 558, "notebooks  //  scraps  //  focus  //  tiny rituals", t.muted, Widgets::kCenter);
  sparkle(g, 94, 152, t.accent2, t.ink); sparkle(g, 914, 168, t.success, t.ink);
}
void DeskApp::drawIntroSplashFrame(Arduino_GFX *g, uint32_t elapsed) {
  const DeskThemePalette &t = theme();
  float progress = static_cast<float>(min(elapsed, static_cast<uint32_t>(4750))) / 4750.0f;
  const char *messages[] = {"gathering quiet thoughts...", "stacking tiny notebooks...",
                            "sharpening pastel pencils...", "warming the touch keyboard...",
                            "your desk is ready!"};
  uint8_t stage = min(static_cast<uint32_t>(4), elapsed * 5 / 4750);
  g->fillRoundRect(300, 154, 424, 246, 22, t.panel);
  int16_t cx = 512, cy = 272;
  float sweep = progress * 4.0f * M_PI - M_PI / 2.0f;
  for (uint8_t i = 0; i < 6; ++i) g->fillCircle(cx + cosf(sweep - i * .1f) * 104,
      cy + sinf(sweep - i * .1f) * 104, i ? 3 : 7, i ? t.line : t.accent2);
  g->fillTriangle(426, 217, 448, 183, 470, 217, t.accent);
  g->fillTriangle(554, 217, 576, 183, 598, 217, t.accent2);
  g->fillRoundRect(420, 208, 184, 132, 20, t.panelHighlight);
  g->drawRoundRect(420, 208, 184, 132, 20, t.accent);
  g->fillRect(438, 225, 148, 82, t.shell);
  bool blink = elapsed % 1200 > 1040;
  if (blink) { g->drawFastHLine(470, 264, 18, t.ink); g->drawFastHLine(536, 264, 18, t.ink); }
  else { g->fillCircle(479, 262, 7, t.ink); g->fillCircle(545, 262, 7, t.ink); }
  g->fillCircle(459, 282, 8, t.accent2); g->fillCircle(565, 282, 8, t.accent2);
  g->drawLine(500, 282, 512, 290, t.success); g->drawLine(512, 290, 524, 282, t.success);
  smallText(g, 512, 360, messages[stage], t.accent, Widgets::kCenter);
  g->fillRoundRect(221, 513, 582, 18, 7, t.shell);
  g->fillRoundRect(221, 513, static_cast<int16_t>(582 * progress), 18, 7, t.accent2);
  smallText(g, 512, 470, "LOADING " + String(static_cast<uint8_t>(progress * 100)) + "%", t.ink, Widgets::kCenter);
}

void DeskApp::drawHeader(Arduino_GFX *g, const String &title, const String &subtitle,
                         bool showBack, bool showSave) {
  const DeskThemePalette &t = theme();
  g->fillRect(0, 0, kScreenW, kHeaderH, t.shell);
  g->fillRect(0, 76, 256, 6, t.accent); g->fillRect(256, 76, 256, 6, t.accent2);
  g->fillRect(512, 76, 256, 6, t.success); g->fillRect(768, 76, 256, 6, t.accent3);
  int16_t tx = 26;
  if (showBack) { button(g, t, 16, 14, 124, 54, "BACK"); tx = 160; }
  Widgets::text(g, tx, 12, fitLabel(g, title, showSave ? 520 : 560, Widgets::fontL()).c_str(), Widgets::fontL(), t.ink, Widgets::kLeft);
  smallText(g, tx, 51, subtitle, t.muted);
  if (showSave) button(g, t, 866, 14, 142, 54, "SAVE", true);
  else if (!showBack) {
    button(g, t, 618, 14, 118, 54, "OS HOME", true);
    button(g, t, 748, 14, 118, 54, "ARCHIVE");
    button(g, t, 878, 14, 130, 54, "SETTINGS");
  }
}

void DeskApp::drawDock(Arduino_GFX *g) {
  const DeskThemePalette &t = theme();
  const char *labels[] = {"DESK", "NOTEBOOKS", "+ SCRAP", "FOCUS", "RITUAL"};
  const DeskPage pages[] = {kDeskPageDesk, kDeskPageNotebooks, kDeskPageScrap, kDeskPageFocus, kDeskPageRitual};
  g->fillRect(0, kDockY - 8, 1024, 82, t.shell);
  for (uint8_t i = 0; i < 5; ++i) {
    bool active = navigator_.active() == pages[i];
    int16_t x = 12 + i * 204;
    button(g, t, x, kDockY, 184, 58, labels[i], active || i == 2);
  }
}

void DeskApp::drawDesk(Arduino_GFX *g) {
  const DeskThemePalette &t = theme();
  drawHeader(g, "CYPHER DESK", "a calm place for words", false, false);
  card(g, t, 28, 106, 620, 92, t.accent);
  smallText(g, 48, 124, "CONTINUE WRITING", t.accent);
  String last = settings_.lastDocument();
  Widgets::text(g, 48, 151, last.length() ? storage_.titleFromPath(last).c_str() : "Start a fresh page", Widgets::fontM(), t.ink, Widgets::kLeft);
  smallText(g, 626, 152, "OPEN", t.muted, Widgets::kRight);
  card(g, t, 28, 208, 620, 92, t.accent2);
  smallText(g, 48, 226, "TODAY'S JOURNAL", t.accent2);
  Widgets::text(g, 48, 253, clock_.prettyDate().c_str(), Widgets::fontM(), t.ink, Widgets::kLeft);
  smallText(g, 626, 254, clock_.sourceLabel(), t.muted, Widgets::kRight);
  smallText(g, 28, 304, "RECENTLY EDITED", t.muted);
  for (uint8_t i = 0; i < 3; ++i) {
    int16_t x = 28 + i * 210;
    card(g, t, x, 326, 196, 108, i == 0 ? t.success : (i == 1 ? t.accent3 : t.warning));
    if (i < noteCount_) {
      smallText(g, x + 16, 346, fitLabel(g, notes_[i].title, 160, Widgets::fontS()), t.ink);
      smallText(g, x + 16, 390, notes_[i].extension, t.muted);
    } else smallText(g, x + 98, 366, "quiet space", t.muted, Widgets::kCenter);
  }
  DeskStats stats = storage_.stats(clock_.isoDate());
  card(g, t, 682, 106, 314, 280, t.line);
  smallText(g, 706, 126, "THIS WEEK", t.accent);
  Widgets::text(g, 706, 164, (String(stats.quietMinutesWeek) + " quiet minutes").c_str(), Widgets::fontM(), t.ink, Widgets::kLeft);
  smallText(g, 706, 214, String(stats.wordsAddedWeek) + " words added", t.muted);
  smallText(g, 706, 252, String(stats.sessionsCompleted) + " sessions completed", t.muted);
  smallText(g, 706, 306, "CURRENT NOTEBOOK", t.accent3);
  smallText(g, 706, 334, activeFolderTitle_.length() ? activeFolderTitle_ : "Loose pages", t.ink);
  Widgets::pill(g, 706, 352, storage_.sourceLabel(), Widgets::fontS(), t.background,
                storage_.persistent() ? t.success : t.warning);
  button(g, t, 694, 406, 142, 56, "NEW TXT");
  button(g, t, 850, 406, 142, 56, "NEW MD", true);
  if (!storage_.persistent()) smallText(g, 843, 486, "RAM DEMO // CHANGES ARE TEMPORARY", t.warning, Widgets::kCenter);
  drawDock(g);
}

void DeskApp::drawNotebooks(Arduino_GFX *g) {
  const DeskThemePalette &t = theme();
  drawHeader(g, activeFolderTitle_.length() ? activeFolderTitle_ : "NOTEBOOKS", "one-level folders, ordinary files", false, false);
  button(g, t, 28, 94, 186, 48, "+ NOTEBOOK"); button(g, t, 226, 94, 154, 48, "+ NOTE");
  if (activeFolderPath_.length()) {
    button(g, t, 392, 94, 128, 48, "UP");
    DeskNoteMetadata notebook = storage_.metadataFor(activeFolderPath_);
    const char *colorNames[] = {"COLOR 1", "COLOR 2", "COLOR 3", "COLOR 4", "COLOR 5"};
    const uint16_t colorSwatches[] = {t.accent, t.accent2, t.accent3, t.success, t.warning};
    button(g, t, 532, 94, 128, 48, colorNames[notebook.notebookColor % 5], true);
    g->fillCircle(642, 118, 7, colorSwatches[notebook.notebookColor % 5]);
  }
  uint8_t visibleFolders = !activeFolderPath_.length() && pageOffset_ == 0
                               ? min(static_cast<uint8_t>(6), folderCount_) : 0;
  uint16_t total = visibleFolders + noteCount_;
  for (uint8_t row = 0; row < 6; ++row) {
    int16_t y = 152 + row * 56;
    if (row >= total) break;
    String label, detail; uint16_t accent = t.ink;
    if (row < visibleFolders) { label = "/ " + folders_[row].title; detail = "notebook"; accent = t.warning; }
    else {
      uint16_t ni = row - visibleFolders;
      DeskNoteMetadata m = storage_.metadataFor(notes_[ni].path);
      label = notes_[ni].title; detail = String(m.favorite ? "heart " : "") + (m.finished ? "finished" : notes_[ni].extension);
      accent = m.finished ? t.success : t.ink;
    }
    bool active = selected_ == row;
    Widgets::panel(g, 24, y, 642, 50, 10, active ? t.panelHighlight : t.panel, active ? 3 : 1, active ? t.accent2 : t.line);
    smallText(g, 44, y + 18, fitLabel(g, label, 430, Widgets::fontS()), accent);
    smallText(g, 646, y + 18, detail, t.muted, Widgets::kRight);
  }
  card(g, t, 680, 146, 320, 300, t.line);
  const char *actions[] = {"OPEN", "FAVORITE", "FINISHED", "EXPORT", "MOVE", "RENAME", "DELETE"};
  for (uint8_t i = 0; i < 7; ++i) button(g, t, i % 2 ? 842 : 690, 158 + (i / 2) * 66, 138, 54, actions[i], i == 0);
  button(g, t, 690, 452, 138, 48, "PREV", pageOffset_ > 0);
  button(g, t, 842, 452, 138, 48, "NEXT", hasMore_);
  drawDock(g);
}

void DeskApp::drawArchive(Arduino_GFX *g) {
  const DeskThemePalette &t = theme();
  drawHeader(g, "ARCHIVE", "a filtered library, never a second folder", true, false);
  const char *filters[] = {"RECENT", "FAVORITES", "FINISHED", "DAILY", "SCRAPS"};
  for (uint8_t i = 0; i < 5; ++i) button(g, t, 24 + i * 132, 94, 120, 44, filters[i], archiveFilter_ == i);
  button(g, t, 690, 94, 138, 44, archiveQuery_.length() ? "SEARCHED" : "SEARCH", archiveQuery_.length());
  button(g, t, 842, 94, 138, 44, "TAGS", archiveTagOnly_);
  if (archiveQuery_.length()) smallText(g, 24, 140, String(archiveTagOnly_ ? "TAG  //  " : "SEARCH  //  ") + archiveQuery_, t.accent);
  for (uint8_t row = 0; row < 6 && row < noteCount_; ++row) {
    int16_t y = 152 + row * 56;
    DeskNoteMetadata m = storage_.metadataFor(notes_[row].path);
    Widgets::panel(g, 24, y, 642, 50, 10, selected_ == row ? t.panelHighlight : t.panel,
                   selected_ == row ? 3 : 1, selected_ == row ? t.accent2 : t.line);
    smallText(g, 44, y + 18, fitLabel(g, notes_[row].title, 430, Widgets::fontS()), t.ink);
    smallText(g, 646, y + 18, String(m.favorite ? "heart " : "") + (m.finished ? "finished" : notes_[row].extension), t.muted, Widgets::kRight);
  }
  card(g, t, 680, 146, 320, 300, t.line);
  const char *actions[] = {"OPEN", "FAVORITE", "FINISHED", "EXPORT", "MOVE", "RENAME", "DELETE"};
  for (uint8_t i = 0; i < 7; ++i) button(g, t, i % 2 ? 842 : 690, 158 + (i / 2) * 66, 138, 54, actions[i], i == 0);
  button(g, t, 690, 452, 138, 48, "PREV", pageOffset_ > 0);
  button(g, t, 842, 452, 138, 48, "NEXT", hasMore_);
}

void DeskApp::drawFocus(Arduino_GFX *g) {
  const DeskThemePalette &t = theme();
  drawHeader(g, "FOCUS", "choose a gentle pocket of writing time", false, false);
  const uint16_t options[] = {10, 20, 30, 45};
  for (uint8_t i = 0; i < 4; ++i) {
    int16_t x = 72 + i * 222;
    card(g, t, x, 140, 184, 88, focusMinutes_ == options[i] ? t.accent2 : t.line);
    Widgets::text(g, x + 92, 160, (String(options[i]) + " min").c_str(), Widgets::fontM(), t.ink, Widgets::kCenter);
  }
  button(g, t, 350, 276, 324, 72, "START WRITING", true);
  DeskStats s = storage_.stats(clock_.isoDate());
  smallText(g, 512, 382, String(s.quietMinutesToday) + " quiet minutes today", t.accent, Widgets::kCenter);
  smallText(g, 512, 420, "Five lines stay near the cursor. No goals, no streaks.", t.muted, Widgets::kCenter);
  drawDock(g);
}

void DeskApp::drawRitual(Arduino_GFX *g) {
  const DeskThemePalette &t = theme();
  drawHeader(g, "RITUAL", prompts_.usingSdPrompts() ? "40 prompts from SD" : "40 prompts tucked into firmware", false, false);
  const char *shortNames[] = {"MORNING", "OBSERVE", "MEMORY", "SCENE", "LETTER"};
  for (uint8_t i = 0; i < 5; ++i) button(g, t, 24 + i * 196, 96, 184, 48, shortNames[i], prompts_.category() == i);
  card(g, t, 70, 170, 906, 204, t.accent2);
  smallText(g, 100, 194, prompts_.categoryName(), t.accent);
  String prompt = prompts_.current();
  int split = prompt.length() > 54 ? prompt.lastIndexOf(' ', 54) : -1;
  Widgets::text(g, 100, 242, (split > 0 ? prompt.substring(0, split) : prompt).c_str(), Widgets::fontM(), t.ink, Widgets::kLeft);
  if (split > 0) Widgets::text(g, 100, 284, prompt.substring(split + 1).c_str(), Widgets::fontM(), t.ink, Widgets::kLeft);
  smallText(g, 100, 334, "Prompts do not repeat until this category cycles.", t.muted);
  button(g, t, 40, 404, 150, 56, "SHUFFLE");
  button(g, t, 206, 404, 150, 56, String(settings_.focusMinutes()) + " MIN");
  button(g, t, 372, 404, 150, 56, theme().name);
  button(g, t, 538, 404, 150, 56, audioAmbienceName());
  button(g, t, 716, 398, 260, 68, "WRITE FROM THIS", true);
  drawDock(g);
}

void DeskApp::drawSettings(Arduino_GFX *g) {
  const DeskThemePalette &t = theme();
  drawHeader(g, "SETTINGS", "local preferences only", true, false);
  button(g, t, 56, 126, 410, 64, "THEME  //  " + String(t.name), true);
  button(g, t, 56, 208, 410, 64, "WI-FI SSID  //  " + String(settings_.wifiSsid().length() ? settings_.wifiSsid() : "not set"));
  button(g, t, 56, 290, 410, 64, "WI-FI PASSWORD  //  " + String(settings_.wifiPassword().length() ? "********" : "not set"));
  button(g, t, 56, 372, 196, 64, "PREVIOUS DAY"); button(g, t, 270, 372, 196, 64, "NEXT DAY");
  button(g, t, 540, 126, 410, 64, "KEYS  //  " + String(audioKeySoundName()));
  button(g, t, 540, 208, 410, 64, "AMBIENCE  //  " + String(audioAmbienceName()));
  button(g, t, 540, 290, 196, 64, "CONFIRM DATE"); button(g, t, 754, 290, 196, 64, "SYNC TIME");
  button(g, t, 540, 372, 410, 64, "SAFE EJECT SD", false);
  smallText(g, 56, 470, clock_.isoDate() + " // " + clock_.status(), t.muted);
  smallText(g, 950, 470, storage_.status(), storage_.persistent() ? t.success : t.warning, Widgets::kRight);
}

void DeskApp::drawEditor(Arduino_GFX *g) {
  const DeskThemePalette &t = theme();
  if (focusActive_) {
    g->fillRect(0, 0, 1024, 82, t.shell);
    button(g, t, 14, 12, 116, 52, "EXIT");
    uint32_t remaining = focusRemainingSeconds();
    char timer[12]; snprintf(timer, sizeof(timer), "%02lu:%02lu", remaining / 60, remaining % 60);
    Widgets::text(g, 512, 17, timer, Widgets::fontL(), t.ink, Widgets::kCenter);
    smallText(g, 512, 53, String(countWords(editor_.buffer)) + " words // " + (editor_.dirty ? "saving..." : "saved"), t.muted, Widgets::kCenter);
    button(g, t, 714, 12, 110, 52, focusPaused_ ? "RESUME" : "PAUSE");
    button(g, t, 842, 12, 164, 52, "+ 5 MIN");
  } else {
    String subtitle = String(editor_.dirty ? "AUTOSAVING" : "SAVED") + " // " + countWords(editor_.buffer) + " words";
    drawHeader(g, editor_.document.title, subtitle, true, true);
    if (editingScrap_) button(g, t, 732, 14, 120, 54, "MOVE");
  }
  card(g, t, 20, 94, 984, 210, focusActive_ ? t.success : t.accent);
  if (!focusActive_) smallText(g, 38, 110, editor_.document.path, t.accent3);
  g->setFont(u8g2_font_cubic11_h_cjk); g->setUTF8Print(true); g->setTextSize(1); g->setTextColor(t.ink);
  uint16_t cursorLine = lineNumberForIndex(editor_.buffer, editor_.cursor);
  uint16_t cursorColumn = columnForIndex(editor_.buffer, editor_.cursor);
  uint8_t rows = focusActive_ ? 5 : 8;
  int16_t baseY = focusActive_ ? 128 : 144;
  int16_t rowH = focusActive_ ? 34 : 18;
  for (uint8_t row = 0; row < rows; ++row) {
    uint16_t lineNo = editor_.topLine + row;
    size_t start = indexForLineAndColumn(editor_.buffer, lineNo, 0);
    size_t end = lineEndForIndex(editor_.buffer, start);
    String line = editor_.buffer.substring(start, end);
    uint16_t sliceEnd = editor_.leftColumn + 78;
    if (sliceEnd > line.length()) sliceEnd = line.length();
    line = editor_.leftColumn < line.length() ? line.substring(editor_.leftColumn, sliceEnd) : "";
    int16_t y = baseY + row * rowH;
    if (focusActive_ && lineNo == cursorLine) g->fillRoundRect(32, y - 21, 960, 30, 8, t.panelHighlight);
    g->setCursor(42, y); if (line.length()) g->print(line);
    if (!editor_.buffer.length() && row == 0) { g->setTextColor(t.muted); g->print("Start writing..."); g->setTextColor(t.ink); }
    if (lineNo == cursorLine) {
      int16_t col = cursorColumn >= editor_.leftColumn ? cursorColumn - editor_.leftColumn : 0;
      g->fillRect(42 + min(col, static_cast<int16_t>(78)) * 11, y - 14, 3, 16, t.accent2);
    }
  }
  if (editingScrap_ && !focusActive_) button(g, t, 20, 268, 140, 38, "DISCARD");
  if (keyboardDirty_) { keyboard_.draw(g, t); keyboardDirty_ = false; }
}

void DeskApp::drawTextInput(Arduino_GFX *g) {
  const DeskThemePalette &t = theme();
  String title = inputPurpose_ == kDeskInputNotebook ? "NAME NOTEBOOK" :
                 inputPurpose_ == kDeskInputRename ? "RENAME NOTE" :
                 inputPurpose_ == kDeskInputSearch ? "SEARCH NOTES" :
                 inputPurpose_ == kDeskInputWifiSsid ? "WI-FI NAME" :
                 inputPurpose_ == kDeskInputWifiPassword ? "WI-FI PASSWORD" : "NAME NOTE";
  drawHeader(g, title, "touch keyboard // return also confirms", true, true);
  card(g, t, 64, 130, 896, 126, t.accent2);
  String shown = inputBuffer_;
  if (inputPurpose_ == kDeskInputWifiPassword && inputBuffer_.length()) {
    shown = "";
    for (size_t i = 0; i < inputBuffer_.length(); ++i) shown += '*';
  }
  Widgets::text(g, 94, 170, shown.length() ? fitLabel(g, shown, 820, Widgets::fontL()).c_str() : "Type here...", Widgets::fontL(), shown.length() ? t.ink : t.muted, Widgets::kLeft);
  if (keyboardDirty_) { keyboard_.draw(g, t); keyboardDirty_ = false; }
}

void DeskApp::drawMessage(Arduino_GFX *g) {
  const DeskThemePalette &t = theme();
  drawHeader(g, messageTitle_, "tap anywhere to return", false, false);
  card(g, t, 128, 150, 768, 300, t.accent2);
  Widgets::text(g, 512, 218, messageTitle_.c_str(), Widgets::fontL(), t.ink, Widgets::kCenter);
  smallText(g, 512, 290, fitLabel(g, messageBody_, 680, Widgets::fontS()), t.muted, Widgets::kCenter);
  Widgets::text(g, 512, 336, "*  (^ o ^)  *", Widgets::fontL(), t.accent, Widgets::kCenter);
  button(g, t, 412, 382, 200, 56, "RETURN", true);
}

void DeskApp::drawConfirm(Arduino_GFX *g) {
  const DeskThemePalette &t = theme();
  drawHeader(g, confirmTitle_, "this action needs a deliberate tap", false, false);
  card(g, t, 180, 150, 664, 300, t.warning);
  Widgets::text(g, 512, 214, confirmTitle_.c_str(), Widgets::fontL(), t.ink, Widgets::kCenter);
  smallText(g, 512, 286, confirmBody_, t.muted, Widgets::kCenter);
  button(g, t, 274, 360, 210, 62, "KEEP IT");
  button(g, t, 540, 360, 210, 62, "CONFIRM", true);
}

void DeskApp::draw() {
  Arduino_GFX *g = CrowDisplay::canvas();
  if (!g) return;
  // Clear only the page area when the keyboard is up. Repainting the whole
  // screen on every keystroke wiped the pressed-key highlight out from under
  // the finger that was still holding it - and repainted 296 rows of keys to
  // show one new character.
  if (keyboardVisible()) {
    g->fillRect(0, 0, DeskKeyboardLayout::kScreenW, DeskKeyboardLayout::kBandY,
                theme().background);
  } else {
    g->fillScreen(theme().background);
  }
  switch (navigator_.active()) {
    case kDeskPageDesk: drawDesk(g); break;
    case kDeskPageNotebooks: drawNotebooks(g); break;
    case kDeskPageArchive: drawArchive(g); break;
    case kDeskPageFocus: drawFocus(g); break;
    case kDeskPageRitual: drawRitual(g); break;
    case kDeskPageSettings: drawSettings(g); break;
    case kDeskPageEditor: drawEditor(g); break;
    case kDeskPageTextInput: drawTextInput(g); break;
    case kDeskPageConfirm: drawConfirm(g); break;
    default: drawMessage(g); break;
  }
}
#endif
