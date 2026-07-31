#include "DeskUtilityApplication.h"

#include "DeskAppRouter.h"
#include "DeskEventBus.h"
#include "DeskSettings.h"
#include "DeskSystemServices.h"
#include "DeskTheme.h"
#include "DeskTouchKeyboard.h"
#include "DeskWriterApplication.h"

#include <CrowPanelShared.h>
#include <Preferences.h>
#include <math.h>

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
#include <Arduino_GFX_Library.h>
#include <U8g2lib.h>
#endif

namespace {
constexpr uint8_t kMaxCalendarEvents = 24;
constexpr uint8_t kMaxContacts = 32;
DeskCalendarEvent gCalendar[kMaxCalendarEvents];
DeskContactRecord gContacts[kMaxContacts];
uint8_t gCalendarCount = 0;
uint8_t gContactCount = 0;
// Which SD mount generation these records came from. UINT32_MAX means "never
// read". This used to be a plain bool, which meant a card inserted after boot
// left Calendar and Contacts permanently empty.
uint32_t gLoadedGeneration = 0xFFFFFFFFu;

const char *appTitle(DeskAppId id) {
  switch (id) {
    case kDeskAppToday: return "Today";
    case kDeskAppCalendar: return "Calendar";
    case kDeskAppContacts: return "Contacts";
    case kDeskAppClock: return "Clock";
    case kDeskAppCalculator: return "Calculator";
    case kDeskAppFiles: return "Files";
    case kDeskAppSettings: return "Settings";
    case kDeskAppRecorder: return "Recorder";
    case kDeskAppMusic: return "Music";
    case kDeskAppPodcasts: return "Podcasts";
    case kDeskAppWeather: return "Weather";
    default: return "App";
  }
}
bool inside(int16_t x, int16_t y, int16_t rx, int16_t ry, int16_t rw, int16_t rh) {
  return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}
String cleanField(String value) {
  value.replace('\t', ' ');
  value.replace('\n', ' ');
  value.replace('\r', ' ');
  value.trim();
  return value;
}
String fieldAt(const String &line, uint8_t index) {
  int start = 0;
  for (uint8_t i = 0; i < index; ++i) {
    start = line.indexOf('\t', start);
    if (start < 0) return "";
    ++start;
  }
  int end = line.indexOf('\t', start);
  return end < 0 ? line.substring(start) : line.substring(start, end);
}
bool validIsoDate(const String &value) {
  if (value.length() != 10 || value[4] != '-' || value[7] != '-') return false;
  for (uint8_t i = 0; i < value.length(); ++i)
    if (i != 4 && i != 7 && (value[i] < '0' || value[i] > '9')) return false;
  int month = value.substring(5, 7).toInt(); int day = value.substring(8, 10).toInt();
  return month >= 1 && month <= 12 && day >= 1 && day <= 31;
}
bool validTimeText(const String &value) {
  if (value.length() != 5 || value[2] != ':') return false;
  for (uint8_t i = 0; i < value.length(); ++i)
    if (i != 2 && (value[i] < '0' || value[i] > '9')) return false;
  return value.substring(0, 2).toInt() <= 23 && value.substring(3, 5).toInt() <= 59;
}
bool leapYear(int16_t year) { return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0; }
uint8_t daysInMonth(int16_t year, uint8_t month) {
  static const uint8_t days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  return month == 2 ? static_cast<uint8_t>(days[1] + (leapYear(year) ? 1 : 0)) : days[month - 1];
}
uint8_t weekdaySundayFirst(int16_t year, uint8_t month, uint8_t day) {
  static const uint8_t offsets[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  int16_t adjustedYear = year - (month < 3 ? 1 : 0);
  return static_cast<uint8_t>((adjustedYear + adjustedYear / 4 - adjustedYear / 100 + adjustedYear / 400 +
                               offsets[month - 1] + day) % 7);
}
String isoDate(int16_t year, uint8_t month, uint8_t day) {
  char buffer[12];
  snprintf(buffer, sizeof(buffer), "%04d-%02u-%02u", year, month, day);
  return String(buffer);
}
const char *monthName(uint8_t month) {
  static const char *names[] = {"January", "February", "March", "April", "May", "June",
                                "July", "August", "September", "October", "November", "December"};
  return month >= 1 && month <= 12 ? names[month - 1] : "Month";
}
void loadLocalData(DeskStorageService *storage) {
  if (storage == nullptr) return;
  const uint32_t generation = storage->mountGeneration();
  if (gLoadedGeneration == generation) return;
  gLoadedGeneration = generation;
  // A reload replaces the records rather than appending to them.
  gCalendarCount = 0;
  gContactCount = 0;
  String body = storage->readText(CYPHER_DESK_CALENDAR_DIR "/events.tsv");
  if (body.startsWith("# cypher-desk-calendar schema=1\n")) {
    int start = body.indexOf('\n') + 1;
    while (start > 0 && start < static_cast<int>(body.length()) && gCalendarCount < kMaxCalendarEvents) {
      int end = body.indexOf('\n', start);
      if (end < 0) end = body.length();
      String line = body.substring(start, end);
      if (line.length()) {
        DeskCalendarEvent &event = gCalendar[gCalendarCount++];
        event.date = fieldAt(line, 0);
        event.time = fieldAt(line, 1);
        event.title = fieldAt(line, 2);
        event.notes = fieldAt(line, 3);
        event.alarm = fieldAt(line, 4).toInt() != 0;
      }
      start = end + 1;
    }
  }
  body = storage->readText(CYPHER_DESK_CONTACTS_DIR "/contacts.tsv");
  if (body.startsWith("# cypher-desk-contacts schema=1\n")) {
    int start = body.indexOf('\n') + 1;
    while (start > 0 && start < static_cast<int>(body.length()) && gContactCount < kMaxContacts) {
      int end = body.indexOf('\n', start);
      if (end < 0) end = body.length();
      String line = body.substring(start, end);
      if (line.length()) {
        DeskContactRecord &contact = gContacts[gContactCount++];
        contact.name = fieldAt(line, 0);
        contact.organization = fieldAt(line, 1);
        contact.phone = fieldAt(line, 2);
        contact.email = fieldAt(line, 3);
        contact.notes = fieldAt(line, 4);
      }
      start = end + 1;
    }
  }
}
bool saveCalendar(DeskStorageService *storage) {
  if (storage == nullptr) return false;
  String body = "# cypher-desk-calendar schema=1\n";
  for (uint8_t i = 0; i < gCalendarCount; ++i) {
    DeskCalendarEvent &event = gCalendar[i];
    body += cleanField(event.date) + "\t" + cleanField(event.time) + "\t" +
            cleanField(event.title) + "\t" + cleanField(event.notes) + "\t" +
            String(event.alarm ? 1 : 0) + "\n";
  }
  return storage->atomicWrite(CYPHER_DESK_CALENDAR_DIR "/events.tsv", body);
}
bool saveContacts(DeskStorageService *storage) {
  if (storage == nullptr) return false;
  String body = "# cypher-desk-contacts schema=1\n";
  for (uint8_t i = 0; i < gContactCount; ++i) {
    DeskContactRecord &contact = gContacts[i];
    body += cleanField(contact.name) + "\t" + cleanField(contact.organization) + "\t" +
            cleanField(contact.phone) + "\t" + cleanField(contact.email) + "\t" +
            cleanField(contact.notes) + "\n";
  }
  return storage->atomicWrite(CYPHER_DESK_CONTACTS_DIR "/contacts.tsv", body);
}

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
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
void osButton(Arduino_GFX *g, const DeskThemePalette &theme, int16_t x, int16_t y,
              int16_t w, int16_t h, const String &label, bool active = false) {
  Widgets::panel(g, x + 3, y + 4, w, h, 10, theme.background);
  Widgets::panel(g, x, y, w, h, 10, active ? theme.panelHighlight : theme.panel,
                 active ? 3 : 1, active ? theme.accent2 : theme.line);
  smallText(g, x + w / 2, y + h / 2 - 3, label, active ? theme.ink : theme.muted,
            Widgets::kCenter);
}
void osCard(Arduino_GFX *g, const DeskThemePalette &theme, int16_t x, int16_t y,
            int16_t w, int16_t h, uint16_t accent) {
  Widgets::panel(g, x + 4, y + 5, w, h, 13, theme.background);
  Widgets::panel(g, x, y, w, h, 13, theme.panel, 2, accent);
}
#endif
}

DeskUtilityApplication::DeskUtilityApplication(DeskAppId appId) : appId_(appId) {}
DeskAppId DeskUtilityApplication::id() const { return appId_; }
const char *DeskUtilityApplication::title() const { return appTitle(appId_); }
void DeskUtilityApplication::begin(DeskAppContext &context) {
  context_ = &context;
  loadLocalData(context.storage);
  if (appId_ == kDeskAppClock) loadClockSettings();
  if (appId_ == kDeskAppFiles) { fileDirectory_ = CYPHER_DESK_ROOT_DIR; refreshFiles(); }
  if (appId_ == kDeskAppCalendar) initializeCalendarMonth();
}
void DeskUtilityApplication::onEnter() {
  dirty_ = true;
  selected_ = 0;
  filePreview_ = "";
  // Cheap no-op unless the card was mounted since the last read.
  if (context_ != nullptr) loadLocalData(context_->storage);
  if (appId_ == kDeskAppFiles) refreshFiles();
  if (appId_ == kDeskAppCalendar) initializeCalendarMonth();
  if (appId_ == kDeskAppMusic) { fileDirectory_ = CYPHER_DESK_MUSIC_DIR; refreshFiles(); }
  if (appId_ == kDeskAppPodcasts) { fileDirectory_ = CYPHER_DESK_PODCASTS_DIR; refreshFiles(); }
}
bool DeskUtilityApplication::dirty() const { return dirty_; }
void DeskUtilityApplication::clearDirty() { dirty_ = false; }
bool DeskUtilityApplication::alarmEnabled() const { return alarmEnabled_; }
void DeskUtilityApplication::tick(uint32_t nowMs) {
  if (nowMs - lastRefreshMs_ >= 1000) {
    lastRefreshMs_ = nowMs;
    if (!dirty_ && (appId_ == kDeskAppClock || appId_ == kDeskAppToday || appId_ == kDeskAppSettings))
      refreshDynamic();
  }
  if (appId_ == kDeskAppWeather && context_ != nullptr && context_->weather != nullptr) {
    String snapshot = weatherSnapshot();
    if (snapshot != weatherSnapshot_) { weatherSnapshot_ = snapshot; dirty_ = true; }
  }
  if (appId_ == kDeskAppRecorder && !dirty_ && context_ != nullptr && context_->audio != nullptr &&
      nowMs - lastLiveRefreshMs_ >= 200) {
    lastLiveRefreshMs_ = nowMs;
    refreshDynamic();
  }
  if (timerRunning_ && nowMs - timerStartedMs_ >= static_cast<uint32_t>(timerMinutes_) * 60000UL) {
    timerRunning_ = false;
    if (context_ != nullptr && context_->events != nullptr)
      context_->events->publish(kDeskEventAlarm, "timer complete");
    dirty_ = true;
  }
}
void DeskUtilityApplication::refreshDynamic() {
  if (context_ == nullptr || dirty_) return;
  drawStatusBar();
  if (appId_ == kDeskAppClock) drawClockDynamic();
  else if (appId_ == kDeskAppSettings) {
    String snapshot = settingsSnapshot();
    if (snapshot != settingsSnapshot_) drawSettingsDynamic();
  } else if (appId_ == kDeskAppRecorder) drawRecorderDynamic();
}
void DeskUtilityApplication::refreshFiles() {
  fileEntryCount_ = 0;
  if (context_ == nullptr || context_->storage == nullptr) return;
  fileEntryCount_ = context_->storage->listDirectory(fileDirectory_, fileEntries_, 12);
  if (selected_ >= fileEntryCount_) selected_ = fileEntryCount_ ? fileEntryCount_ - 1 : 0;
}
void DeskUtilityApplication::initializeCalendarMonth() {
  if (calendarMonthInitialized_ || context_ == nullptr || context_->time == nullptr) return;
  String date = context_->time->dateText();
  if (!validIsoDate(date) && context_->settings != nullptr) date = context_->settings->savedDate();
  if (validIsoDate(date)) {
    calendarYear_ = date.substring(0, 4).toInt();
    calendarMonth_ = static_cast<uint8_t>(date.substring(5, 7).toInt());
    pendingCalendarDate_ = date;
  }
  calendarMonthInitialized_ = true;
}
void DeskUtilityApplication::shiftCalendarMonth(int8_t delta) {
  int16_t month = static_cast<int16_t>(calendarMonth_) + delta;
  if (month < 1) { month = 12; --calendarYear_; }
  else if (month > 12) { month = 1; ++calendarYear_; }
  calendarMonth_ = static_cast<uint8_t>(month);
  if (!pendingCalendarDate_.startsWith(String(calendarYear_) + "-"))
    pendingCalendarDate_ = isoDate(calendarYear_, calendarMonth_, 1);
}
bool DeskUtilityApplication::selectedContactMatches(uint8_t index) const {
  if (index >= gContactCount || !contactQuery_.length()) return index < gContactCount;
  String needle = contactQuery_; needle.toLowerCase();
  String haystack = gContacts[index].name + " " + gContacts[index].organization + " " +
                    gContacts[index].phone + " " + gContacts[index].email + " " + gContacts[index].notes;
  haystack.toLowerCase();
  return haystack.indexOf(needle) >= 0;
}
void DeskUtilityApplication::beginInput(InputPurpose purpose, const String &initial) {
  inputPurpose_ = purpose;
  input_ = initial;
  if (context_ != nullptr && context_->keyboard != nullptr) context_->keyboard->reset();
  dirty_ = true;
}
void DeskUtilityApplication::cancelInput() { inputPurpose_ = kInputNone; input_ = ""; dirty_ = true; }
void DeskUtilityApplication::finishInput() {
  if (context_ == nullptr) { cancelInput(); return; }
  input_.trim();
  if (inputPurpose_ == kInputCalendarTitle && input_.length() && gCalendarCount < kMaxCalendarEvents) {
    String date = validIsoDate(pendingCalendarDate_) ? pendingCalendarDate_ : context_->time->dateText();
    gCalendar[gCalendarCount++] = {date, "09:00", input_, "", false};
    saveCalendar(context_->storage);
  } else if (inputPurpose_ == kInputCalendarEditTitle && input_.length() && selected_ < gCalendarCount) {
    gCalendar[selected_].title = input_;
    saveCalendar(context_->storage);
  } else if (inputPurpose_ == kInputCalendarDate && selected_ < gCalendarCount && validIsoDate(input_)) {
    gCalendar[selected_].date = input_; saveCalendar(context_->storage);
  } else if (inputPurpose_ == kInputCalendarTime && selected_ < gCalendarCount && validTimeText(input_)) {
    gCalendar[selected_].time = input_; saveCalendar(context_->storage);
  } else if (inputPurpose_ == kInputCalendarNotes && selected_ < gCalendarCount) {
    gCalendar[selected_].notes = input_; saveCalendar(context_->storage);
  } else if (inputPurpose_ == kInputContactName && input_.length() && gContactCount < kMaxContacts) {
    gContacts[gContactCount++] = {input_, "", "", "", ""};
    saveContacts(context_->storage);
  } else if (inputPurpose_ == kInputContactEditName && input_.length() && selected_ < gContactCount) {
    gContacts[selected_].name = input_;
    saveContacts(context_->storage);
  } else if (inputPurpose_ == kInputContactOrganization && selected_ < gContactCount) {
    gContacts[selected_].organization = input_; saveContacts(context_->storage);
  } else if (inputPurpose_ == kInputContactPhone && selected_ < gContactCount) {
    gContacts[selected_].phone = input_; saveContacts(context_->storage);
  } else if (inputPurpose_ == kInputContactEmail && selected_ < gContactCount) {
    gContacts[selected_].email = input_; saveContacts(context_->storage);
  } else if (inputPurpose_ == kInputContactNotes && selected_ < gContactCount) {
    gContacts[selected_].notes = input_; saveContacts(context_->storage);
  } else if (inputPurpose_ == kInputContactSearch) {
    contactQuery_ = input_;
  } else if (inputPurpose_ == kInputFileFolder && input_.length()) {
    context_->storage->ensureDirectory(fileDirectory_ + "/" + input_); refreshFiles();
  } else if (inputPurpose_ == kInputFileRename && input_.length() && pendingFilePath_.length()) {
    context_->storage->renamePath(pendingFilePath_, fileDirectory_ + "/" + input_); pendingFilePath_ = ""; refreshFiles();
  } else if (inputPurpose_ == kInputFileCopy && input_.length() && pendingFilePath_.length()) {
    context_->storage->copyFile(pendingFilePath_, fileDirectory_ + "/" + input_); pendingFilePath_ = ""; refreshFiles();
  } else if (inputPurpose_ == kInputFileMove && input_.length() && pendingFilePath_.length()) {
    context_->storage->moveFile(pendingFilePath_, input_); pendingFilePath_ = ""; refreshFiles();
  } else if (inputPurpose_ == kInputWifiPassword && pendingSsid_.length()) {
    context_->wifi->connect(pendingSsid_, input_, true);
  } else if (inputPurpose_ == kInputHiddenSsid && input_.length()) {
    pendingSsid_ = input_;
    beginInput(kInputHiddenPassword);
    return;
  } else if (inputPurpose_ == kInputHiddenPassword && pendingSsid_.length()) {
    context_->wifi->connect(pendingSsid_, input_, true);
  }
  inputPurpose_ = kInputNone;
  input_ = "";
  dirty_ = true;
}
void DeskUtilityApplication::handleKeyboard(int16_t x, int16_t y) {
  if (context_ == nullptr || context_->keyboard == nullptr) return;
  DeskKeyEvent key = context_->keyboard->hitTest(x, y);
  if (key.action == kDeskKeyShift || key.action == kDeskKeySymbols) {
    context_->keyboard->applyModeAction(key.action);
  } else if (key.action == kDeskKeyText && input_.length() < 96) {
    input_ += key.text;
    if (context_->keyboard->shifted()) context_->keyboard->applyModeAction(kDeskKeyShift);
  } else if (key.action == kDeskKeyBackspace && input_.length()) {
    input_.remove(input_.length() - 1);
  } else if (key.action == kDeskKeyEnter) {
    finishInput();
    return;
  }
  dirty_ = true;
}
bool DeskUtilityApplication::handleTouch(const DeskTouchEvent &event) {
  if (!event.released || context_ == nullptr) return false;
  int16_t x = event.x;
  int16_t y = event.y;
  if (inputPurpose_ != kInputNone) {
    if (inside(x, y, 18, 60, 120, 48)) cancelInput();
    else if (inside(x, y, 866, 60, 140, 48)) finishInput();
    else handleKeyboard(x, y);
    return true;
  }
  if (confirmingDelete_) {
    if (inside(x, y, 318, 352, 178, 60)) { confirmingDelete_ = false; deleteKind_ = kDeleteNone; dirty_ = true; return true; }
    if (inside(x, y, 528, 352, 178, 60)) {
      if (deleteKind_ == kDeleteFile && context_->storage != nullptr && pendingFilePath_.length() && !context_->storage->pathProtected(pendingFilePath_)) {
        context_->storage->removeFile(pendingFilePath_); refreshFiles();
      } else if (deleteKind_ == kDeleteCalendar && selected_ < gCalendarCount) {
        for (uint8_t i = selected_; i + 1 < gCalendarCount; ++i) gCalendar[i] = gCalendar[i + 1];
        --gCalendarCount; if (selected_ >= gCalendarCount && selected_) --selected_; saveCalendar(context_->storage);
      } else if (deleteKind_ == kDeleteContact && selected_ < gContactCount) {
        for (uint8_t i = selected_; i + 1 < gContactCount; ++i) gContacts[i] = gContacts[i + 1];
        --gContactCount; if (selected_ >= gContactCount && selected_) --selected_; saveContacts(context_->storage);
      }
      pendingFilePath_ = ""; confirmingDelete_ = false; deleteKind_ = kDeleteNone; dirty_ = true; return true;
    }
    return true;
  }
  if (inside(x, y, 12, 8, 112, 42)) { context_->router->home(); return true; }
  switch (appId_) {
    case kDeskAppToday:
      if (inside(x, y, 30, 174, 300, 92)) {
        context_->router->open(kDeskAppWriter);
        if (context_->writer != nullptr) context_->writer->writer().commandDaily();
      }
      else if (inside(x, y, 362, 174, 300, 92)) context_->router->open(kDeskAppCalendar);
      else if (inside(x, y, 694, 174, 300, 92)) context_->router->open(kDeskAppRecorder);
      else if (inside(x, y, 30, 330, 300, 60)) {
        context_->router->open(kDeskAppWriter);
        if (context_->writer != nullptr) context_->writer->writer().commandScrap();
      }
      break;
    case kDeskAppCalendar:
      initializeCalendarMonth();
      if (!calendarListView_) {
        if (inside(x, y, 24, 94, 84, 40)) shiftCalendarMonth(-1);
        else if (inside(x, y, 118, 94, 84, 40)) shiftCalendarMonth(1);
        else if (inside(x, y, 608, 94, 104, 40)) {
          calendarMonthInitialized_ = false; initializeCalendarMonth();
        } else if (inside(x, y, 722, 94, 104, 40)) calendarListView_ = true;
        else if (inside(x, y, 836, 94, 164, 40)) beginInput(kInputCalendarTitle);
        else if (inside(x, y, 20, 160, 980, 360)) {
          constexpr int16_t gridX = 20, gridY = 160, cellW = 140, cellH = 60;
          uint8_t col = (x - gridX) / cellW, row = (y - gridY) / cellH;
          uint8_t cell = row * 7 + col;
          uint8_t first = weekdaySundayFirst(calendarYear_, calendarMonth_, 1);
          int16_t day = static_cast<int16_t>(cell) - first + 1;
          if (day >= 1 && day <= daysInMonth(calendarYear_, calendarMonth_)) {
            pendingCalendarDate_ = isoDate(calendarYear_, calendarMonth_, static_cast<uint8_t>(day));
            for (uint8_t i = 0; i < gCalendarCount; ++i)
              if (gCalendar[i].date == pendingCalendarDate_) { selected_ = i; break; }
          }
        }
        dirty_ = true;
        break;
      }
      if (inside(x, y, 572, 72, 186, 48)) { calendarListView_ = false; dirty_ = true; break; }
      if (inside(x, y, 790, 72, 202, 48)) beginInput(kInputCalendarTitle);
      else if (inside(x, y, 790, 142, 202, 44) && gCalendarCount)
        beginInput(kInputCalendarEditTitle, gCalendar[selected_].title);
      else if (inside(x, y, 790, 198, 202, 44) && gCalendarCount)
        beginInput(kInputCalendarDate, gCalendar[selected_].date);
      else if (inside(x, y, 790, 254, 202, 44) && gCalendarCount)
        beginInput(kInputCalendarTime, gCalendar[selected_].time);
      else if (inside(x, y, 790, 310, 202, 44) && gCalendarCount)
        beginInput(kInputCalendarNotes, gCalendar[selected_].notes);
      else if (inside(x, y, 790, 366, 202, 44) && gCalendarCount) {
        gCalendar[selected_].alarm = !gCalendar[selected_].alarm; saveCalendar(context_->storage); dirty_ = true;
      } else if (inside(x, y, 790, 422, 202, 44) && gCalendarCount) {
        deleteKind_ = kDeleteCalendar; confirmingDelete_ = true;
      } else {
        for (uint8_t row = 0; row < min(static_cast<uint8_t>(6), gCalendarCount); ++row)
          if (inside(x, y, 28, 142 + row * 58, 720, 50)) { selected_ = row; pendingCalendarDate_ = gCalendar[row].date; dirty_ = true; }
      }
      break;
    case kDeskAppContacts:
      if (inside(x, y, 790, 72, 202, 48)) beginInput(kInputContactName);
      else if (inside(x, y, 570, 72, 188, 48)) beginInput(kInputContactSearch, contactQuery_);
      else if (inside(x, y, 790, 142, 202, 44) && gContactCount)
        beginInput(kInputContactEditName, gContacts[selected_].name);
      else if (inside(x, y, 790, 198, 202, 44) && gContactCount)
        beginInput(kInputContactOrganization, gContacts[selected_].organization);
      else if (inside(x, y, 790, 254, 202, 44) && gContactCount)
        beginInput(kInputContactPhone, gContacts[selected_].phone);
      else if (inside(x, y, 790, 310, 202, 44) && gContactCount)
        beginInput(kInputContactEmail, gContacts[selected_].email);
      else if (inside(x, y, 790, 366, 202, 44) && gContactCount)
        beginInput(kInputContactNotes, gContacts[selected_].notes);
      else if (inside(x, y, 790, 422, 202, 44) && gContactCount) {
        deleteKind_ = kDeleteContact; confirmingDelete_ = true;
      } else {
        for (uint8_t row = 0; row < min(static_cast<uint8_t>(6), gContactCount); ++row)
          if (selectedContactMatches(row) && inside(x, y, 28, 142 + row * 58, 520, 50)) { selected_ = row; dirty_ = true; }
      }
      break;
    case kDeskAppClock:
      if (inside(x, y, 30, 190, 300, 74)) {
        if (stopwatchRunning_) { stopwatchElapsedMs_ += millis() - stopwatchStartedMs_; stopwatchRunning_ = false; }
        else { stopwatchStartedMs_ = millis(); stopwatchRunning_ = true; }
        dirty_ = true;
      } else if (inside(x, y, 362, 190, 300, 74)) {
        if (timerRunning_) timerRunning_ = false;
        else { timerRunning_ = true; timerStartedMs_ = millis(); }
        dirty_ = true;
      } else if (inside(x, y, 694, 190, 140, 74)) { timerMinutes_ = min<uint16_t>(120, timerMinutes_ + 5); dirty_ = true; }
      else if (inside(x, y, 850, 190, 140, 74)) { timerMinutes_ = timerMinutes_ > 5 ? timerMinutes_ - 5 : 5; dirty_ = true; }
      else if (inside(x, y, 30, 316, 300, 74)) { alarmEnabled_ = !alarmEnabled_; saveClockSettings(); dirty_ = true; }
      break;
    case kDeskAppCalculator: {
      const char *keys[5][4] = {{"C", "+/-", "%", "/"}, {"7", "8", "9", "*"},
                                {"4", "5", "6", "-"}, {"1", "2", "3", "+"},
                                {"0", ".", "=", "="}};
      for (uint8_t row = 0; row < 5; ++row) for (uint8_t col = 0; col < 4; ++col)
        if (inside(x, y, 174 + col * 170, 146 + row * 78, 154, 64)) pressCalculator(keys[row][col]);
      break;
    }
    case kDeskAppFiles:
      if (inside(x, y, 30, 92, 118, 44)) {
        int slash = fileDirectory_.lastIndexOf('/');
        if (slash > 0 && fileDirectory_ != CYPHER_DESK_ROOT_DIR) fileDirectory_ = fileDirectory_.substring(0, slash);
        refreshFiles();
      } else if (inside(x, y, 164, 92, 160, 44)) beginInput(kInputFileFolder);
      else if (inside(x, y, 754, 132, 238, 44) && fileEntryCount_ && !fileEntries_[selected_].directory &&
               context_->storage->textFile(fileEntries_[selected_].path)) filePreview_ = context_->storage->readText(fileEntries_[selected_].path, 3000);
      else if (inside(x, y, 754, 188, 238, 44) && fileEntryCount_ && !fileEntries_[selected_].directory) {
        pendingFilePath_ = fileEntries_[selected_].path; beginInput(kInputFileRename, fileEntries_[selected_].name);
      } else if (inside(x, y, 754, 244, 238, 44) && fileEntryCount_ && !fileEntries_[selected_].directory) {
        pendingFilePath_ = fileEntries_[selected_].path; beginInput(kInputFileCopy, "copy-" + fileEntries_[selected_].name);
      } else if (inside(x, y, 754, 300, 238, 44) && fileEntryCount_ && !fileEntries_[selected_].directory) {
        pendingFilePath_ = fileEntries_[selected_].path; beginInput(kInputFileMove, String(CYPHER_DESK_DOCUMENTS_DIR) + "/" + fileEntries_[selected_].name);
      } else if (inside(x, y, 754, 356, 238, 44) && fileEntryCount_) {
        pendingFilePath_ = fileEntries_[selected_].path; deleteKind_ = kDeleteFile; confirmingDelete_ = true;
      } else if (inside(x, y, 774, 430, 218, 58)) context_->storage->safeEject();
      else if (inside(x, y, 774, 500, 218, 58)) context_->storage->remount();
      else for (uint8_t row = 0; row < min(static_cast<uint8_t>(6), fileEntryCount_); ++row) {
        if (!inside(x, y, 30, 150 + row * 64, 700, 54)) continue;
        selected_ = row;
        if (fileEntries_[row].directory) { fileDirectory_ = fileEntries_[row].path; refreshFiles(); }
        else filePreview_ = "";
        break;
      }
      dirty_ = true;
      break;
    case kDeskAppSettings:
      if (inside(x, y, 30, 150, 300, 64)) context_->settings->setTheme(nextDeskTheme(context_->settings->theme()));
      else if (inside(x, y, 362, 150, 300, 64)) context_->wifi->scan();
      else if (inside(x, y, 694, 150, 300, 64)) context_->wifi->setOffline(!context_->wifi->offline());
      else if (inside(x, y, 30, 232, 300, 54)) beginInput(kInputHiddenSsid);
      else if (inside(x, y, 362, 232, 300, 54)) diagnosticsVisible_ = !diagnosticsVisible_;
      else {
        uint8_t visible = context_->wifi->state() == kDeskWifiNetworksFound
                              ? context_->wifi->networkCount() : context_->wifi->savedCount();
        for (uint8_t row = 0; row < min(static_cast<uint8_t>(5), visible); ++row) {
          if (!inside(x, y, 30, 306 + row * 50, 640, 44)) continue;
          if (context_->wifi->state() == kDeskWifiNetworksFound) {
            DeskWifiNetwork network = context_->wifi->network(row);
            pendingSsid_ = network.ssid;
            if (network.secured) beginInput(kInputWifiPassword);
            else context_->wifi->connect(network.ssid, "", true);
          } else context_->wifi->connectSaved(row);
        }
      }
      dirty_ = true;
      break;
    case kDeskAppRecorder:
      if (inside(x, y, 180, 392, 300, 58)) context_->audio->startSpeakerTest();
      else if (inside(x, y, 512, 392, 332, 58)) {
        if (context_->audio->recording()) context_->audio->stopRecording();
        else context_->audio->startRecording();
      } else if (inside(x, y, 350, 466, 324, 54) && context_->audio->recordingPath().length())
        context_->audio->playWav(context_->audio->recordingPath());
      dirty_ = true;
      break;
    case kDeskAppMusic:
    case kDeskAppPodcasts:
      for (uint8_t row = 0; row < min(static_cast<uint8_t>(5), fileEntryCount_); ++row) {
        if (inside(x, y, 80, 166 + row * 58, 864, 48) && !fileEntries_[row].directory)
          context_->audio->playWav(fileEntries_[row].path);
      }
      if (inside(x, y, 760, 480, 184, 52)) context_->audio->stopPlayback();
      dirty_ = true;
      break;
    case kDeskAppWeather:
      if (inside(x, y, 360, 484, 304, 58) && context_->weather != nullptr) context_->weather->requestRefresh();
      dirty_ = true;
      break;
    default:
      break;
  }
  return true;
}
bool DeskUtilityApplication::handleBack() {
  if (inputPurpose_ != kInputNone) { cancelInput(); return true; }
  if (confirmingDelete_) { confirmingDelete_ = false; deleteKind_ = kDeleteNone; dirty_ = true; return true; }
  if (appId_ == kDeskAppFiles && filePreview_.length()) { filePreview_ = ""; dirty_ = true; return true; }
  if (appId_ == kDeskAppFiles && fileDirectory_ != CYPHER_DESK_ROOT_DIR) {
    int slash = fileDirectory_.lastIndexOf('/');
    if (slash > 0) fileDirectory_ = fileDirectory_.substring(0, slash);
    refreshFiles(); dirty_ = true; return true;
  }
  return false;
}

void DeskUtilityApplication::pressCalculator(const String &key) {
  if (key == "C") {
    calcDisplay_ = "0"; calcAccumulator_ = 0; calcOperation_ = 0; calcFresh_ = true;
  } else if (key == "+/-") {
    if (calcDisplay_ != "0") calcDisplay_ = calcDisplay_.startsWith("-") ? calcDisplay_.substring(1) : "-" + calcDisplay_;
  } else if (key == "%") {
    calcDisplay_ = String(calcDisplay_.toDouble() / 100.0, 6); calcFresh_ = true;
  } else if (key == "+" || key == "-" || key == "*" || key == "/") {
    calcAccumulator_ = calcDisplay_.toDouble(); calcOperation_ = key[0]; calcFresh_ = true;
  } else if (key == "=") {
    double right = calcDisplay_.toDouble();
    if (calcOperation_ == '+') calcAccumulator_ += right;
    else if (calcOperation_ == '-') calcAccumulator_ -= right;
    else if (calcOperation_ == '*') calcAccumulator_ *= right;
    else if (calcOperation_ == '/' && fabs(right) < 0.0000001) { calcDisplay_ = "Cannot divide by zero"; calcFresh_ = true; dirty_ = true; return; }
    else if (calcOperation_ == '/') calcAccumulator_ /= right;
    calcDisplay_ = String(calcAccumulator_, 6);
    while (calcDisplay_.endsWith("0")) calcDisplay_.remove(calcDisplay_.length() - 1);
    if (calcDisplay_.endsWith(".")) calcDisplay_.remove(calcDisplay_.length() - 1);
    calcOperation_ = 0; calcFresh_ = true;
  } else {
    if (calcFresh_ || calcDisplay_ == "0" || calcDisplay_.startsWith("Cannot")) { calcDisplay_ = ""; calcFresh_ = false; }
    if (key == "." && calcDisplay_.indexOf('.') >= 0) return;
    if (calcDisplay_.length() < 14) calcDisplay_ += key;
  }
  dirty_ = true;
}
void DeskUtilityApplication::loadClockSettings() {
  Preferences prefs;
  if (!prefs.begin("cypher-desk", true)) return;
  alarmEnabled_ = prefs.getBool("alarm-on", false);
  alarmMinuteOfDay_ = prefs.getUShort("alarm-min", 8 * 60);
  prefs.end();
}
void DeskUtilityApplication::saveClockSettings() {
  Preferences prefs;
  if (!prefs.begin("cypher-desk", false)) return;
  prefs.putBool("alarm-on", alarmEnabled_);
  prefs.putUShort("alarm-min", alarmMinuteOfDay_);
  prefs.end();
}

void DeskUtilityApplication::drawShell(const String &subtitle) {
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  if (context_ == nullptr) return;
  Arduino_GFX *g = CrowDisplay::canvas();
  if (g == nullptr) return;
  const DeskThemePalette &theme = deskTheme(context_->settings->theme());
  g->fillScreen(theme.background);
  g->fillRect(0, 0, 1024, 52, theme.shell);
  osButton(g, theme, 12, 7, 112, 38, "OS HOME");
  smallText(g, 146, 17, title(), theme.ink);
  drawStatusBar();
  g->fillRect(0, 50, 256, 4, theme.accent);
  g->fillRect(256, 50, 256, 4, theme.accent2);
  g->fillRect(512, 50, 256, 4, theme.success);
  g->fillRect(768, 50, 256, 4, theme.accent3);
  smallText(g, 28, 78, subtitle, theme.muted);
#else
  (void)subtitle;
#endif
}
void DeskUtilityApplication::drawStatusBar() {
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  if (context_ == nullptr) return;
  Arduino_GFX *g = CrowDisplay::canvas();
  if (g == nullptr) return;
  const DeskThemePalette &theme = deskTheme(context_->settings->theme());
  g->fillRect(400, 0, 624, 50, theme.shell);
  Preferences statusPrefs;
  bool alarmOn = false;
  if (statusPrefs.begin("cypher-desk", true)) {
    alarmOn = statusPrefs.getBool("alarm-on", false);
    statusPrefs.end();
  }
  smallText(g, 1004, 17, context_->time->timeText() + "  //  WIFI " +
            context_->wifi->stateLabel() + "  //  SD " + context_->storage->stateLabel() +
            "  //  REC " + (context_->audio->recording() ? "ON" : "OFF") +
            "  //  ALARM " + (alarmOn ? "ON" : "OFF"),
            theme.muted, Widgets::kRight);
#endif
}
void DeskUtilityApplication::drawInput() {
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  drawShell("SHARED TOUCH KEYBOARD");
  Arduino_GFX *g = CrowDisplay::canvas();
  const DeskThemePalette &theme = deskTheme(context_->settings->theme());
  osButton(g, theme, 18, 60, 120, 48, "CANCEL");
  osButton(g, theme, 866, 60, 140, 48, "SAVE", true);
  String label = (inputPurpose_ == kInputCalendarTitle || inputPurpose_ == kInputCalendarEditTitle) ? "EVENT TITLE" :
                 inputPurpose_ == kInputCalendarDate ? "EVENT DATE YYYY-MM-DD" :
                 inputPurpose_ == kInputCalendarTime ? "EVENT TIME HH:MM" :
                 inputPurpose_ == kInputCalendarNotes ? "EVENT NOTES" :
                 (inputPurpose_ == kInputContactName || inputPurpose_ == kInputContactEditName) ? "CONTACT NAME" :
                 inputPurpose_ == kInputContactOrganization ? "ORGANIZATION" :
                 inputPurpose_ == kInputContactPhone ? "PHONE" :
                 inputPurpose_ == kInputContactEmail ? "EMAIL" :
                 inputPurpose_ == kInputContactNotes ? "CONTACT NOTES" :
                 inputPurpose_ == kInputContactSearch ? "SEARCH CONTACTS" :
                 inputPurpose_ == kInputFileFolder ? "NEW FOLDER" :
                 inputPurpose_ == kInputFileRename ? "RENAME FILE" :
                 inputPurpose_ == kInputFileCopy ? "COPY AS" :
                 inputPurpose_ == kInputFileMove ? "MOVE TO PATH" :
                 inputPurpose_ == kInputHiddenSsid ? "HIDDEN NETWORK" : "WI-FI PASSWORD";
  smallText(g, 34, 144, label, theme.accent);
  String shown = input_;
  if (inputPurpose_ == kInputWifiPassword || inputPurpose_ == kInputHiddenPassword) {
    shown = ""; for (size_t i = 0; i < input_.length(); ++i) shown += '*';
  }
  Widgets::panel(g, 28, 166, 968, 94, 12, theme.panel, 2, theme.line);
  smallText(g, 48, 202, shown.length() ? shown : "tap keys below", shown.length() ? theme.ink : theme.muted);
  context_->keyboard->draw(g, theme);
#endif
}
void DeskUtilityApplication::drawToday() {
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  drawShell("YOUR CREATOR COMMAND DESK  //  " + context_->time->dateText());
  Arduino_GFX *g = CrowDisplay::canvas(); const DeskThemePalette &t = deskTheme(context_->settings->theme());
  osCard(g, t, 30, 122, 964, 366, t.accent);
  smallText(g, 52, 146, "QUICK START", t.accent);
  osButton(g, t, 30, 174, 300, 92, "DAILY PAGE", true);
  osButton(g, t, 362, 174, 300, 92, "TODAY'S CALENDAR");
  osButton(g, t, 694, 174, 300, 92, "VOICE NOTE");
  uint8_t todayEvents = 0;
  for (uint8_t i = 0; i < gCalendarCount; ++i) if (gCalendar[i].date == context_->time->dateText()) ++todayEvents;
  smallText(g, 52, 304, String(todayEvents) + " event" + (todayEvents == 1 ? "" : "s") + " today  //  " +
            String(context_->storage->countFiles(CYPHER_DESK_NOTES_DIR, ".md")) + " Markdown notes", t.ink);
  osButton(g, t, 30, 330, 300, 60, "QUICK SCRAP");
  String last = context_->settings->lastDocument();
  smallText(g, 362, 350, last.length() ? "LAST NOTE  //  " + last : "LAST NOTE  //  create a Daily Page or open Writer", t.muted);
  if (todayEvents) {
    for (uint8_t i = 0, row = 0; i < gCalendarCount && row < 2; ++i) {
      if (gCalendar[i].date == context_->time->dateText())
        smallText(g, 362, 392 + row++ * 32, gCalendar[i].time + "  //  " + gCalendar[i].title, t.accent);
    }
  } else smallText(g, 362, 394, "No events today. Add one locally in Calendar.", t.muted);
  smallText(g, 52, 454, context_->storage->status() + "  //  OFFLINE FIRST", t.success);
#endif
}
void DeskUtilityApplication::drawCalendar() {
  if (calendarListView_) drawCalendarList();
  else drawCalendarMonth();
}
void DeskUtilityApplication::drawCalendarMonth() {
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  initializeCalendarMonth();
  drawShell("LOCAL MONTH VIEW  //  EVENTS STAY ON SD");
  Arduino_GFX *g = CrowDisplay::canvas(); const DeskThemePalette &t = deskTheme(context_->settings->theme());
  osButton(g, t, 24, 94, 84, 40, "PREV");
  osButton(g, t, 118, 94, 84, 40, "NEXT");
  smallText(g, 236, 108, String(monthName(calendarMonth_)) + " " + String(calendarYear_), t.ink);
  osButton(g, t, 608, 94, 104, 40, "TODAY");
  osButton(g, t, 722, 94, 104, 40, "LIST");
  osButton(g, t, 836, 94, 164, 40, "ADD EVENT", true);
  const char *weekdays[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
  constexpr int16_t gridX = 20, gridY = 160, cellW = 140, cellH = 60;
  for (uint8_t col = 0; col < 7; ++col)
    smallText(g, gridX + col * cellW + cellW / 2, 144, weekdays[col], t.muted, Widgets::kCenter);
  uint8_t firstWeekday = weekdaySundayFirst(calendarYear_, calendarMonth_, 1);
  uint8_t monthDays = daysInMonth(calendarYear_, calendarMonth_);
  uint8_t previousMonth = calendarMonth_ == 1 ? 12 : calendarMonth_ - 1;
  int16_t previousYear = calendarMonth_ == 1 ? calendarYear_ - 1 : calendarYear_;
  uint8_t previousDays = daysInMonth(previousYear, previousMonth);
  String today = context_->time->dateText();
  for (uint8_t cell = 0; cell < 42; ++cell) {
    uint8_t row = cell / 7, col = cell % 7;
    int16_t x = gridX + col * cellW, y = gridY + row * cellH;
    int16_t day = static_cast<int16_t>(cell) - firstWeekday + 1;
    bool current = day >= 1 && day <= monthDays;
    int16_t dateYear = calendarYear_; uint8_t dateMonth = calendarMonth_; uint8_t dateDay = 1;
    if (current) dateDay = static_cast<uint8_t>(day);
    else if (day < 1) { dateYear = previousYear; dateMonth = previousMonth; dateDay = previousDays + day; }
    else { dateMonth = calendarMonth_ == 12 ? 1 : calendarMonth_ + 1; dateYear = calendarMonth_ == 12 ? calendarYear_ + 1 : calendarYear_; dateDay = day - monthDays; }
    String date = isoDate(dateYear, dateMonth, dateDay);
    bool selected = date == pendingCalendarDate_;
    bool isToday = date == today;
    Widgets::panel(g, x, y, cellW, cellH, 0, current ? t.panel : t.background,
                   selected ? 2 : 1, selected ? t.accent2 : t.line);
    if (isToday) g->fillCircle(x + cellW - 18, y + 15, 12, t.accent3);
    smallText(g, x + cellW - 10, y + 7, String(dateDay), isToday ? t.background : (current ? t.ink : t.muted), Widgets::kRight);
    if (!current) continue;
    uint8_t eventRow = 0, extra = 0;
    for (uint8_t i = 0; i < gCalendarCount; ++i) {
      if (gCalendar[i].date != date) continue;
      if (eventRow >= 2) { ++extra; continue; }
      int16_t chipY = y + 27 + eventRow * 15;
      g->fillRoundRect(x + 6, chipY, cellW - 12, 13, 4, eventRow == 0 ? t.accent : t.accent2);
      String title = gCalendar[i].title;
      if (title.length() > 18) title = title.substring(0, 17) + ".";
      smallText(g, x + 10, chipY + 1, title, t.background);
      ++eventRow;
    }
    if (extra) smallText(g, x + 10, y + 47, "+" + String(extra) + " more", t.warning);
  }
  uint8_t selectedEvents = 0;
  for (uint8_t i = 0; i < gCalendarCount; ++i) if (gCalendar[i].date == pendingCalendarDate_) ++selectedEvents;
  smallText(g, 24, 536, pendingCalendarDate_ + "  //  " + String(selectedEvents) +
            " event" + (selectedEvents == 1 ? "" : "s") + "  //  tap LIST to edit", t.muted);
#endif
}
void DeskUtilityApplication::drawCalendarList() {
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  drawShell("EVENT EDITOR  //  ATOMIC SCHEMA 1 STORAGE");
  Arduino_GFX *g = CrowDisplay::canvas(); const DeskThemePalette &t = deskTheme(context_->settings->theme());
  osButton(g, t, 572, 72, 186, 48, "MONTH VIEW");
  osButton(g, t, 790, 72, 202, 48, "ADD EVENT", true);
  for (uint8_t row = 0; row < min(static_cast<uint8_t>(6), gCalendarCount); ++row) {
    DeskCalendarEvent &event = gCalendar[row];
    osButton(g, t, 28, 142 + row * 58, 720, 50,
             event.date + "  " + event.time + "  //  " + event.title, row == selected_);
  }
  if (!gCalendarCount) smallText(g, 40, 168, "No events yet. Add one without needing the cloud.", t.muted);
  if (gCalendarCount) {
    DeskCalendarEvent &event = gCalendar[selected_];
    smallText(g, 790, 124, "SELECTED EVENT", t.accent);
    osButton(g, t, 790, 142, 202, 44, "TITLE  //  " + event.title);
    osButton(g, t, 790, 198, 202, 44, "DATE  //  " + event.date);
    osButton(g, t, 790, 254, 202, 44, "TIME  //  " + event.time);
    osButton(g, t, 790, 310, 202, 44, event.notes.length() ? "EDIT NOTES" : "ADD NOTES");
    osButton(g, t, 790, 366, 202, 44, event.alarm ? "ALARM  //  ON" : "ALARM  //  OFF", event.alarm);
    osButton(g, t, 790, 422, 202, 44, "DELETE EVENT");
  }
#endif
}
void DeskUtilityApplication::drawContacts() {
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  drawShell("LOCAL PEOPLE AND BUSINESS CONTACTS");
  Arduino_GFX *g = CrowDisplay::canvas(); const DeskThemePalette &t = deskTheme(context_->settings->theme());
  osButton(g, t, 570, 72, 188, 48, contactQuery_.length() ? "SEARCH  //  " + contactQuery_ : "SEARCH");
  osButton(g, t, 790, 72, 202, 48, "ADD CONTACT", true);
  for (uint8_t row = 0; row < min(static_cast<uint8_t>(6), gContactCount); ++row) {
    if (!selectedContactMatches(row)) continue;
    DeskContactRecord &contact = gContacts[row];
    String detail = contact.organization.length() ? "  //  " + contact.organization : "";
    osButton(g, t, 28, 142 + row * 58, 520, 50, contact.name + detail, row == selected_);
  }
  if (!gContactCount) smallText(g, 40, 168, "No contacts yet. Add a person or business.", t.muted);
  else {
    DeskContactRecord &contact = gContacts[selected_];
    smallText(g, 790, 124, "SELECTED CONTACT", t.accent);
    osButton(g, t, 790, 142, 202, 44, "NAME  //  " + contact.name);
    osButton(g, t, 790, 198, 202, 44, contact.organization.length() ? "EDIT ORGANIZATION" : "ADD ORGANIZATION");
    osButton(g, t, 790, 254, 202, 44, contact.phone.length() ? "EDIT PHONE" : "ADD PHONE");
    osButton(g, t, 790, 310, 202, 44, contact.email.length() ? "EDIT EMAIL" : "ADD EMAIL");
    osButton(g, t, 790, 366, 202, 44, contact.notes.length() ? "EDIT NOTES" : "ADD NOTES");
    osButton(g, t, 790, 422, 202, 44, "DELETE CONTACT");
    smallText(g, 570, 196, contact.organization, t.muted);
    smallText(g, 570, 230, contact.phone + "  " + contact.email, t.muted);
  }
#endif
}
void DeskUtilityApplication::drawClock() {
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  drawShell("CLOCK  //  STOPWATCH  //  TIMER  //  LOCAL ALARM");
  Arduino_GFX *g = CrowDisplay::canvas(); const DeskThemePalette &t = deskTheme(context_->settings->theme());
  Widgets::text(g, 512, 90, context_->time->timeText().c_str(), Widgets::fontXL(), t.ink, Widgets::kCenter);
  uint32_t sw = stopwatchElapsedMs_ + (stopwatchRunning_ ? millis() - stopwatchStartedMs_ : 0);
  osButton(g, t, 30, 190, 300, 74, String(stopwatchRunning_ ? "STOP " : "START ") + "STOPWATCH  " + String(sw / 1000) + "s", stopwatchRunning_);
  osButton(g, t, 362, 190, 300, 74, String(timerRunning_ ? "CANCEL " : "START ") + String(timerMinutes_) + " MIN TIMER", timerRunning_);
  osButton(g, t, 694, 190, 140, 74, "+5 MIN"); osButton(g, t, 850, 190, 140, 74, "-5 MIN");
  osButton(g, t, 30, 316, 300, 74, String("ALARM 08:00  //  ") + (alarmEnabled_ ? "ON" : "OFF"), alarmEnabled_);
  smallText(g, 362, 340, "Alarm preference persists in the existing cypher-desk NVS namespace.", t.muted);
#endif
}
void DeskUtilityApplication::drawClockDynamic() {
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  if (context_ == nullptr) return;
  Arduino_GFX *g = CrowDisplay::canvas();
  if (g == nullptr) return;
  const DeskThemePalette &t = deskTheme(context_->settings->theme());
  g->fillRect(330, 58, 364, 82, t.background);
  Widgets::text(g, 512, 90, context_->time->timeText().c_str(), Widgets::fontXL(), t.ink, Widgets::kCenter);
  g->fillRect(30, 190, 300, 74, t.background);
  uint32_t sw = stopwatchElapsedMs_ + (stopwatchRunning_ ? millis() - stopwatchStartedMs_ : 0);
  osButton(g, t, 30, 190, 300, 74,
           String(stopwatchRunning_ ? "STOP " : "START ") + "STOPWATCH  " + String(sw / 1000) + "s",
           stopwatchRunning_);
#endif
}
void DeskUtilityApplication::drawCalculator() {
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  drawShell("OFFLINE FOUR-FUNCTION CALCULATOR");
  Arduino_GFX *g = CrowDisplay::canvas(); const DeskThemePalette &t = deskTheme(context_->settings->theme());
  Widgets::panel(g, 174, 88, 664, 50, 10, t.shell, 2, t.line);
  smallText(g, 816, 105, calcDisplay_, t.ink, Widgets::kRight);
  const char *keys[5][4] = {{"C", "+/-", "%", "/"}, {"7", "8", "9", "*"},
                            {"4", "5", "6", "-"}, {"1", "2", "3", "+"},
                            {"0", ".", "=", "="}};
  for (uint8_t row = 0; row < 5; ++row) for (uint8_t col = 0; col < 4; ++col)
    osButton(g, t, 174 + col * 170, 146 + row * 78, 154, 64, keys[row][col], col == 3 || keys[row][col][0] == '=');
#endif
}
void DeskUtilityApplication::drawFiles() {
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  drawShell("SD WORKSPACE  //  SAFE LOCAL FILE OPERATIONS");
  Arduino_GFX *g = CrowDisplay::canvas(); const DeskThemePalette &t = deskTheme(context_->settings->theme());
  osButton(g, t, 30, 92, 118, 44, "UP");
  osButton(g, t, 164, 92, 160, 44, "NEW FOLDER", true);
  smallText(g, 344, 106, fileDirectory_, t.accent);
  for (uint8_t i = 0; i < min(static_cast<uint8_t>(6), fileEntryCount_); ++i) {
    const DeskStorageService::FileEntry &entry = fileEntries_[i];
    osButton(g, t, 30, 150 + i * 64, 700, 54,
             String(entry.directory ? "[DIR]  " : "[FILE]  ") + entry.name +
             (entry.directory ? "" : "  //  " + String(entry.size) + " B"), i == selected_);
  }
  if (!fileEntryCount_) smallText(g, 40, 170, "Folder empty. Create a folder or copy files onto the SD card.", t.muted);
  osCard(g, t, 754, 120, 238, 292, t.accent2);
  smallText(g, 873, 136, String(context_->storage->stateLabel()) + "  //  " + String(context_->storage->freePercent()) + "% FREE", t.success, Widgets::kCenter);
  osButton(g, t, 754, 132, 238, 44, "OPEN TEXT");
  osButton(g, t, 754, 188, 238, 44, "RENAME");
  osButton(g, t, 754, 244, 238, 44, "COPY");
  osButton(g, t, 754, 300, 238, 44, "MOVE");
  osButton(g, t, 754, 356, 238, 44, "DELETE");
  osButton(g, t, 774, 430, 218, 58, "SAFE EJECT");
  osButton(g, t, 774, 500, 218, 58, "REMOUNT");
  if (filePreview_.length()) {
    osCard(g, t, 66, 170, 650, 286, t.warning);
    smallText(g, 88, 194, "TEXT PREVIEW  //  BACK TO CLOSE", t.warning);
    smallText(g, 88, 230, filePreview_, t.ink);
  }
#endif
}
void DeskUtilityApplication::drawSettings() {
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  drawShell("THEME  //  HOSTED-C6 WI-FI  //  OFFLINE MODE");
  Arduino_GFX *g = CrowDisplay::canvas(); const DeskThemePalette &t = deskTheme(context_->settings->theme());
  osButton(g, t, 30, 150, 300, 64, "THEME  //  " + String(t.name), true);
  osButton(g, t, 362, 150, 300, 64, "SCAN NETWORKS");
  osButton(g, t, 694, 150, 300, 64, context_->wifi->offline() ? "LEAVE OFFLINE MODE" : "ENTER OFFLINE MODE", context_->wifi->offline());
  osButton(g, t, 30, 232, 300, 54, "HIDDEN NETWORK");
  osButton(g, t, 362, 232, 300, 54, diagnosticsVisible_ ? "HIDE DIAGNOSTICS" : "DIAGNOSTICS");
  drawSettingsDynamic();
  smallText(g, 700, 316, "Credentials are NVS-stored, masked, and never logged.", t.muted);
  smallText(g, 700, 350, "Ordinary NVS is not described as encrypted.", t.warning);
  if (diagnosticsVisible_) {
    osCard(g, t, 674, 382, 318, 174, t.success);
    smallText(g, 694, 404, "REAL PROOF STATE", t.success);
    smallText(g, 694, 434, String("DISPLAY ") + (context_->storage->mounted() ? "RUNNING" : "RUNNING / NO SD"), t.ink);
    smallText(g, 694, 462, String("SD  ") + context_->storage->status(), t.ink);
    smallText(g, 694, 490, String("C6  ") + context_->wifi->stateLabel() + "  //  TIME " + (context_->time->synced() ? "SYNCED" : "PENDING"), t.ink);
    smallText(g, 694, 518, "SPK " + String(context_->audio->speakerAvailable() ? "READY" : "UNPROVEN") +
              "  //  MIC " + String(context_->audio->microphoneAvailable() ? "READY" : "UNPROVEN"), t.warning);
  }
  settingsSnapshot_ = settingsSnapshot();
#endif
}
String DeskUtilityApplication::settingsSnapshot() const {
  if (context_ == nullptr || context_->wifi == nullptr) return "";
  return String(context_->wifi->state()) + "|" + context_->wifi->status() + "|" +
         context_->wifi->networkCount() + "|" + context_->wifi->savedCount() + "|" +
         (context_->wifi->offline() ? "1" : "0");
}
String DeskUtilityApplication::weatherSnapshot() const {
  if (context_ == nullptr || context_->weather == nullptr) return "";
  const DeskWeatherData &weather = context_->weather->data();
  return context_->weather->status() + "|" + context_->weather->locationLabel() + "|" +
         String(context_->weather->valid() ? 1 : 0) + "|" + String(context_->weather->cached() ? 1 : 0) + "|" +
         String(weather.tempC, 1) + "|" + weather.condition;
}
void DeskUtilityApplication::drawSettingsDynamic() {
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  if (context_ == nullptr) return;
  Arduino_GFX *g = CrowDisplay::canvas();
  if (g == nullptr) return;
  const DeskThemePalette &t = deskTheme(context_->settings->theme());
  g->fillRect(350, 230, 320, 40, t.background);
  g->fillRect(20, 280, 650, 276, t.background);
  smallText(g, 362, 248, context_->wifi->status(), t.muted);
  bool visible = context_->wifi->state() == kDeskWifiNetworksFound;
  uint8_t count = visible ? context_->wifi->networkCount() : context_->wifi->savedCount();
  smallText(g, 30, 292, visible ? "VISIBLE NETWORKS" : "SAVED NETWORKS", t.accent);
  for (uint8_t row = 0; row < min(static_cast<uint8_t>(5), count); ++row) {
    String name = visible ? context_->wifi->network(row).ssid : context_->wifi->savedSsid(row);
    if (visible && context_->wifi->network(row).secured) name += "  //  LOCKED";
    osButton(g, t, 30, 306 + row * 50, 640, 44, name);
  }
  settingsSnapshot_ = settingsSnapshot();
#endif
}
void DeskUtilityApplication::drawRecorder() {
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  drawShell("VOICE CAPTURE  //  SD-BACKED AND HARDWARE-GUARDED");
  Arduino_GFX *g = CrowDisplay::canvas(); const DeskThemePalette &t = deskTheme(context_->settings->theme());
  osCard(g, t, 120, 126, 784, 360, t.warning);
  smallText(g, 512, 170, context_->audio->microphoneAvailable() ? "PDM MICROPHONE READY" : "MICROPHONE UNAVAILABLE", context_->audio->microphoneAvailable() ? t.success : t.warning, Widgets::kCenter);
  smallText(g, 512, 212, String("Speaker: ") + (context_->audio->speakerAvailable() ? "ready" : "unavailable") + "   Mic: " + (context_->audio->microphoneAvailable() ? "ready" : "unavailable"), t.ink, Widgets::kCenter);
  smallText(g, 512, 250, "Mic pins: PDM CLK GPIO24 / DATA GPIO26 / 16 kHz mono PCM WAV", t.muted, Widgets::kCenter);
  drawRecorderDynamic();
  osButton(g, t, 180, 392, 300, 58, "PLAY SPEAKER TEST");
  osButton(g, t, 512, 392, 332, 58, context_->audio->recording() ? "STOP AND SAVE" : "START RECORDING", context_->audio->recording());
  osButton(g, t, 350, 466, 324, 54, context_->audio->recordingPath().length() ? "PLAY LAST RECORDING" : "NO RECORDING YET");
#endif
}
void DeskUtilityApplication::drawRecorderDynamic() {
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  if (context_ == nullptr) return;
  Arduino_GFX *g = CrowDisplay::canvas();
  if (g == nullptr) return;
  const DeskThemePalette &t = deskTheme(context_->settings->theme());
  g->fillRect(150, 264, 724, 44, t.panel);
  smallText(g, 512, 286, String("Level: ") + context_->audio->inputLevel() + "   //   " +
            context_->audio->testStatus() + (context_->audio->recording() ? "  //  " +
            String(context_->audio->recordingDurationMs() / 1000) + "s" : ""), t.accent, Widgets::kCenter);
#endif
}
void DeskUtilityApplication::drawMusic() {
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  drawShell("LOCAL PCM WAV LIBRARY  //  SD-BACKED PLAYBACK");
  Arduino_GFX *g = CrowDisplay::canvas(); const DeskThemePalette &t = deskTheme(context_->settings->theme());
  osCard(g, t, 60, 122, 904, 356, t.accent2);
  smallText(g, 82, 148, String(context_->storage->countFiles(CYPHER_DESK_MUSIC_DIR, ".wav")) + " WAV FILES  //  16 kHz MONO PCM ONLY", t.accent);
  for (uint8_t row = 0; row < min(static_cast<uint8_t>(5), fileEntryCount_); ++row)
    osButton(g, t, 80, 166 + row * 58, 864, 48, fileEntries_[row].name,
             context_->audio->playing() && context_->audio->playbackPath() == fileEntries_[row].path);
  if (!fileEntryCount_) smallText(g, 512, 250, "Copy compatible WAV files to /cypher-puter/desk/music/", t.muted, Widgets::kCenter);
  smallText(g, 82, 456, context_->audio->status() + "  //  " + context_->audio->testStatus(), t.warning);
  osButton(g, t, 760, 480, 184, 52, "STOP");
#endif
}
void DeskUtilityApplication::drawPodcasts() {
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  drawShell("LOCAL PODCAST FOLDER  //  SD ONLY, NO RSS OR DOWNLOADS");
  Arduino_GFX *g = CrowDisplay::canvas(); const DeskThemePalette &t = deskTheme(context_->settings->theme());
  osCard(g, t, 60, 122, 904, 356, t.accent3);
  smallText(g, 82, 148, String(context_->storage->countFiles(CYPHER_DESK_PODCASTS_DIR)) + " LOCAL FILES  //  WAV PLAYBACK WHEN HARDWARE IS READY", t.accent);
  for (uint8_t row = 0; row < min(static_cast<uint8_t>(5), fileEntryCount_); ++row)
    osButton(g, t, 80, 166 + row * 58, 864, 48, fileEntries_[row].name,
             context_->audio->playing() && context_->audio->playbackPath() == fileEntries_[row].path);
  if (!fileEntryCount_) smallText(g, 512, 250, "Copy local WAV files to /cypher-puter/desk/podcasts/", t.muted, Widgets::kCenter);
  smallText(g, 82, 456, "RSS, downloading, cloud sync, and simulated playback are outside this release.", t.warning);
  osButton(g, t, 760, 480, 184, 52, "STOP");
#endif
}
void DeskUtilityApplication::drawWeather() {
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  drawShell("OPEN-METEO FORECAST  //  USER-CONFIGURED LOCATION");
  Arduino_GFX *g = CrowDisplay::canvas(); const DeskThemePalette &t = deskTheme(context_->settings->theme());
  if (context_->weather == nullptr || !context_->weather->configured()) {
    osCard(g, t, 160, 155, 704, 270, t.warning);
    smallText(g, 512, 214, "SET YOUR LOCATION BEFORE REQUESTING WEATHER", t.ink, Widgets::kCenter);
    smallText(g, 512, 278, "Serial: weather location <latitude> <longitude> [label]", t.accent, Widgets::kCenter);
    smallText(g, 512, 334, "Example: weather location 34.0522 -118.2437 Los Angeles", t.muted, Widgets::kCenter);
    smallText(g, 512, 390, "Coordinates are stored locally. No location is assumed by this device.", t.muted, Widgets::kCenter);
    weatherSnapshot_ = weatherSnapshot();
    return;
  }
  const DeskWeatherData &weather = context_->weather->data();
  osCard(g, t, 74, 132, 876, 326, t.accent);
  smallText(g, 512, 170, context_->weather->locationLabel(), t.accent, Widgets::kCenter);
  if (context_->weather->valid()) {
    smallText(g, 300, 260, String(weather.tempC, 1) + " C", t.ink, Widgets::kCenter);
    smallText(g, 300, 310, weather.condition, t.muted, Widgets::kCenter);
    smallText(g, 668, 225, "FEELS  " + String(weather.feelsC, 1) + " C", t.ink, Widgets::kCenter);
    smallText(g, 668, 272, "HIGH / LOW  " + String(weather.hiC, 1) + " / " + String(weather.loC, 1) + " C", t.ink, Widgets::kCenter);
    smallText(g, 668, 319, "WIND  " + String(weather.windKt, 1) + " kt", t.ink, Widgets::kCenter);
    if (context_->weather->cached()) smallText(g, 512, 362, "CACHED RESULT  //  REFRESH FOR LIVE DATA", t.warning, Widgets::kCenter);
  } else {
    smallText(g, 512, 270, "NO WEATHER RESULT YET", t.ink, Widgets::kCenter);
  }
  smallText(g, 512, 408, context_->weather->status(), t.muted, Widgets::kCenter);
  osButton(g, t, 360, 484, 304, 58, "REFRESH WEATHER");
  smallText(g, 512, 566, "Requires verified internet. HTTPS certificate validation is not yet device-proven.", t.warning, Widgets::kCenter);
  weatherSnapshot_ = weatherSnapshot();
#endif
}
void DeskUtilityApplication::drawDeleteConfirm() {
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  Arduino_GFX *g = CrowDisplay::canvas();
  if (g == nullptr) return;
  const DeskThemePalette &t = deskTheme(context_->settings->theme());
  Widgets::panel(g, 260, 256, 504, 176, 14, t.shell, 3, t.warning);
  String subject = deleteKind_ == kDeleteCalendar && selected_ < gCalendarCount ?
      gCalendar[selected_].date + "  //  " + gCalendar[selected_].title :
      deleteKind_ == kDeleteContact && selected_ < gContactCount ? gContacts[selected_].name : pendingFilePath_;
  smallText(g, 512, 286, deleteKind_ == kDeleteFile ? "DELETE THIS FILE?" : "DELETE THIS LOCAL RECORD?", t.warning, Widgets::kCenter);
  smallText(g, 512, 322, subject, t.ink, Widgets::kCenter);
  smallText(g, 512, 342, deleteKind_ == kDeleteFile ? "Protected system folders cannot be removed here." : "This change is saved atomically to SD.", t.muted, Widgets::kCenter);
  osButton(g, t, 318, 352, 178, 60, "CANCEL");
  osButton(g, t, 528, 352, 178, 60, "DELETE", true);
#endif
}
void DeskUtilityApplication::draw() {
  if (inputPurpose_ != kInputNone) { drawInput(); return; }
  switch (appId_) {
    case kDeskAppToday: drawToday(); break;
    case kDeskAppCalendar: drawCalendar(); break;
    case kDeskAppContacts: drawContacts(); break;
    case kDeskAppClock: drawClock(); break;
    case kDeskAppCalculator: drawCalculator(); break;
    case kDeskAppFiles: drawFiles(); break;
    case kDeskAppSettings: drawSettings(); break;
    case kDeskAppRecorder: drawRecorder(); break;
    case kDeskAppMusic: drawMusic(); break;
    case kDeskAppPodcasts: drawPodcasts(); break;
    case kDeskAppWeather: drawWeather(); break;
    default: break;
  }
  if (confirmingDelete_) drawDeleteConfirm();
  dirty_ = false;
}

void DeskUtilityApplication::handleSerial(const String &commandValue, Print &out) {
  String command = commandValue;
  command.trim();
  if (appId_ == kDeskAppCalculator) {
    pressCalculator(command);
    out.print(F("[calculator] display=")); out.println(calcDisplay_);
  } else if (appId_ == kDeskAppCalendar) {
    if (command.startsWith("add ") && gCalendarCount < kMaxCalendarEvents) {
      String rest = command.substring(4);
      int first = rest.indexOf(' '); int second = first < 0 ? -1 : rest.indexOf(' ', first + 1);
      if (first > 0 && second > first) {
        gCalendar[gCalendarCount++] = {rest.substring(0, first), rest.substring(first + 1, second), rest.substring(second + 1), "", false};
        saveCalendar(context_->storage);
      }
    } else if (command.startsWith("edit ")) {
      String rest = command.substring(5); int split = rest.indexOf(' ');
      int index = split > 0 ? rest.substring(0, split).toInt() - 1 : -1;
      rest = split > 0 ? rest.substring(split + 1) : "";
      int first = rest.indexOf(' '); int second = first < 0 ? -1 : rest.indexOf(' ', first + 1);
      if (index >= 0 && index < gCalendarCount && first > 0 && second > first) {
        gCalendar[index].date = rest.substring(0, first);
        gCalendar[index].time = rest.substring(first + 1, second);
        gCalendar[index].title = rest.substring(second + 1);
        saveCalendar(context_->storage);
      }
    } else if (command.startsWith("note ")) {
      String rest = command.substring(5); int split = rest.indexOf(' ');
      int index = split > 0 ? rest.substring(0, split).toInt() - 1 : -1;
      if (index >= 0 && index < gCalendarCount) { gCalendar[index].notes = rest.substring(split + 1); saveCalendar(context_->storage); }
    } else if (command.startsWith("alarm ")) {
      String rest = command.substring(6); int split = rest.indexOf(' ');
      int index = split > 0 ? rest.substring(0, split).toInt() - 1 : -1;
      if (index >= 0 && index < gCalendarCount) { gCalendar[index].alarm = rest.substring(split + 1) == "on"; saveCalendar(context_->storage); }
    } else if (command.startsWith("delete ")) {
      int index = command.substring(7).toInt() - 1;
      if (index >= 0 && index < gCalendarCount) { for (uint8_t i = index; i + 1 < gCalendarCount; ++i) gCalendar[i] = gCalendar[i + 1]; --gCalendarCount; saveCalendar(context_->storage); }
    }
    for (uint8_t i = 0; i < gCalendarCount; ++i) { out.print(i + 1); out.print(F(". ")); out.print(gCalendar[i].date); out.print(' '); out.print(gCalendar[i].time); out.print(F(" // ")); out.println(gCalendar[i].title); }
  } else if (appId_ == kDeskAppContacts) {
    if (command.startsWith("add ") && gContactCount < kMaxContacts) { gContacts[gContactCount++] = {command.substring(4), "", "", "", ""}; saveContacts(context_->storage); }
    else if (command.startsWith("set ")) {
      String rest = command.substring(4); int first = rest.indexOf(' '); int second = first < 0 ? -1 : rest.indexOf(' ', first + 1);
      int index = first > 0 ? rest.substring(0, first).toInt() - 1 : -1;
      if (index >= 0 && index < gContactCount && second > first) {
        String field = rest.substring(first + 1, second); String value = rest.substring(second + 1);
        if (field == "name") gContacts[index].name = value;
        else if (field == "org") gContacts[index].organization = value;
        else if (field == "phone") gContacts[index].phone = value;
        else if (field == "email") gContacts[index].email = value;
        else if (field == "notes") gContacts[index].notes = value;
        saveContacts(context_->storage);
      }
    }
    else if (command.startsWith("delete ")) { int index = command.substring(7).toInt() - 1; if (index >= 0 && index < gContactCount) { for (uint8_t i = index; i + 1 < gContactCount; ++i) gContacts[i] = gContacts[i + 1]; --gContactCount; saveContacts(context_->storage); } }
    for (uint8_t i = 0; i < gContactCount; ++i) { out.print(i + 1); out.print(F(". ")); out.println(gContacts[i].name); }
  } else if (appId_ == kDeskAppFiles) {
    if (command == "eject") context_->storage->safeEject();
    else if (command == "remount") context_->storage->remount();
    else if (command.startsWith("list ")) { String names[24]; uint8_t count = context_->storage->listFileNames(command.substring(5), names, 24); for (uint8_t i = 0; i < count; ++i) out.println(names[i]); }
    else if (command.startsWith("read ")) out.println(context_->storage->readText(command.substring(5), 12000));
    else if (command.startsWith("mkdir ")) context_->storage->ensureDirectory(command.substring(6));
    else if (command.startsWith("delete ")) context_->storage->removeFile(command.substring(7));
    else if (command.startsWith("rename ")) { String rest = command.substring(7); int split = rest.indexOf(' '); if (split > 0) context_->storage->renamePath(rest.substring(0, split), rest.substring(split + 1)); }
    else if (command.startsWith("move ")) { String rest = command.substring(5); int split = rest.indexOf(' '); if (split > 0) context_->storage->moveFile(rest.substring(0, split), rest.substring(split + 1)); }
    else if (command.startsWith("copy ")) { String rest = command.substring(5); int split = rest.indexOf(' '); if (split > 0) context_->storage->copyFile(rest.substring(0, split), rest.substring(split + 1)); }
    context_->storage->print(out);
  } else if (appId_ == kDeskAppClock) {
    if (command == "alarm on") alarmEnabled_ = true;
    else if (command == "alarm off") alarmEnabled_ = false;
    else if (command.startsWith("timer ")) timerMinutes_ = constrain(command.substring(6).toInt(), 1, 120);
    saveClockSettings();
    out.print(F("[clock] alarm=")); out.print(alarmEnabled_ ? "on" : "off"); out.print(F(" timer=")); out.println(timerMinutes_);
  }
  dirty_ = true;
}
