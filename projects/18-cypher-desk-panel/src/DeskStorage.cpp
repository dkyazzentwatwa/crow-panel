#include "DeskStorage.h"

#include "DeskSystemServices.h"

#if USE_CYPHER_DESK_SD
#include <SD_MMC.h>
#endif

#include <CrowPanelShared.h>
#include <new>
#include <time.h>

namespace {
#if USE_CYPHER_DESK_SD
const char *kIndexPath = CYPHER_DESK_DATA_DIR "/index.tsv";
const char *kSessionsPath = CYPHER_DESK_DATA_DIR "/sessions.csv";
#endif

String parentPath(const String &path) {
  int slash = path.lastIndexOf('/');
  return slash > 0 ? path.substring(0, slash) : "";
}

#if USE_CYPHER_DESK_SD
String csvSafe(String value) {
  value.replace(',', ' ');
  value.replace('\n', ' ');
  value.replace('\r', ' ');
  return value;
}
#endif

time_t dateEpoch(const String &date) {
  if (date.length() != 10) return 0;
  tm value = {};
  value.tm_year = date.substring(0, 4).toInt() - 1900;
  value.tm_mon = date.substring(5, 7).toInt() - 1;
  value.tm_mday = date.substring(8, 10).toInt();
  value.tm_hour = 12;
  value.tm_isdst = -1;
  return mktime(&value);
}
}  // namespace

void DeskStorage::begin(DeskStorageService *service) {
  service_ = service;
  persistent_ = false;
  ejected_ = false;
  if (mockNoteCount_ == 0) seedMock();

#if USE_CYPHER_DESK_SD
  // The card is already mounted (or known absent) by the time we get here -
  // CypherDeskOs::begin() runs storage_.begin() first, and it is the only
  // caller of SD_MMC.begin()/end() in the product.
  if (service_ == nullptr || !service_->mounted()) {
    status_ = "SD unavailable; RAM demo active (nonpersistent)";
    Logger::warn("cypher-desk", status_);
    return;
  }
  persistent_ = true;
  const char *required[] = {CYPHER_DESK_ROOT_DIR, CYPHER_DESK_NOTES_DIR,
                            CYPHER_DESK_NOTES_DIR "/daily",
                            CYPHER_DESK_NOTES_DIR "/scraps", CYPHER_DESK_AUDIO_DIR,
                            CYPHER_DESK_EXPORTS_DIR, CYPHER_DESK_DATA_DIR};
  for (const char *path : required) {
    if (!ensureDirectory(path)) {
      status_ = "SD workspace create failed; RAM demo active (nonpersistent)";
      persistent_ = false;
      Logger::error("cypher-desk", status_);
      return;
    }
  }
  status_ = "SD workspace ready";
  if (!loadMetadata()) {
    rebuildMetadata();
    status_ = "SD ready; metadata rebuilt";
  }
#endif
  Logger::info("cypher-desk", status_);
}

bool DeskStorage::persistent() const { return persistent_; }
bool DeskStorage::ejected() const { return ejected_; }
const String &DeskStorage::status() const { return status_; }
const char *DeskStorage::sourceLabel() const {
  if (ejected_) return "SD safely ejected";
  return persistent_ ? "SD workspace" : "RAM demo (temporary)";
}

void DeskStorage::seedMock() {
  mockFolderCount_ = 1;
  mockFolders_[0] = {String(CYPHER_DESK_NOTES_DIR) + "/business", "business", "Business"};
  mockNoteCount_ = 3;
  mockNotes_[0] = {String(CYPHER_DESK_NOTES_DIR) + "/0001_today.md", "Today", ".md"};
  mockBodies_[0] = "# Today\n\n- Choose the one important task\n- Capture one useful content idea\n";
  mockNotes_[1] = {String(CYPHER_DESK_NOTES_DIR) + "/0002_ideas.txt", "Ideas", ".txt"};
  mockBodies_[1] = "Everyday business owner content ideas\n\n";
  mockNotes_[2] = {String(CYPHER_DESK_NOTES_DIR) + "/business/0001_follow-ups.md", "Follow ups", ".md"};
  mockBodies_[2] = "# Follow ups\n\n- Customer question\n- Invoice check\n- Film while at the shop\n";
  for (uint8_t i = 0; i < mockNoteCount_; ++i) ensureMetadata(mockNotes_[i].path);
}

String DeskStorage::basename(const String &path) const {
  int slash = path.lastIndexOf('/');
  return slash >= 0 ? path.substring(slash + 1) : path;
}
String DeskStorage::extensionFromPath(const String &path) const {
  if (path.endsWith(".md")) return ".md";
  if (path.endsWith(".txt")) return ".txt";
  return "";
}
String DeskStorage::slugify(const String &nameValue) const {
  String name = nameValue;
  name.toLowerCase();
  String result;
  bool dash = false;
  for (size_t i = 0; i < name.length(); ++i) {
    char c = name[i];
    if (isalnum(static_cast<unsigned char>(c))) {
      result += c;
      dash = false;
    } else if (!dash && result.length()) {
      result += '-';
      dash = true;
    }
  }
  while (result.endsWith("-")) result.remove(result.length() - 1);
  return result.length() ? result : "untitled";
}
String DeskStorage::titleFromPath(const String &path) const {
  String name = basename(path);
  String extension = extensionFromPath(name);
  if (extension.length()) name.remove(name.length() - extension.length());
  int underscore = name.indexOf('_');
  if (underscore >= 0 && underscore + 1 < static_cast<int>(name.length())) name = name.substring(underscore + 1);
  name.replace('-', ' ');
  name.replace('_', ' ');
  name.trim();
  if (!name.length()) name = "Untitled";
  name[0] = toupper(name[0]);
  return name;
}
String DeskStorage::displayTitle(const String &path) const {
  if (parentPath(path) != String(CYPHER_DESK_NOTES_DIR) + "/scraps") return titleFromPath(path);
  String body = readTextFile(path, 512);
  int start = 0;
  while (start <= static_cast<int>(body.length())) {
    int end = body.indexOf('\n', start);
    if (end < 0) end = body.length();
    String line = body.substring(start, end);
    line.trim();
    while (line.startsWith("#") || line.startsWith(">")) {
      line.remove(0, 1);
      line.trim();
    }
    if (line.length()) return line.substring(0, min(static_cast<int>(line.length()), 38));
    start = end + 1;
  }
  return "Untitled scrap";
}
String DeskStorage::titleFromFolderName(const String &nameValue) const {
  String name = nameValue;
  name.replace('-', ' ');
  name.replace('_', ' ');
  name.trim();
  if (!name.length()) name = "Notebook";
  name[0] = toupper(name[0]);
  return name;
}

void DeskStorage::sortNotes(DeskDocument *documents, uint16_t count) const {
  for (uint16_t i = 0; i < count; ++i) {
    for (uint16_t j = i + 1; j < count; ++j) {
      DeskNoteMetadata a = metadataFor(documents[i].path);
      DeskNoteMetadata b = metadataFor(documents[j].path);
      bool newer = b.lastEdited > a.lastEdited ||
                   (b.lastEdited == a.lastEdited && documents[j].path > documents[i].path);
      if (newer) {
        DeskDocument swap = documents[i];
        documents[i] = documents[j];
        documents[j] = swap;
      }
    }
  }
}
void DeskStorage::sortFolders(DeskFolder *folders, uint8_t count) const {
  for (uint8_t i = 0; i < count; ++i) {
    for (uint8_t j = i + 1; j < count; ++j) {
      if (folders[j].name < folders[i].name) {
        DeskFolder swap = folders[i];
        folders[i] = folders[j];
        folders[j] = swap;
      }
    }
  }
}

uint16_t DeskStorage::listNotes(const String &directory, DeskDocument *out, uint16_t maxCount) const {
  return listNotesPage(directory, 0, out, maxCount, nullptr);
}

uint16_t DeskStorage::listNotesPage(const String &directory, uint16_t offset, DeskDocument *out,
                                    uint16_t maxCount, bool *hasMore) const {
  if (hasMore) *hasMore = false;
  if (out == nullptr || maxCount == 0) return 0;
  DeskDocument *all = new (std::nothrow) DeskDocument[kMaxMetadata];
  if (all == nullptr) return 0;
  uint16_t capacity = kMaxMetadata;
  uint16_t found = 0;
#if USE_CYPHER_DESK_SD
  if (persistent_) {
    File folder = SD_MMC.open(directory);
    if (!folder || !folder.isDirectory()) {
      delete[] all;
      return 0;
    }
    while (found < capacity) {
      File entry = folder.openNextFile();
      if (!entry) break;
      if (!entry.isDirectory()) {
        String path = entry.path();
        String extension = extensionFromPath(path);
        if (extension.length()) all[found++] = {path, displayTitle(path), extension};
      }
      entry.close();
    }
    folder.close();
  } else
#endif
  {
    for (uint8_t i = 0; i < mockNoteCount_ && found < capacity; ++i) {
      if (parentPath(mockNotes_[i].path) == directory) all[found++] = mockNotes_[i];
    }
  }
  sortNotes(all, found);
  if (offset >= found) {
    delete[] all;
    return 0;
  }
  uint16_t count = maxCount < found - offset ? maxCount : found - offset;
  for (uint16_t i = 0; i < count; ++i) out[i] = all[offset + i];
  if (hasMore) *hasMore = found > offset + count;
  delete[] all;
  return count;
}

uint8_t DeskStorage::listFolders(DeskFolder *out, uint8_t maxCount) const {
  if (out == nullptr || maxCount == 0) return 0;
  uint8_t count = 0;
#if USE_CYPHER_DESK_SD
  if (persistent_) {
    File folder = SD_MMC.open(CYPHER_DESK_NOTES_DIR);
    if (!folder || !folder.isDirectory()) return 0;
    while (count < maxCount) {
      File entry = folder.openNextFile();
      if (!entry) break;
      if (entry.isDirectory()) {
        String path = entry.path();
        String name = basename(path);
        if (name != "daily" && name != "scraps") out[count++] = {path, name, titleFromFolderName(name)};
      }
      entry.close();
    }
    folder.close();
  } else
#endif
  {
    count = min(maxCount, mockFolderCount_);
    for (uint8_t i = 0; i < count; ++i) out[i] = mockFolders_[i];
  }
  sortFolders(out, count);
  return count;
}

bool DeskStorage::ensureDirectory(const String &path) const {
#if USE_CYPHER_DESK_SD
  if (SD_MMC.exists(path)) return true;
  String current;
  int start = 1;
  while (start <= static_cast<int>(path.length())) {
    int slash = path.indexOf('/', start);
    if (slash < 0) slash = path.length();
    current = path.substring(0, slash);
    if (current.length() && !SD_MMC.exists(current) && !SD_MMC.mkdir(current)) return false;
    start = slash + 1;
  }
  return SD_MMC.exists(path);
#else
  (void)path;
  return false;
#endif
}

String DeskStorage::readTextFile(const String &path, size_t maxLen) const {
#if USE_CYPHER_DESK_SD
  if (persistent_) {
    File file = SD_MMC.open(path, FILE_READ);
    if (!file) return "";
    String body;
    body.reserve(min(maxLen, static_cast<size_t>(4096)));
    while (file.available() && body.length() < maxLen) body += static_cast<char>(file.read());
    file.close();
    return body;
  }
#endif
  for (uint8_t i = 0; i < mockNoteCount_; ++i) if (mockNotes_[i].path == path) return mockBodies_[i];
  return "";
}

bool DeskStorage::atomicWrite(const String &path, const String &body) const {
  // One implementation for the product, in DeskStorageService: temp -> verify
  // byte count -> rename original to .bak -> rename temp into place -> drop
  // .bak, with rollback on failure and a boot-time recovery pass. The copy that
  // used to live here skipped the low-space guard and never reported a short
  // write as an SD error state.
  return service_ != nullptr && service_->atomicWrite(path, body);
}

bool DeskStorage::saveTextFile(const String &path, const String &body) {
#if USE_CYPHER_DESK_SD
  if (persistent_) {
    bool ok = atomicWrite(path, body);
    if (ok) {
      DeskNoteMetadata &meta = ensureMetadata(path);
      meta.lastEdited = millis() / 1000;
      saveMetadata();
    }
    return ok;
  }
#endif
  for (uint8_t i = 0; i < mockNoteCount_; ++i) {
    if (mockNotes_[i].path == path) {
      mockBodies_[i] = body;
      mockNotes_[i].title = displayTitle(path);
      ensureMetadata(path).lastEdited = millis() / 1000;
      return true;
    }
  }
  if (mockNoteCount_ >= CYPHER_DESK_MAX_MOCK_NOTES) return false;
  mockNotes_[mockNoteCount_] = {path, titleFromPath(path), extensionFromPath(path)};
  mockBodies_[mockNoteCount_++] = body;
  ensureMetadata(path).lastEdited = millis() / 1000;
  return true;
}

bool DeskStorage::exists(const String &path) const {
#if USE_CYPHER_DESK_SD
  if (persistent_) return SD_MMC.exists(path);
#endif
  for (uint8_t i = 0; i < mockNoteCount_; ++i) if (mockNotes_[i].path == path) return true;
  return false;
}

uint16_t DeskStorage::nextSequentialId(const String &directory) const {
  uint16_t maxSeen = 0;
  DeskDocument documents[CYPHER_DESK_PAGE_SIZE];
  uint16_t count = listNotes(directory, documents, CYPHER_DESK_PAGE_SIZE);
  for (uint16_t i = 0; i < count; ++i) {
    uint16_t id = basename(documents[i].path).substring(0, 4).toInt();
    if (id > maxSeen) maxSeen = id;
  }
  return maxSeen + 1;
}
String DeskStorage::nextNotePath(const String &directory, const char *extension) const {
  const char *ext = String(extension) == ".md" ? ".md" : ".txt";
  char name[32];
  snprintf(name, sizeof(name), "%04u_note%s", nextSequentialId(directory), ext);
  return directory + "/" + name;
}
String DeskStorage::namedNotePath(const String &directory, const String &name, const char *extension) const {
  String ext = String(extension) == ".txt" ? ".txt" : ".md";
  String base = slugify(name);
  String path = directory + "/" + base + ext;
  for (uint8_t i = 2; exists(path) && i < 100; ++i) path = directory + "/" + base + "-" + i + ext;
  return path;
}
String DeskStorage::dailyPath(const String &isoDate) const {
  return String(CYPHER_DESK_NOTES_DIR) + "/daily/" + isoDate + ".md";
}
String DeskStorage::createScrapPath() const {
  return nextNotePath(String(CYPHER_DESK_NOTES_DIR) + "/scraps", ".md");
}

String DeskStorage::createNamedFolder(const String &name) {
  String slug = slugify(name);
  if (slug == "daily" || slug == "scraps") return "";
  String path = String(CYPHER_DESK_NOTES_DIR) + "/" + slug;
#if USE_CYPHER_DESK_SD
  if (persistent_) return (!SD_MMC.exists(path) && SD_MMC.mkdir(path)) ? path : "";
#endif
  for (uint8_t i = 0; i < mockFolderCount_; ++i) if (mockFolders_[i].path == path) return "";
  if (mockFolderCount_ >= 8) return "";
  mockFolders_[mockFolderCount_++] = {path, slug, titleFromFolderName(slug)};
  return path;
}
String DeskStorage::createFolder() {
  for (uint8_t id = 1; id < 100; ++id) {
    String name = "notebook-" + String(id);
    String result = createNamedFolder(name);
    if (result.length()) return result;
  }
  return "";
}

bool DeskStorage::removeNote(const String &path) {
#if USE_CYPHER_DESK_SD
  if (persistent_) {
    if (!SD_MMC.remove(path)) return false;
  } else
#endif
  {
    int found = -1;
    for (uint8_t i = 0; i < mockNoteCount_; ++i) if (mockNotes_[i].path == path) found = i;
    if (found < 0) return false;
    for (uint8_t i = found; i + 1 < mockNoteCount_; ++i) {
      mockNotes_[i] = mockNotes_[i + 1];
      mockBodies_[i] = mockBodies_[i + 1];
    }
    --mockNoteCount_;
  }
  int16_t index = metadataIndex(path);
  if (index >= 0) {
    for (uint16_t i = index; i + 1 < metadataCount_; ++i) metadata_[i] = metadata_[i + 1];
    --metadataCount_;
    saveMetadata();
  }
  return true;
}

bool DeskStorage::moveNote(const String &path, const String &newDirectory, String &newPath,
                           bool overwrite) {
  newPath = newDirectory + "/" + basename(path);
  if (newPath == path) return true;
  if (exists(newPath) && !overwrite) return false;
#if USE_CYPHER_DESK_SD
  if (persistent_) {
    if (!ensureDirectory(newDirectory)) return false;
    if (overwrite) SD_MMC.remove(newPath);
    if (!SD_MMC.rename(path, newPath)) return false;
  } else
#endif
  {
    int source = -1;
    for (uint8_t i = 0; i < mockNoteCount_; ++i) if (mockNotes_[i].path == path) source = i;
    if (source < 0) return false;
    mockNotes_[source].path = newPath;
  }
  int16_t index = metadataIndex(path);
  if (index >= 0) metadata_[index].path = newPath;
  saveMetadata();
  return true;
}

bool DeskStorage::renameNote(const String &path, const String &newName, String &newPath) {
  String ext = extensionFromPath(path);
  newPath = parentPath(path) + "/" + slugify(newName) + ext;
  if (newPath == path) return true;
  if (exists(newPath)) return false;
#if USE_CYPHER_DESK_SD
  if (persistent_) {
    if (!SD_MMC.rename(path, newPath)) return false;
    int16_t index = metadataIndex(path);
    if (index >= 0) metadata_[index].path = newPath;
    saveMetadata();
    return true;
  }
#endif
  for (uint8_t i = 0; i < mockNoteCount_; ++i) {
    if (mockNotes_[i].path == path) {
      mockNotes_[i].path = newPath;
      mockNotes_[i].title = titleFromPath(newPath);
      int16_t index = metadataIndex(path);
      if (index >= 0) metadata_[index].path = newPath;
      return true;
    }
  }
  return false;
}

bool DeskStorage::exportCopy(const String &path, const String &isoDate, String &exportPath) {
  String name = basename(path);
  String ext = extensionFromPath(name);
  if (ext.length()) name.remove(name.length() - ext.length());
  exportPath = String(CYPHER_DESK_EXPORTS_DIR) + "/" + isoDate + "-" + String(millis()) + "-" + name + ext;
#if USE_CYPHER_DESK_SD
  if (persistent_) return atomicWrite(exportPath, readTextFile(path));
#endif
  return saveTextFile(exportPath, readTextFile(path));
}

bool DeskStorage::safeEject() {
#if USE_CYPHER_DESK_SD
  if (persistent_ && service_ != nullptr) {
    // Flush the note index first, then let the one mount owner unmount. This
    // used to call SD_MMC.end() directly, which left DeskStorageService still
    // reporting a mounted card it no longer had.
    saveMetadata();
    if (!service_->safeEject()) {
      status_ = "SD eject refused; card not mounted";
      return false;
    }
    persistent_ = false;
    ejected_ = true;
    status_ = "SD safely ejected; remount before writing";
    return true;
  }
#endif
  status_ = "No mounted SD card";
  return false;
}

int16_t DeskStorage::metadataIndex(const String &path) const {
  for (uint16_t i = 0; i < metadataCount_; ++i) if (metadata_[i].path == path) return i;
  return -1;
}
DeskNoteMetadata &DeskStorage::ensureMetadata(const String &path) {
  int16_t index = metadataIndex(path);
  if (index >= 0) return metadata_[index];
  if (metadataCount_ >= kMaxMetadata) return metadata_[kMaxMetadata - 1];
  metadata_[metadataCount_] = {};
  metadata_[metadataCount_].path = path;
  return metadata_[metadataCount_++];
}
DeskNoteMetadata DeskStorage::metadataFor(const String &path) const {
  int16_t index = metadataIndex(path);
  if (index >= 0) return metadata_[index];
  DeskNoteMetadata empty;
  empty.path = path;
  return empty;
}
bool DeskStorage::setMetadata(const DeskNoteMetadata &metadata) {
  ensureMetadata(metadata.path) = metadata;
  return saveMetadata();
}
bool DeskStorage::toggleFavorite(const String &path) {
  DeskNoteMetadata &meta = ensureMetadata(path);
  meta.favorite = !meta.favorite;
  return saveMetadata();
}
bool DeskStorage::toggleFinished(const String &path) {
  DeskNoteMetadata &meta = ensureMetadata(path);
  meta.finished = !meta.finished;
  return saveMetadata();
}
bool DeskStorage::setNotebookColor(const String &path, uint8_t color) {
  DeskNoteMetadata &meta = ensureMetadata(path);
  meta.notebookColor = color % kDeskThemeCount;
  return saveMetadata();
}

bool DeskStorage::loadMetadata() {
#if USE_CYPHER_DESK_SD
  if (!persistent_ || !SD_MMC.exists(kIndexPath)) return false;
  String body = readTextFile(kIndexPath, 48000);
  if (!body.startsWith("path\tfavorite\tfinished\tcolor\topened\tedited\n")) return false;
  metadataCount_ = 0;
  int start = body.indexOf('\n') + 1;
  while (start > 0 && start < static_cast<int>(body.length())) {
    int end = body.indexOf('\n', start);
    if (end < 0) end = body.length();
    String line = body.substring(start, end);
    int fields[5];
    int cursor = 0;
    bool valid = true;
    for (uint8_t i = 0; i < 5; ++i) {
      fields[i] = line.indexOf('\t', cursor);
      if (fields[i] < 0) { valid = false; break; }
      cursor = fields[i] + 1;
    }
    if (!valid || metadataCount_ >= kMaxMetadata) return false;
    DeskNoteMetadata &meta = metadata_[metadataCount_++];
    meta.path = line.substring(0, fields[0]);
    meta.favorite = line.substring(fields[0] + 1, fields[1]).toInt() != 0;
    meta.finished = line.substring(fields[1] + 1, fields[2]).toInt() != 0;
    meta.notebookColor = line.substring(fields[2] + 1, fields[3]).toInt();
    meta.lastOpened = strtoul(line.substring(fields[3] + 1, fields[4]).c_str(), nullptr, 10);
    meta.lastEdited = strtoul(line.substring(fields[4] + 1).c_str(), nullptr, 10);
    if (!meta.path.startsWith(CYPHER_DESK_NOTES_DIR)) return false;
    start = end + 1;
  }
  return true;
#else
  return false;
#endif
}

bool DeskStorage::saveMetadata() {
#if USE_CYPHER_DESK_SD
  if (!persistent_) return false;
  String body = "path\tfavorite\tfinished\tcolor\topened\tedited\n";
  for (uint16_t i = 0; i < metadataCount_; ++i) {
    const DeskNoteMetadata &meta = metadata_[i];
    if (!meta.path.startsWith(CYPHER_DESK_NOTES_DIR)) continue;
    body += meta.path + "\t" + String(meta.favorite ? 1 : 0) + "\t" +
            String(meta.finished ? 1 : 0) + "\t" + String(meta.notebookColor) + "\t" +
            String(meta.lastOpened) + "\t" + String(meta.lastEdited) + "\n";
  }
  return atomicWrite(kIndexPath, body);
#else
  return true;
#endif
}

bool DeskStorage::rebuildMetadata() {
  metadataCount_ = 0;
  DeskDocument *documents = new (std::nothrow) DeskDocument[kMaxMetadata];
  if (documents == nullptr) return false;
  uint16_t count = scanArchive(kDeskArchiveRecent, documents, kMaxMetadata);
  for (uint16_t i = 0; i < count; ++i) {
    DeskNoteMetadata &meta = ensureMetadata(documents[i].path);
    meta.lastEdited = i + 1;
  }
  delete[] documents;
  return saveMetadata();
}

uint16_t DeskStorage::scanArchive(DeskArchiveFilter filter, DeskDocument *out, uint16_t capacity) const {
  uint16_t count = 0;
  DeskDocument *page = new (std::nothrow) DeskDocument[kMaxMetadata];
  if (page == nullptr) return 0;
  auto addDirectory = [&](const String &directory) {
    uint16_t found = listNotesPage(directory, 0, page, kMaxMetadata, nullptr);
    for (uint16_t i = 0; i < found && count < capacity; ++i) {
      DeskNoteMetadata meta = metadataFor(page[i].path);
      bool include = filter == kDeskArchiveRecent ||
                     (filter == kDeskArchiveFavorites && meta.favorite) ||
                     (filter == kDeskArchiveFinished && meta.finished) ||
                     (filter == kDeskArchiveDaily && directory.endsWith("/daily")) ||
                     (filter == kDeskArchiveScraps && directory.endsWith("/scraps"));
      if (include) out[count++] = page[i];
    }
  };
  addDirectory(CYPHER_DESK_NOTES_DIR);
  addDirectory(String(CYPHER_DESK_NOTES_DIR) + "/daily");
  addDirectory(String(CYPHER_DESK_NOTES_DIR) + "/scraps");
  DeskFolder folders[CYPHER_DESK_MAX_FOLDERS];
  uint8_t folderCount = listFolders(folders, CYPHER_DESK_MAX_FOLDERS);
  for (uint8_t i = 0; i < folderCount && count < capacity; ++i) addDirectory(folders[i].path);
  sortNotes(out, count);
  delete[] page;
  return count;
}

uint16_t DeskStorage::queryArchive(DeskArchiveFilter filter, uint16_t offset, DeskDocument *out,
                                   uint16_t maxCount, bool *hasMore) const {
  DeskDocument *all = new (std::nothrow) DeskDocument[kMaxMetadata];
  if (all == nullptr) {
    if (hasMore) *hasMore = false;
    return 0;
  }
  uint16_t found = scanArchive(filter, all, kMaxMetadata);
  if (hasMore) *hasMore = found > offset + maxCount;
  if (offset >= found) {
    delete[] all;
    return 0;
  }
  uint16_t count = maxCount < found - offset ? maxCount : found - offset;
  for (uint16_t i = 0; i < count; ++i) out[i] = all[offset + i];
  delete[] all;
  return count;
}

bool DeskStorage::logSession(const DeskSession &session) {
#if USE_CYPHER_DESK_SD
  if (persistent_) {
    String existing;
    if (SD_MMC.exists(kSessionsPath)) existing = readTextFile(kSessionsPath, 64000);
    if (!existing.length()) existing = "date,duration,starting_words,ending_words,document,completion\n";
    existing += csvSafe(session.date) + "," + String(session.durationMinutes) + "," +
                String(session.startingWords) + "," + String(session.endingWords) + "," +
                csvSafe(session.documentPath) + "," + (session.completed ? "completed" : "early") + "\n";
    return atomicWrite(kSessionsPath, existing);
  }
#endif
  if (mockSessionCount_ >= 12) return false;
  mockSessions_[mockSessionCount_++] = session;
  return true;
}

DeskStats DeskStorage::stats(const String &today) const {
  DeskStats result;
  time_t todayEpoch = dateEpoch(today);
  auto apply = [&](const DeskSession &session) {
    time_t sessionEpoch = dateEpoch(session.date);
    double ageDays = todayEpoch && sessionEpoch ? difftime(todayEpoch, sessionEpoch) / 86400.0 : 99;
    if (session.date == today) result.quietMinutesToday += session.durationMinutes;
    if (ageDays >= 0 && ageDays < 7) {
      result.quietMinutesWeek += session.durationMinutes;
      result.wordsAddedWeek += session.wordDelta;
      if (session.completed) ++result.sessionsCompleted;
    }
    result.lastSession = session.date + "  " + String(session.durationMinutes) + " min";
  };
#if USE_CYPHER_DESK_SD
  if (persistent_ && SD_MMC.exists(kSessionsPath)) {
    String body = readTextFile(kSessionsPath, 64000);
    int start = body.indexOf('\n') + 1;
    while (start > 0 && start < static_cast<int>(body.length())) {
      int end = body.indexOf('\n', start);
      if (end < 0) end = body.length();
      String line = body.substring(start, end);
      int comma[5];
      int cursor = 0;
      bool valid = true;
      for (uint8_t i = 0; i < 5; ++i) {
        comma[i] = line.indexOf(',', cursor);
        if (comma[i] < 0) { valid = false; break; }
        cursor = comma[i] + 1;
      }
      if (valid) {
        DeskSession session;
        session.date = line.substring(0, comma[0]);
        session.durationMinutes = line.substring(comma[0] + 1, comma[1]).toInt();
        session.startingWords = line.substring(comma[1] + 1, comma[2]).toInt();
        session.endingWords = line.substring(comma[2] + 1, comma[3]).toInt();
        session.wordDelta = static_cast<int32_t>(session.endingWords) - session.startingWords;
        session.documentPath = line.substring(comma[3] + 1, comma[4]);
        session.completed = line.substring(comma[4] + 1) == "completed";
        apply(session);
      }
      start = end + 1;
    }
    return result;
  }
#endif
  for (uint8_t i = 0; i < mockSessionCount_; ++i) apply(mockSessions_[i]);
  return result;
}
