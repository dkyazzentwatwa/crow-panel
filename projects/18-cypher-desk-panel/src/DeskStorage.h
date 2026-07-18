#ifndef CYPHER_DESK_PANEL_STORAGE_H
#define CYPHER_DESK_PANEL_STORAGE_H

#include "../config/ProjectConfig.h"
#include "DeskTypes.h"

class DeskStorage {
 public:
  void begin();
  bool persistent() const;
  bool ejected() const;
  const String &status() const;
  const char *sourceLabel() const;

  uint16_t listNotes(const String &directory, DeskDocument *out, uint16_t maxCount) const;
  uint16_t listNotesPage(const String &directory, uint16_t offset, DeskDocument *out,
                         uint16_t maxCount, bool *hasMore = nullptr) const;
  uint16_t queryArchive(DeskArchiveFilter filter, uint16_t offset, DeskDocument *out,
                        uint16_t maxCount, bool *hasMore = nullptr) const;
  uint8_t listFolders(DeskFolder *out, uint8_t maxCount) const;
  String createFolder();
  String createNamedFolder(const String &name);
  String nextNotePath(const String &directory, const char *extension) const;
  String namedNotePath(const String &directory, const String &name, const char *extension) const;
  String dailyPath(const String &isoDate) const;
  String createScrapPath() const;
  String readTextFile(const String &path, size_t maxLen = CYPHER_DESK_MAX_EDITOR_LEN) const;
  bool saveTextFile(const String &path, const String &body);
  bool exists(const String &path) const;
  bool removeNote(const String &path);
  bool renameNote(const String &path, const String &newName, String &newPath);
  bool moveNote(const String &path, const String &newDirectory, String &newPath,
                bool overwrite = false);
  bool exportCopy(const String &path, const String &isoDate, String &exportPath);
  bool safeEject();

  DeskNoteMetadata metadataFor(const String &path) const;
  bool setMetadata(const DeskNoteMetadata &metadata);
  bool toggleFavorite(const String &path);
  bool toggleFinished(const String &path);
  bool setNotebookColor(const String &path, uint8_t color);
  bool rebuildMetadata();

  bool logSession(const DeskSession &session);
  DeskStats stats(const String &today) const;

  String basename(const String &path) const;
  String extensionFromPath(const String &path) const;
  String titleFromPath(const String &path) const;
  String titleFromFolderName(const String &name) const;
  String slugify(const String &name) const;

 private:
  static constexpr uint16_t kMaxMetadata = 160;
  bool persistent_ = false;
  bool ejected_ = false;
  bool metadataDirty_ = false;
  String status_ = "RAM demo ready (nonpersistent)";
  DeskDocument mockNotes_[CYPHER_DESK_MAX_MOCK_NOTES];
  String mockBodies_[CYPHER_DESK_MAX_MOCK_NOTES];
  uint8_t mockNoteCount_ = 0;
  DeskFolder mockFolders_[8];
  uint8_t mockFolderCount_ = 0;
  DeskNoteMetadata metadata_[kMaxMetadata];
  uint16_t metadataCount_ = 0;
  DeskSession mockSessions_[12];
  uint8_t mockSessionCount_ = 0;

  void seedMock();
  bool ensureDirectory(const String &path) const;
  uint16_t nextSequentialId(const String &directory) const;
  void sortNotes(DeskDocument *documents, uint16_t count) const;
  void sortFolders(DeskFolder *folders, uint8_t count) const;
  int16_t metadataIndex(const String &path) const;
  DeskNoteMetadata &ensureMetadata(const String &path);
  bool loadMetadata();
  bool saveMetadata();
  bool atomicWrite(const String &path, const String &body) const;
  uint16_t scanArchive(DeskArchiveFilter filter, DeskDocument *out, uint16_t capacity) const;
  String displayTitle(const String &path) const;
};

#endif
