#ifndef CYPHER_DESK_PANEL_TYPES_H
#define CYPHER_DESK_PANEL_TYPES_H

#include <Arduino.h>

constexpr uint16_t CYPHER_DESK_PAGE_SIZE = 48;
constexpr uint16_t CYPHER_DESK_MAX_NOTES = CYPHER_DESK_PAGE_SIZE;
constexpr uint8_t CYPHER_DESK_MAX_FOLDERS = 24;
constexpr uint8_t CYPHER_DESK_MAX_MOCK_NOTES = 20;
// 12,000 was a Cardputer-era limit. The ceiling here is not flash but internal
// RAM: the editor holds the document in an Arduino String, and the save path
// holds a second copy while it writes. 32,000 is comfortable against ~250 KB of
// free internal heap; going materially past it wants a PSRAM-backed buffer
// type instead of String, which is a larger change than raising a constant.
constexpr size_t CYPHER_DESK_MAX_EDITOR_LEN = 32000;

enum DeskPage {
  kDeskPageDesk,
  kDeskPageNotebooks,
  kDeskPageScrap,
  kDeskPageFocus,
  kDeskPageRitual,
  kDeskPageArchive,
  kDeskPageSettings,
  kDeskPageEditor,
  kDeskPageMessage,
  kDeskPageTextInput,
  kDeskPageConfirm
};

using DeskScreen = DeskPage;
constexpr DeskPage kDeskHome = kDeskPageDesk;
constexpr DeskPage kDeskFolder = kDeskPageNotebooks;
constexpr DeskPage kDeskEditor = kDeskPageEditor;
constexpr DeskPage kDeskMessage = kDeskPageMessage;

// The five original writing palettes, plus the six ported from project 21.
// Two of the ported ones are LIGHT themes, which only work because of the
// onAccent role below.
enum DeskThemeId {
  kDeskThemeMidnightPlum,
  kDeskThemeMatchaTerminal,
  kDeskThemeDustyRose,
  kDeskThemeRainyBlue,
  kDeskThemePaperback,
  kDeskThemeOpsTeal,
  kDeskThemeAmberCrt,
  kDeskThemeSynthwave,
  kDeskThemeMatrix,
  kDeskThemePastel,
  kDeskThemeCottonCandy,
  kDeskThemeCount
};

struct DeskThemePalette {
  const char *name;
  uint16_t background;
  uint16_t shell;
  uint16_t panel;
  uint16_t panelHighlight;
  uint16_t ink;
  uint16_t muted;
  uint16_t accent;
  uint16_t accent2;
  uint16_t accent3;
  uint16_t success;
  uint16_t warning;
  uint16_t line;
  // Text drawn ON TOP of an accent/success/warning fill. Without this a light
  // theme is unreadable the moment anything is highlighted, because the ink
  // colour is already dark.
  uint16_t onAccent;
  uint16_t keyboardRows[4];
  // True for palettes whose background is lighter than their ink, so chrome
  // that picks a shadow or divider colour can do the right thing.
  bool light;
};

struct DeskDocument {
  String path;
  String title;
  String extension;
};

struct DeskFolder {
  String path;
  String name;
  String title;
};

struct DeskNoteMetadata {
  String path;
  bool favorite = false;
  bool finished = false;
  uint8_t notebookColor = 0;
  uint32_t lastOpened = 0;
  uint32_t lastEdited = 0;
};

struct DeskSession {
  String date;
  uint16_t durationMinutes = 0;
  int32_t wordDelta = 0;
  uint32_t startingWords = 0;
  uint32_t endingWords = 0;
  String documentPath;
  bool completed = false;
};

struct DeskStats {
  uint32_t quietMinutesToday = 0;
  uint32_t quietMinutesWeek = 0;
  int32_t wordsAddedWeek = 0;
  uint16_t sessionsCompleted = 0;
  String lastSession;
};

enum DeskArchiveFilter {
  kDeskArchiveRecent,
  kDeskArchiveFavorites,
  kDeskArchiveFinished,
  kDeskArchiveDaily,
  kDeskArchiveScraps
};

enum DeskInputPurpose {
  kDeskInputNone,
  kDeskInputNotebook,
  kDeskInputNoteName,
  kDeskInputRename,
  kDeskInputSearch,
  kDeskInputWifiSsid,
  kDeskInputWifiPassword
};

struct DeskEditorState {
  DeskDocument document;
  String buffer;
  size_t cursor = 0;
  uint16_t topLine = 0;
  uint16_t leftColumn = 0;
  uint16_t preferredColumn = 0;
  bool dirty = false;
  uint32_t lastEditMs = 0;
  uint32_t lastSaveMs = 0;
};

#endif
