#include "DeskSystemServices.h"

#include <CrowPanelShared.h>
#include <Preferences.h>
#include <time.h>

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
#include <Arduino_GFX_Library.h>
#endif

#if USE_CYPHER_DESK_SD
#include <SD_MMC.h>
#endif

#if USE_WIFI
#include <WiFi.h>
#include <WiFiClient.h>
#endif

#if USE_CYPHER_DESK_WEATHER && USE_WIFI
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#endif

// All I2S now lives in DeskAudioEngine (raw IDF i2s_std), which
// DeskSystemServices.h pulls in along with CYPHER_DESK_AUDIO_BACKEND.
#include "DeskWavReader.h"

namespace {
constexpr int16_t kScreenW = 1024;
constexpr int16_t kScreenH = 600;
constexpr const char *kPreferencesNamespace = "cypher-desk";

#if USE_CYPHER_DESK_SD
File gDeskRecordingFile;
File gDeskPlaybackFile;
#endif

int16_t clampAxis(long value, int16_t maxValue) {
  if (value < 0) return 0;
  if (value > maxValue) return maxValue;
  return static_cast<int16_t>(value);
}
int16_t mapAxis(int16_t value, int16_t minimum, int16_t maximum, int16_t outputMaximum) {
  if (minimum == maximum) return 0;
  return clampAxis(static_cast<long>(value - minimum) * outputMaximum /
                   (maximum - minimum), outputMaximum);
}
#if USE_CYPHER_DESK_SD
String parentPath(const String &path) {
  int slash = path.lastIndexOf('/');
  return slash > 0 ? path.substring(0, slash) : "";
}
#endif

#if USE_CYPHER_DESK_SD
void writeLe16(File &file, uint16_t value) {
  uint8_t bytes[2] = {static_cast<uint8_t>(value), static_cast<uint8_t>(value >> 8)};
  file.write(bytes, sizeof(bytes));
}
void writeLe32(File &file, uint32_t value) {
  uint8_t bytes[4] = {static_cast<uint8_t>(value), static_cast<uint8_t>(value >> 8),
                      static_cast<uint8_t>(value >> 16), static_cast<uint8_t>(value >> 24)};
  file.write(bytes, sizeof(bytes));
}
void writeWavHeader(File &file, uint32_t dataBytes) {
  file.seek(0);
  file.write(reinterpret_cast<const uint8_t *>("RIFF"), 4); writeLe32(file, 36 + dataBytes);
  file.write(reinterpret_cast<const uint8_t *>("WAVEfmt "), 8); writeLe32(file, 16);
  writeLe16(file, 1); writeLe16(file, 1); writeLe32(file, 16000);
  writeLe32(file, 32000); writeLe16(file, 2); writeLe16(file, 16);
  file.write(reinterpret_cast<const uint8_t *>("data"), 4); writeLe32(file, dataBytes);
}
uint16_t readLe16(File &file) {
  uint8_t bytes[2] = {};
  return file.read(bytes, sizeof(bytes)) == sizeof(bytes)
             ? static_cast<uint16_t>(bytes[0]) | (static_cast<uint16_t>(bytes[1]) << 8) : 0;
}
uint32_t readLe32(File &file) {
  uint8_t bytes[4] = {};
  return file.read(bytes, sizeof(bytes)) == sizeof(bytes)
             ? static_cast<uint32_t>(bytes[0]) | (static_cast<uint32_t>(bytes[1]) << 8) |
                   (static_cast<uint32_t>(bytes[2]) << 16) | (static_cast<uint32_t>(bytes[3]) << 24) : 0;
}
#endif
}

bool DeskDisplayService::begin() {
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  // manualFlush: draws land in the cached PSRAM framebuffer and only explicit
  // flush() calls reach the panel. Required for per-key press art, the music
  // scrub bar, and the video window - the video player writes through PPA,
  // which bypasses Arduino_GFX entirely and would never be synced otherwise.
  return CrowDisplay::begin(activeHardwareProfile(), "CYPHER DESK OS", true);
#else
  return false;
#endif
}
void DeskDisplayService::tick() {
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  CrowDisplay::tick();
#endif
}
bool DeskDisplayService::ready() const {
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  return CrowDisplay::canvas() != nullptr;
#else
  return false;
#endif
}
Arduino_GFX *DeskDisplayService::canvas() const {
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  return CrowDisplay::canvas();
#else
  return nullptr;
#endif
}

uint32_t DeskStorageService::mountGeneration() const { return mountGeneration_; }
void DeskStorageService::setState(DeskSdState state, const String &reason) {
  if (state_ == state && reason.isEmpty()) return;
  // Every unmounted -> mounted edge is a new generation. Views that cached
  // SD-backed records compare against this instead of latching a "loaded once"
  // flag, which is why calendar and contacts stayed empty when a card was
  // inserted after boot.
  const bool wasMounted = state_ == kDeskSdMounted || state_ == kDeskSdFull;
  const bool nowMounted = state == kDeskSdMounted || state == kDeskSdFull;
  if (nowMounted && !wasMounted) ++mountGeneration_;
  state_ = state;
  if (events_ != nullptr && reason.length()) events_->publish(kDeskEventStorage, reason);
  if (reason.length()) Logger::info("desk-storage", reason);
}
void DeskStorageService::begin(DeskEventBus *events) {
  events_ = events;
  deliberatelyEjected_ = false;
  mountCard();
}
bool DeskStorageService::mountCard() {
#if USE_CYPHER_DESK_SD
  setState(kDeskSdMounting, "mounting SD card");
  bool alreadyMounted = SD_MMC.cardType() != CARD_NONE;
  if (!alreadyMounted && !SD_MMC.begin("/sdcard", CYPHER_DESK_SDMMC_1BIT != 0)) {
    setState(kDeskSdNotPresent, "SD unavailable; local apps use nonpersistent fallback");
    return false;
  }
  if (SD_MMC.cardType() == CARD_NONE) {
    setState(kDeskSdNotPresent, "no SD card detected");
    return false;
  }
  // A card that mounts but reports no capacity is the signature of a
  // filesystem the core's FAT driver cannot read - almost always exFAT, which
  // is what a modern desktop formats a >32 GB card as by default. Saying so is
  // far more useful than a generic mount failure.
  if (SD_MMC.totalBytes() == 0) {
    setState(kDeskSdUnsupportedFilesystem, "card is not FAT32; reformat as FAT32");
    return false;
  }
  const char *directories[] = {
      CYPHER_DESK_ROOT_DIR, CYPHER_DESK_NOTES_DIR, CYPHER_DESK_RECORDINGS_DIR,
      CYPHER_DESK_MUSIC_DIR, CYPHER_DESK_PODCASTS_DIR, CYPHER_DESK_VIDEO_DIR,
      CYPHER_DESK_AUDIO_DIR, CYPHER_DESK_DOCUMENTS_DIR,
      CYPHER_DESK_CALENDAR_DIR, CYPHER_DESK_CONTACTS_DIR, CYPHER_DESK_EXPORTS_DIR,
      CYPHER_DESK_BACKUPS_DIR, CYPHER_DESK_CACHE_DIR, CYPHER_DESK_DATA_DIR};
  for (const char *directory : directories) {
    if (!ensureDirectory(directory)) {
      setState(kDeskSdError, "SD directory setup failed");
      return false;
    }
  }
  deliberatelyEjected_ = false;
  setState(lowSpace() ? kDeskSdFull : kDeskSdMounted,
           lowSpace() ? "SD low-space guard active" : "SD workspace mounted");
  recoverTransactions();
  return true;
#else
  setState(kDeskSdNotPresent, "SD support disabled at compile time");
  return false;
#endif
}
void DeskStorageService::tick(uint32_t nowMs) {
#if USE_CYPHER_DESK_SD
  if (nowMs - lastPollMs_ < 750) return;
  lastPollMs_ = nowMs;
  bool present = SD_MMC.cardType() != CARD_NONE;
  if (!present && (state_ == kDeskSdMounted || state_ == kDeskSdFull)) {
    setState(deliberatelyEjected_ ? kDeskSdNotPresent : kDeskSdRemovedUnexpectedly,
             deliberatelyEjected_ ? "SD remains safely ejected" : "SD removed unexpectedly");
  } else if (present && state_ == kDeskSdRemovedUnexpectedly) {
    setState(kDeskSdMounted, "SD detected again; reboot or remount before writing");
  } else if (present && state_ == kDeskSdMounted && lowSpace()) {
    setState(kDeskSdFull, "SD low-space guard active");
  }
#else
  (void)nowMs;
#endif
}
DeskSdState DeskStorageService::state() const { return state_; }
const char *DeskStorageService::stateLabel() const {
  switch (state_) {
    case kDeskSdNotPresent: return "Not present";
    case kDeskSdMounting: return "Mounting";
    case kDeskSdMounted: return "Mounted";
    case kDeskSdReadOnly: return "Read only";
    case kDeskSdUnsupportedFilesystem: return "Unsupported FS";
    case kDeskSdCorrupted: return "Corrupted";
    case kDeskSdFull: return "Full / low space";
    case kDeskSdRemovedUnexpectedly: return "Removed unexpectedly";
    default: return "Error";
  }
}
String DeskStorageService::status() const {
  String result = stateLabel();
  if (mounted()) result += " // " + String(freePercent()) + "% free";
  return result;
}
bool DeskStorageService::mounted() const { return state_ == kDeskSdMounted || state_ == kDeskSdFull; }
uint64_t DeskStorageService::totalBytes() const {
#if USE_CYPHER_DESK_SD
  return mounted() ? SD_MMC.totalBytes() : 0;
#else
  return 0;
#endif
}
uint64_t DeskStorageService::freeBytes() const {
#if USE_CYPHER_DESK_SD
  uint64_t total = totalBytes();
  uint64_t used = mounted() ? SD_MMC.usedBytes() : 0;
  return total > used ? total - used : 0;
#else
  return 0;
#endif
}
uint8_t DeskStorageService::freePercent() const {
  uint64_t total = totalBytes();
  return total ? static_cast<uint8_t>((freeBytes() * 100ULL) / total) : 0;
}
bool DeskStorageService::lowSpace() const {
  uint64_t total = totalBytes();
  if (!total) return false;
  uint64_t fivePercent = total / 20ULL;
  uint64_t threshold = fivePercent > CYPHER_DESK_LOW_SPACE_BYTES
                           ? fivePercent : CYPHER_DESK_LOW_SPACE_BYTES;
  return freeBytes() < threshold;
}
bool DeskStorageService::safeEject() {
#if USE_CYPHER_DESK_SD
  if (!mounted()) return false;
  SD_MMC.end();
  deliberatelyEjected_ = true;
  setState(kDeskSdNotPresent, "SD safely ejected");
  return true;
#else
  return false;
#endif
}
bool DeskStorageService::remount() { deliberatelyEjected_ = false; return mountCard(); }
bool DeskStorageService::ensureDirectory(const String &path) {
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
bool DeskStorageService::renamePath(const String &source, const String &destination) {
#if USE_CYPHER_DESK_SD
  return mounted() && !pathProtected(source) && !pathProtected(destination) && !SD_MMC.exists(destination) && ensureDirectory(parentPath(destination)) &&
         SD_MMC.rename(source, destination);
#else
  (void)source; (void)destination;
  return false;
#endif
}
String DeskStorageService::readText(const String &path, size_t maxLength) const {
#if USE_CYPHER_DESK_SD
  if (!mounted()) return "";
  File file = SD_MMC.open(path, FILE_READ);
  if (!file || file.isDirectory()) return "";
  String body;
  body.reserve(min(maxLength, static_cast<size_t>(4096)));
  while (file.available() && body.length() < maxLength) body += static_cast<char>(file.read());
  file.close();
  return body;
#else
  (void)path; (void)maxLength;
  return "";
#endif
}
bool DeskStorageService::atomicWrite(const String &path, const String &body) {
#if USE_CYPHER_DESK_SD
  if (state_ != kDeskSdMounted || lowSpace() || !ensureDirectory(parentPath(path))) return false;
  String temporary = path + ".tmp";
  String backup = path + ".bak";
  SD_MMC.remove(temporary);
  File file = SD_MMC.open(temporary, FILE_WRITE);
  if (!file) { setState(kDeskSdReadOnly, "SD write open failed"); return false; }
  size_t written = file.print(body);
  file.flush();
  file.close();
  if (written != body.length()) {
    setState(kDeskSdError, "SD short write; recovery file retained");
    return false;
  }
  bool hadOriginal = SD_MMC.exists(path);
  SD_MMC.remove(backup);
  if (hadOriginal && !SD_MMC.rename(path, backup)) return false;
  if (!SD_MMC.rename(temporary, path)) {
    if (hadOriginal) SD_MMC.rename(backup, path);
    setState(kDeskSdError, "SD atomic rename failed");
    return false;
  }
  if (hadOriginal) SD_MMC.remove(backup);
  return true;
#else
  (void)path; (void)body;
  return false;
#endif
}
bool DeskStorageService::copyFile(const String &source, const String &destination) {
#if USE_CYPHER_DESK_SD
  if (state_ != kDeskSdMounted || pathProtected(source) || pathProtected(destination) || lowSpace() || !ensureDirectory(parentPath(destination))) return false;
  File input = SD_MMC.open(source, FILE_READ);
  if (!input || input.isDirectory()) return false;
  String temporary = destination + ".tmp";
  SD_MMC.remove(temporary);
  File output = SD_MMC.open(temporary, FILE_WRITE);
  if (!output) { input.close(); return false; }
  uint8_t buffer[1024];
  bool ok = true;
  while (input.available()) {
    size_t count = input.read(buffer, sizeof(buffer));
    if (!count || output.write(buffer, count) != count) { ok = false; break; }
  }
  output.flush(); output.close(); input.close();
  if (!ok || SD_MMC.exists(destination) || !SD_MMC.rename(temporary, destination)) {
    SD_MMC.remove(temporary);
    return false;
  }
  return true;
#else
  (void)source; (void)destination;
  return false;
#endif
}
bool DeskStorageService::moveFile(const String &source, const String &destination) {
#if USE_CYPHER_DESK_SD
  if (!mounted() || pathProtected(source) || pathProtected(destination) || !ensureDirectory(parentPath(destination)) || SD_MMC.exists(destination)) return false;
  return SD_MMC.rename(source, destination);
#else
  (void)source; (void)destination;
  return false;
#endif
}
bool DeskStorageService::removeFile(const String &path) {
#if USE_CYPHER_DESK_SD
  return mounted() && !pathProtected(path) && SD_MMC.remove(path);
#else
  (void)path;
  return false;
#endif
}
uint16_t DeskStorageService::countFiles(const String &directory, const char *extension) const {
#if USE_CYPHER_DESK_SD
  if (!mounted()) return 0;
  File folder = SD_MMC.open(directory);
  if (!folder || !folder.isDirectory()) return 0;
  uint16_t count = 0;
  while (true) {
    File entry = folder.openNextFile();
    if (!entry) break;
    if (!entry.isDirectory()) {
      String path = entry.path();
      if (extension == nullptr || path.endsWith(extension)) ++count;
    }
    entry.close();
  }
  folder.close();
  return count;
#else
  (void)directory; (void)extension;
  return 0;
#endif
}
uint8_t DeskStorageService::listFileNames(const String &directory, String *out, uint8_t maxCount) const {
#if USE_CYPHER_DESK_SD
  if (!mounted() || out == nullptr || maxCount == 0) return 0;
  File folder = SD_MMC.open(directory);
  if (!folder || !folder.isDirectory()) return 0;
  uint8_t count = 0;
  while (count < maxCount) {
    File entry = folder.openNextFile();
    if (!entry) break;
    String path = entry.path();
    int slash = path.lastIndexOf('/');
    out[count++] = slash >= 0 ? path.substring(slash + 1) : path;
    entry.close();
  }
  folder.close();
  return count;
#else
  (void)directory; (void)out; (void)maxCount;
  return 0;
#endif
}
uint8_t DeskStorageService::listDirectory(const String &directory, FileEntry *out, uint8_t maxCount) const {
#if USE_CYPHER_DESK_SD
  if (!mounted() || out == nullptr || maxCount == 0) return 0;
  File folder = SD_MMC.open(directory);
  if (!folder || !folder.isDirectory()) return 0;
  uint8_t count = 0;
  while (count < maxCount) {
    File entry = folder.openNextFile();
    if (!entry) break;
    String path = entry.path();
    int slash = path.lastIndexOf('/');
    out[count++] = {path, slash >= 0 ? path.substring(slash + 1) : path,
                    entry.isDirectory(), static_cast<uint32_t>(entry.size())};
    entry.close();
  }
  folder.close();
  return count;
#else
  (void)directory; (void)out; (void)maxCount;
  return 0;
#endif
}
bool DeskStorageService::pathProtected(const String &path) const {
  return path == CYPHER_DESK_DATA_DIR || path.startsWith(String(CYPHER_DESK_DATA_DIR) + "/") ||
         path == CYPHER_DESK_CACHE_DIR || path.startsWith(String(CYPHER_DESK_CACHE_DIR) + "/") ||
         path == CYPHER_DESK_BACKUPS_DIR || path.startsWith(String(CYPHER_DESK_BACKUPS_DIR) + "/");
}
bool DeskStorageService::textFile(const String &path) const {
  return path.endsWith(".txt") || path.endsWith(".md") || path.endsWith(".csv") || path.endsWith(".tsv");
}
uint8_t DeskStorageService::recoverTransactions() {
#if USE_CYPHER_DESK_SD
  const char *targets[] = {CYPHER_DESK_CALENDAR_DIR "/events.tsv",
                           CYPHER_DESK_CONTACTS_DIR "/contacts.tsv"};
  uint8_t recovered = 0;
  for (const char *target : targets) {
    String path(target);
    String temporary = path + ".tmp";
    String backup = path + ".bak";
    if (!SD_MMC.exists(path) && SD_MMC.exists(backup) && SD_MMC.rename(backup, path)) ++recovered;
    if (SD_MMC.exists(path) && SD_MMC.exists(temporary)) SD_MMC.remove(temporary);
  }
  if (recovered && events_ != nullptr) events_->publish(kDeskEventRecovery, "restored structured data backup");
  return recovered;
#else
  return 0;
#endif
}
const char *DeskStorageService::cardLabel() const {
#if USE_CYPHER_DESK_SD
  switch (SD_MMC.cardType()) {
    case CARD_MMC: return "MMC";
    case CARD_SD: return "SDSC";
    case CARD_SDHC: return "SDHC/SDXC";
    case CARD_NONE: return "none";
    default: return "unknown";
  }
#else
  return "disabled";
#endif
}
void DeskStorageService::print(Print &out) const {
  out.print(F("[storage] state=")); out.print(stateLabel());
  out.print(F(" card=")); out.print(cardLabel());
  out.print(F(" total_mb=")); out.print(static_cast<uint32_t>(totalBytes() / (1024ULL * 1024ULL)));
  out.print(F(" free_mb=")); out.print(static_cast<uint32_t>(freeBytes() / (1024ULL * 1024ULL)));
  out.print(F(" free_percent=")); out.print(freePercent());
  out.print(F(" low_space=")); out.println(lowSpace() ? "yes" : "no");
}

void DeskWifiService::setState(DeskWifiState state, const String &status) {
  bool changed = state_ != state || status_ != status;
  state_ = state;
  status_ = status;
  stateStartedMs_ = millis();
  if (changed && events_ != nullptr) events_->publish(kDeskEventWifi, status);
  if (changed) Logger::info("desk-wifi", status);
}
void DeskWifiService::begin(DeskEventBus *events) {
  events_ = events;
  loadProfiles();
#if USE_WIFI
  setState(kDeskWifiIdle, "Wi-Fi idle");
#else
  setState(kDeskWifiDisabled, "Wi-Fi disabled at compile time");
#endif
}
void DeskWifiService::setOffline(bool offline) {
  offline_ = offline;
  if (offline) disconnect();
  setState(offline ? kDeskWifiDisabled : kDeskWifiIdle,
           offline ? "offline mode" : "Wi-Fi idle");
}
bool DeskWifiService::offline() const { return offline_; }
void DeskWifiService::scan() {
#if USE_WIFI
  if (offline_) return;
  if (!configureCrowPanelHostedWiFiPins("cypher-desk-os")) {
    setState(kDeskWifiError, "hosted-C6 pin configuration failed");
    return;
  }
  WiFi.mode(WIFI_STA);
  WiFi.scanDelete();
  if (WiFi.scanNetworks(true, true) == WIFI_SCAN_FAILED) {
    setState(kDeskWifiError, "Wi-Fi scan could not start");
    return;
  }
  networkCount_ = 0;
  setState(kDeskWifiScanning, "scanning for networks");
#endif
}
bool DeskWifiService::connect(const String &ssid, const String &password, bool saveProfile) {
#if USE_WIFI
  if (offline_ || ssid.isEmpty()) return false;
  if (!configureCrowPanelHostedWiFiPins("cypher-desk-os")) {
    setState(kDeskWifiError, "hosted-C6 unavailable");
    return false;
  }
  pendingSsid_ = ssid;
  pendingPassword_ = password;
  savePending_ = saveProfile;
  connectivityChecked_ = false;
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), password.c_str());
  setState(kDeskWifiConnecting, "connecting to " + ssid);
  return true;
#else
  (void)ssid; (void)password; (void)saveProfile;
  return false;
#endif
}
void DeskWifiService::disconnect() {
#if USE_WIFI
  WiFi.disconnect(false, false);
#endif
  if (!offline_) setState(kDeskWifiDisconnected, "Wi-Fi disconnected");
}
void DeskWifiService::tick(uint32_t nowMs) {
#if USE_WIFI
  if (offline_) return;
  if (state_ == kDeskWifiScanning) {
    int16_t found = WiFi.scanComplete();
    if (found == WIFI_SCAN_RUNNING) return;
    if (found < 0) { setState(kDeskWifiError, "Wi-Fi scan failed"); return; }
    networkCount_ = min(static_cast<uint8_t>(found), kMaxNetworks);
    for (uint8_t i = 0; i < networkCount_; ++i) {
      networks_[i] = {WiFi.SSID(i), WiFi.RSSI(i), WiFi.encryptionType(i) != WIFI_AUTH_OPEN};
    }
    WiFi.scanDelete();
    setState(kDeskWifiNetworksFound, String(networkCount_) + " networks found");
    return;
  }
  if (state_ == kDeskWifiConnecting) {
    wl_status_t status = WiFi.status();
    if (status == WL_CONNECTED) {
      if (savePending_) rememberProfile(pendingSsid_, pendingPassword_);
      setState(kDeskWifiConnectedNoInternet, "connected; checking internet");
      connectivityChecked_ = false;
      return;
    }
    if (status == WL_CONNECT_FAILED) {
      setState(kDeskWifiAuthenticationFailed, "authentication failed");
      return;
    }
    if (status == WL_NO_SSID_AVAIL) {
      setState(kDeskWifiNetworkNotFound, "network not found");
      return;
    }
    if (nowMs - stateStartedMs_ > 18000) {
      setState(kDeskWifiDisconnected, "connection timed out");
      return;
    }
  }
  if (state_ == kDeskWifiConnectedNoInternet && !connectivityChecked_) runConnectivityCheck();
  if ((state_ == kDeskWifiConnected || state_ == kDeskWifiCaptivePortalSuspected) &&
      WiFi.status() != WL_CONNECTED) setState(kDeskWifiDisconnected, "Wi-Fi link lost");
#else
  (void)nowMs;
#endif
}
void DeskWifiService::runConnectivityCheck() {
#if USE_WIFI
  connectivityChecked_ = true;
  WiFiClient client;
  client.setTimeout(350);
  if (!client.connect(CYPHER_DESK_CONNECTIVITY_HOST, CYPHER_DESK_CONNECTIVITY_PORT, 350)) {
    setState(kDeskWifiConnectedNoInternet, "connected without verified internet");
    return;
  }
  client.print(String("GET ") + CYPHER_DESK_CONNECTIVITY_PATH +
               " HTTP/1.1\r\nHost: " + CYPHER_DESK_CONNECTIVITY_HOST +
               "\r\nConnection: close\r\n\r\n");
  String line = client.readStringUntil('\n');
  client.stop();
  if (line.indexOf(" 204 ") >= 0) setState(kDeskWifiConnected, "internet verified");
  else if (line.indexOf(" 30") >= 0 || line.indexOf(" 200 ") >= 0)
    setState(kDeskWifiCaptivePortalSuspected, "captive portal suspected");
  else setState(kDeskWifiConnectedNoInternet, "connected without verified internet");
#endif
}
void DeskWifiService::loadProfiles() {
  Preferences prefs;
  if (!prefs.begin(kPreferencesNamespace, true)) return;
  savedCount_ = min(prefs.getUChar("wifi-count", 0), kMaxSaved);
  for (uint8_t i = 0; i < savedCount_; ++i) {
    String suffix = String(i);
    savedSsids_[i] = prefs.getString(("wifi" + suffix + "s").c_str(), "");
    savedPasswords_[i] = prefs.getString(("wifi" + suffix + "p").c_str(), "");
  }
  String legacySsid = prefs.getString("ssid", "");
  String legacyPassword = prefs.getString("wifi-pass", "");
  prefs.end();
  if (savedCount_ == 0 && legacySsid.length()) rememberProfile(legacySsid, legacyPassword);
}
void DeskWifiService::persistProfiles() {
  Preferences prefs;
  if (!prefs.begin(kPreferencesNamespace, false)) return;
  prefs.putUChar("wifi-count", savedCount_);
  for (uint8_t i = 0; i < kMaxSaved; ++i) {
    String suffix = String(i);
    prefs.putString(("wifi" + suffix + "s").c_str(), i < savedCount_ ? savedSsids_[i] : "");
    prefs.putString(("wifi" + suffix + "p").c_str(), i < savedCount_ ? savedPasswords_[i] : "");
  }
  prefs.end();
}
void DeskWifiService::rememberProfile(const String &ssid, const String &password) {
  int8_t existing = -1;
  for (uint8_t i = 0; i < savedCount_; ++i) if (savedSsids_[i] == ssid) existing = i;
  if (existing >= 0) {
    savedPasswords_[existing] = password;
    for (int8_t i = existing; i > 0; --i) {
      savedSsids_[i] = savedSsids_[i - 1];
      savedPasswords_[i] = savedPasswords_[i - 1];
    }
  } else {
    if (savedCount_ < kMaxSaved) ++savedCount_;
    for (int8_t i = savedCount_ - 1; i > 0; --i) {
      savedSsids_[i] = savedSsids_[i - 1];
      savedPasswords_[i] = savedPasswords_[i - 1];
    }
  }
  savedSsids_[0] = ssid;
  savedPasswords_[0] = password;
  persistProfiles();
}
bool DeskWifiService::forget(uint8_t index) {
  if (index >= savedCount_) return false;
  for (uint8_t i = index; i + 1 < savedCount_; ++i) {
    savedSsids_[i] = savedSsids_[i + 1];
    savedPasswords_[i] = savedPasswords_[i + 1];
  }
  --savedCount_;
  persistProfiles();
  return true;
}
DeskWifiState DeskWifiService::state() const { return state_; }
const char *DeskWifiService::stateLabel() const {
  switch (state_) {
    case kDeskWifiDisabled: return "Disabled";
    case kDeskWifiIdle: return "Idle";
    case kDeskWifiScanning: return "Scanning";
    case kDeskWifiNetworksFound: return "Networks found";
    case kDeskWifiConnecting: return "Connecting";
    case kDeskWifiConnected: return "Connected";
    case kDeskWifiConnectedNoInternet: return "No internet";
    case kDeskWifiAuthenticationFailed: return "Authentication failed";
    case kDeskWifiNetworkNotFound: return "Network not found";
    case kDeskWifiCaptivePortalSuspected: return "Captive portal";
    case kDeskWifiDisconnected: return "Disconnected";
    default: return "Error";
  }
}
String DeskWifiService::status() const { return status_; }
bool DeskWifiService::connected() const {
  return state_ == kDeskWifiConnected || state_ == kDeskWifiConnectedNoInternet ||
         state_ == kDeskWifiCaptivePortalSuspected;
}
bool DeskWifiService::internetVerified() const { return state_ == kDeskWifiConnected; }
uint8_t DeskWifiService::networkCount() const { return networkCount_; }
DeskWifiNetwork DeskWifiService::network(uint8_t index) const {
  return index < networkCount_ ? networks_[index] : DeskWifiNetwork{};
}
uint8_t DeskWifiService::savedCount() const { return savedCount_; }
String DeskWifiService::savedSsid(uint8_t index) const { return index < savedCount_ ? savedSsids_[index] : ""; }
bool DeskWifiService::connectSaved(uint8_t index) {
  return index < savedCount_ && connect(savedSsids_[index], savedPasswords_[index], false);
}
String DeskWifiService::activeSsid() const {
#if USE_WIFI
  return connected() ? WiFi.SSID() : pendingSsid_;
#else
  return "";
#endif
}
void DeskWifiService::print(Print &out) const {
  out.print(F("[wifi] state=")); out.print(stateLabel());
  out.print(F(" ssid=")); out.print(activeSsid());
  out.print(F(" saved=")); out.print(savedCount_);
  out.print(F(" visible=")); out.println(networkCount_);
}

void DeskWeatherService::begin(DeskWifiService *wifi, DeskStorageService *storage, DeskEventBus *events) {
  wifi_ = wifi;
  storage_ = storage;
  events_ = events;
#if USE_CYPHER_DESK_WEATHER && USE_WIFI
  Preferences prefs;
  if (prefs.begin(kPreferencesNamespace, true)) {
    configured_ = prefs.getBool("weather-set", false);
    latitude_ = prefs.getFloat("weather-lat", 0.0f);
    longitude_ = prefs.getFloat("weather-lon", 0.0f);
    label_ = prefs.getString("weather-name", "");
    prefs.end();
  }
  status_ = configured_ ? "Ready to refresh" : "Set a location first";
  loadCache();
#else
  (void)wifi_; (void)storage_; (void)events_;
#endif
}

bool DeskWeatherService::setLocation(float latitude, float longitude, const String &label) {
  if (latitude < -90.0f || latitude > 90.0f || longitude < -180.0f || longitude > 180.0f) {
    status_ = "Coordinates out of range";
    return false;
  }
  latitude_ = latitude;
  longitude_ = longitude;
  label_ = label;
  label_.trim();
  if (!label_.length()) label_ = String(latitude_, 4) + ", " + String(longitude_, 4);
  configured_ = true;
  // A cache belongs to its previous coordinate pair. Do not relabel it after
  // the user changes their location.
  valid_ = false;
  cached_ = false;
  Preferences prefs;
  if (prefs.begin(kPreferencesNamespace, false)) {
    prefs.putBool("weather-set", true);
    prefs.putFloat("weather-lat", latitude_);
    prefs.putFloat("weather-lon", longitude_);
    prefs.putString("weather-name", label_);
    prefs.end();
  }
  status_ = "Location saved; refresh when online";
  if (events_ != nullptr) events_->publish(kDeskEventInfo, "Weather location saved");
  return true;
}

bool DeskWeatherService::configured() const { return configured_; }
bool DeskWeatherService::requestRefresh() {
#if !USE_CYPHER_DESK_WEATHER || !USE_WIFI
  status_ = "Weather feature disabled at compile time";
  return false;
#else
  if (!configured_) { status_ = "Set a location first"; return false; }
  if (wifi_ == nullptr || !wifi_->internetVerified()) { status_ = "Waiting for verified internet"; return false; }
  refreshRequested_ = true;
  status_ = "Refresh queued";
  return true;
#endif
}
bool DeskWeatherService::valid() const { return valid_; }
bool DeskWeatherService::cached() const { return cached_; }
const DeskWeatherData &DeskWeatherService::data() const { return data_; }
String DeskWeatherService::locationLabel() const { return label_; }
String DeskWeatherService::status() const { return status_; }

void DeskWeatherService::tick(uint32_t nowMs) {
#if USE_CYPHER_DESK_WEATHER && USE_WIFI
  if (!refreshRequested_) return;
  refreshRequested_ = false;
  if (fetch()) lastRefreshMs_ = nowMs;
#else
  (void)nowMs;
#endif
}

bool DeskWeatherService::fetch() {
#if USE_CYPHER_DESK_WEATHER && USE_WIFI
  status_ = "Refreshing Open-Meteo...";
  String url = String("https://api.open-meteo.com/v1/forecast?latitude=") + String(latitude_, 4) +
      "&longitude=" + String(longitude_, 4) +
      "&current=temperature_2m,apparent_temperature,weather_code,wind_speed_10m" +
      "&daily=temperature_2m_max,temperature_2m_min&wind_speed_unit=kn" +
      "&temperature_unit=celsius&timezone=auto&forecast_days=1";
  WiFiClientSecure client;
  client.setInsecure();  // Matches the existing ADS-B Open-Meteo adapter; no CA bundle is shipped.
  HTTPClient http;
  http.setConnectTimeout(1800);
  http.setTimeout(4500);
  http.useHTTP10(true);
  if (!http.begin(client, url)) { status_ = "Unable to start weather request"; return false; }
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    status_ = String("Weather HTTP ") + code;
    http.end();
    if (events_ != nullptr) events_->publish(kDeskEventInfo, status_);
    return false;
  }
  String body = http.getString();
  http.end();
  JsonDocument filter;
  filter["current"]["temperature_2m"] = true;
  filter["current"]["apparent_temperature"] = true;
  filter["current"]["weather_code"] = true;
  filter["current"]["wind_speed_10m"] = true;
  filter["daily"]["temperature_2m_max"] = true;
  filter["daily"]["temperature_2m_min"] = true;
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, body, DeserializationOption::Filter(filter));
  if (error) { status_ = String("Weather JSON: ") + error.c_str(); return false; }
  int weatherCode = doc["current"]["weather_code"] | -1;
  const char *condition = "Unknown";
  if (weatherCode == 0) condition = "Clear sky";
  else if (weatherCode <= 2) condition = "Partly cloudy";
  else if (weatherCode == 3) condition = "Overcast";
  else if (weatherCode == 45 || weatherCode == 48) condition = "Fog";
  else if (weatherCode >= 51 && weatherCode <= 55) condition = "Drizzle";
  else if (weatherCode >= 61 && weatherCode <= 67) condition = "Rain";
  else if (weatherCode >= 71 && weatherCode <= 77) condition = "Snow";
  else if (weatherCode >= 80 && weatherCode <= 82) condition = "Showers";
  else if (weatherCode >= 95) condition = "Thunderstorm";
  data_.tempC = doc["current"]["temperature_2m"] | NAN;
  data_.feelsC = doc["current"]["apparent_temperature"] | NAN;
  data_.windKt = doc["current"]["wind_speed_10m"] | NAN;
  data_.hiC = doc["daily"]["temperature_2m_max"][0] | NAN;
  data_.loC = doc["daily"]["temperature_2m_min"][0] | NAN;
  data_.condition = condition;
  valid_ = true;
  cached_ = false;
  status_ = "Live weather refreshed";
  saveCache();
  if (events_ != nullptr) events_->publish(kDeskEventInfo, "Weather refreshed: " + data_.condition);
  return true;
#else
  return false;
#endif
}

void DeskWeatherService::loadCache() {
#if USE_CYPHER_DESK_WEATHER
  if (storage_ == nullptr || !storage_->mounted()) return;
  String body = storage_->readText(CYPHER_DESK_CACHE_DIR "/weather.tsv", 1024);
  if (!body.startsWith("# cypher-desk-weather schema=1\n")) return;
  String line = body.substring(body.indexOf('\n') + 1); line.trim();
  int fields[6]; int start = 0;
  for (uint8_t i = 0; i < 6; ++i) { fields[i] = line.indexOf('\t', start); if (fields[i] < 0) return; start = fields[i] + 1; }
  data_.tempC = line.substring(0, fields[0]).toFloat();
  data_.feelsC = line.substring(fields[0] + 1, fields[1]).toFloat();
  data_.windKt = line.substring(fields[1] + 1, fields[2]).toFloat();
  data_.hiC = line.substring(fields[2] + 1, fields[3]).toFloat();
  data_.loC = line.substring(fields[3] + 1, fields[4]).toFloat();
  data_.condition = line.substring(fields[4] + 1, fields[5]);
  label_ = line.substring(fields[5] + 1);
  valid_ = data_.condition.length() > 0;
  cached_ = valid_;
  if (valid_) status_ = "Showing cached weather";
#endif
}
void DeskWeatherService::saveCache() {
#if USE_CYPHER_DESK_WEATHER
  if (storage_ == nullptr || !storage_->mounted()) return;
  storage_->ensureDirectory(CYPHER_DESK_CACHE_DIR);
  String body = "# cypher-desk-weather schema=1\n" + String(data_.tempC, 1) + '\t' +
      String(data_.feelsC, 1) + '\t' + String(data_.windKt, 1) + '\t' + String(data_.hiC, 1) + '\t' +
      String(data_.loC, 1) + '\t' + data_.condition + '\t' + label_ + '\n';
  storage_->atomicWrite(CYPHER_DESK_CACHE_DIR "/weather.tsv", body);
#endif
}
void DeskWeatherService::print(Print &out) const {
  out.print(F("[weather] configured=")); out.print(configured_ ? "yes" : "no");
  out.print(F(" location=")); out.print(label_);
  out.print(F(" state=")); out.print(status_);
  if (valid_) { out.print(F(" temp_c=")); out.print(data_.tempC, 1); out.print(F(" condition=")); out.print(data_.condition); }
  out.println();
}

void DeskTimeService::begin(DeskWifiService *wifi, DeskEventBus *events) {
  wifi_ = wifi;
  events_ = events;
  Preferences prefs;
  if (prefs.begin(kPreferencesNamespace, true)) {
    timezone_ = prefs.getString("timezone", CYPHER_DESK_TIMEZONE);
    prefs.end();
  }
  requestSync();
}
void DeskTimeService::requestSync() { requested_ = true; requestedAtMs_ = millis(); }
bool DeskTimeService::setTimezone(const String &timezoneValue) {
  String value = timezoneValue; value.trim();
  if (!value.length() || value.length() > 63) return false;
  timezone_ = value;
  Preferences prefs;
  if (prefs.begin(kPreferencesNamespace, false)) { prefs.putString("timezone", timezone_); prefs.end(); }
  configured_ = false; synced_ = false; requestSync();
  return true;
}
String DeskTimeService::timezone() const { return timezone_; }
void DeskTimeService::tick(uint32_t nowMs) {
  if (!requested_ || wifi_ == nullptr || !wifi_->connected()) return;
  if (!configured_) {
    configTzTime(timezone_.c_str(), "pool.ntp.org", "time.nist.gov");
    configured_ = true;
  }
  tm value = {};
  if (getLocalTime(&value, 5) && value.tm_year >= 124) {
    synced_ = true;
    requested_ = false;
    if (events_ != nullptr) events_->publish(kDeskEventInfo, "NTP time synchronized");
  } else if (nowMs - requestedAtMs_ > 20000) {
    requested_ = false;
  }
}
String DeskTimeService::timeText() const {
  tm value = {};
  if (!getLocalTime(&value, 5)) return "--:--";
  char output[8];
  strftime(output, sizeof(output), "%H:%M", &value);
  return String(output);
}
String DeskTimeService::dateText() const {
  tm value = {};
  if (!getLocalTime(&value, 5)) return "offline date";
  char output[24];
  strftime(output, sizeof(output), "%Y-%m-%d", &value);
  return String(output);
}
bool DeskTimeService::synced() const { return synced_; }
void DeskAudioService::begin(DeskEventBus *events) {
  events_ = events;
  engine_.begin(Serial);
  engine_.setVolume(volume_);
  keyClick_.begin(Serial);
#if USE_CYPHER_DESK_RECORDER
  engine_.beginMicrophone();
#endif
  testStatus_ = engine_.ready() ? "audio ready" : engine_.status();
}

void DeskAudioService::tick() {
  pumpTone();
  pumpPlayback();
  pumpRecorder();
}

// --- SD -> engine pump -----------------------------------------------------
// This is the only place that reads audio off the card. It runs in loop
// context; the mixer task drains what it queues here and never opens a file.
void DeskAudioService::pumpPlayback() {
#if USE_CYPHER_DESK_SD
  if (!playback_ || paused_) return;
  // Refill in chunks so a single call cannot monopolise the frame. The ring is
  // ~1.5 s deep, so falling behind by a few frames is invisible.
  static uint8_t buffer[2048];
  const uint32_t sourceFrameBytes = playbackChannels_ * (playbackBits_ / 8);
  uint32_t roomFrames = engine_.streamFreeSourceFrames();
  while (roomFrames > 0 && gDeskPlaybackFile) {
    uint32_t wanted = roomFrames * sourceFrameBytes;
    if (wanted > sizeof(buffer)) wanted = sizeof(buffer);
    const uint32_t remaining = playbackDataBytes_ - playbackConsumed_;
    if (wanted > remaining) wanted = remaining;
    if (wanted == 0) break;

    const size_t got = gDeskPlaybackFile.read(buffer, wanted);
    if (got == 0) break;
    const size_t accepted = engine_.pushStream(buffer, got);
    playbackConsumed_ += accepted;
    if (accepted < got) {
      // The ring filled mid-buffer. Rewind the file to the first unconsumed
      // byte so nothing is dropped, and pick it up next tick.
      gDeskPlaybackFile.seek(playbackDataStart_ + playbackConsumed_);
      break;
    }
    if (playbackConsumed_ >= playbackDataBytes_) break;
    roomFrames = engine_.streamFreeSourceFrames();
  }

  if (playbackConsumed_ >= playbackDataBytes_) {
    if (loop_) {
      playbackConsumed_ = 0;
      gDeskPlaybackFile.seek(playbackDataStart_);
    } else {
      // Input is done; let the queue drain before releasing the owner so the
      // last second of a track is actually heard.
      engine_.endStreamInput();
      if (!engine_.streamActive()) stopPlayback();
    }
  }
#endif
}

void DeskAudioService::pumpRecorder() {
#if USE_CYPHER_DESK_RECORDER && USE_CYPHER_DESK_SD
  if (!testRecording_) return;
  uint8_t buffer[512];
  const size_t got = engine_.readMicrophone(buffer, sizeof(buffer));
  if (got) {
    if (gDeskRecordingFile) gDeskRecordingFile.write(buffer, got);
    recordedBytes_ += got;
    const int16_t *samples = reinterpret_cast<const int16_t *>(buffer);
    uint32_t peak = 0;
    for (size_t i = 0; i < got / 2; ++i) {
      const int32_t value = samples[i];
      const uint32_t magnitude = value < 0 ? static_cast<uint32_t>(-value) : static_cast<uint32_t>(value);
      if (magnitude > peak) peak = magnitude;
    }
    lastLevel_ = peak;
  }
  if (testDurationMs_ && millis() - testStartedMs_ >= testDurationMs_) {
    const uint32_t bytes = recordedBytes_;
    stopRecording();
    testStatus_ = String("microphone test complete; ") + bytes + " bytes";
  }
#endif
}

void DeskAudioService::pumpTone() {
  if (!testTone_) return;
  if (testDurationMs_ && millis() - testStartedMs_ >= testDurationMs_) {
    testTone_ = false;
    engine_.endStreamInput();
    owner_ = kDeskAudioOwnerNone;
    testStatus_ = "speaker test complete";
    return;
  }
  // Generate straight into the streaming voice at the output rate, so the test
  // exercises the same path music does rather than a private code route.
  int16_t block[256];
  uint32_t room = engine_.streamFreeSourceFrames();
  while (room >= 256) {
    for (uint16_t i = 0; i < 256; ++i) {
      // 440 Hz square, well below full scale.
      const uint32_t period = DeskAudioEngine::kOutputRate / 440;
      block[i] = ((toneFrame_ + i) % period) < (period / 2) ? 5000 : -5000;
    }
    toneFrame_ += 256;
    const size_t pushed = engine_.pushStream(reinterpret_cast<const uint8_t *>(block), sizeof(block));
    if (pushed < sizeof(block)) break;
    room = engine_.streamFreeSourceFrames();
  }
}

// --- Arbitration -----------------------------------------------------------

bool DeskAudioService::acquire(DeskAudioOwner owner) {
  if (owner_ != kDeskAudioOwnerNone && owner_ != owner) return false;
  owner_ = owner;
  recording_ = owner == kDeskAudioOwnerRecorder && microphoneAvailable();
  return true;
}
void DeskAudioService::release(DeskAudioOwner owner) {
  if (owner_ != owner) return;
  recording_ = false;
  owner_ = kDeskAudioOwnerNone;
}
DeskAudioOwner DeskAudioService::owner() const { return owner_; }
bool DeskAudioService::speakerAvailable() const { return engine_.ready(); }
bool DeskAudioService::microphoneAvailable() const { return engine_.microphoneReady(); }

bool DeskAudioService::startSpeakerTest(uint16_t durationMs) {
  if (!speakerAvailable() || owner_ != kDeskAudioOwnerNone) return false;
  if (!engine_.openStream(DeskAudioEngine::kOutputRate, 1, 16)) return false;
  owner_ = kDeskAudioOwnerMusic;
  testTone_ = true;
  testStartedMs_ = millis();
  toneFrame_ = 0;
  testDurationMs_ = durationMs;
  testStatus_ = "speaker test running";
  return true;
}
bool DeskAudioService::startMicrophoneTest(uint16_t durationMs) {
  if (!startRecording("audio-test")) return false;
  testRecording_ = true;
  testDurationMs_ = durationMs;
  testStatus_ = "microphone test running";
  return true;
}
bool DeskAudioService::startRecording(const String &nameValue) {
  if (!microphoneAvailable() || owner_ != kDeskAudioOwnerNone) return false;
#if USE_CYPHER_DESK_SD
  if (SD_MMC.cardType() == CARD_NONE) { testStatus_ = "insert SD card for recording"; return false; }
  String name = nameValue;
  name.trim();
  if (!name.length()) name = String("recording-") + millis();
  name.replace("/", "-"); name.replace("\\", "-"); name.replace(" ", "-");
  if (!name.endsWith(".wav")) name += ".wav";
  recordingPath_ = String(CYPHER_DESK_RECORDINGS_DIR) + "/" + name;
  gDeskRecordingFile = SD_MMC.open(recordingPath_, FILE_WRITE);
  if (!gDeskRecordingFile) { testStatus_ = "recording file open failed"; recordingPath_ = ""; return false; }
  uint8_t blank[44] = {}; gDeskRecordingFile.write(blank, sizeof(blank));
#else
  (void)nameValue;
  testStatus_ = "SD required for recording"; return false;
#endif
  owner_ = kDeskAudioOwnerRecorder; recording_ = true; testRecording_ = true;
  testStartedMs_ = millis(); testDurationMs_ = 0; recordedBytes_ = 0; lastLevel_ = 0;
  testStatus_ = "recording to SD"; return true;
}
bool DeskAudioService::stopRecording() {
  if (!recording_) return false;
  testRecording_ = false;
#if USE_CYPHER_DESK_SD
  if (gDeskRecordingFile) { writeWavHeader(gDeskRecordingFile, recordedBytes_); gDeskRecordingFile.close(); }
#endif
  recording_ = false;
  owner_ = kDeskAudioOwnerNone;
  testStatus_ = String("recording saved; ") + recordedBytes_ + " bytes";
  return true;
}

// --- Playback --------------------------------------------------------------

bool DeskAudioService::playWav(const String &path, DeskAudioOwner owner, bool loop) {
  if (!speakerAvailable()) { testStatus_ = "speaker unavailable"; return false; }
  if (owner_ != kDeskAudioOwnerNone && owner_ != owner) { testStatus_ = "audio in use"; return false; }
#if USE_CYPHER_DESK_SD
  if (SD_MMC.cardType() == CARD_NONE) { testStatus_ = "insert SD card for playback"; return false; }
  stopPlayback();
  gDeskPlaybackFile = SD_MMC.open(path, FILE_READ);
  if (!gDeskPlaybackFile) { testStatus_ = "WAV open failed"; return false; }

  DeskWavFileSource source(gDeskPlaybackFile);
  DeskWavFormat format;
  String reason;
  if (!DeskWav::parse(source, format, reason)) {
    gDeskPlaybackFile.close();
    testStatus_ = reason;
    return false;
  }
  if (!engine_.openStream(format.sampleRate, format.channels, format.bitsPerSample)) {
    gDeskPlaybackFile.close();
    testStatus_ = "mixer refused this format";
    return false;
  }
  playbackDataStart_ = format.dataStart;
  playbackDataBytes_ = format.dataBytes;
  playbackConsumed_ = 0;
  playbackRate_ = format.sampleRate;
  playbackChannels_ = format.channels;
  playbackBits_ = format.bitsPerSample;
  playbackFormat_ = format.describe();
  playbackPath_ = path;
  loop_ = loop;
  paused_ = false;
  playback_ = true;
  owner_ = owner;
  testStatus_ = String("playing ") + playbackFormat_;
  return true;
#else
  (void)path; (void)owner; (void)loop;
  testStatus_ = "SD required for playback"; return false;
#endif
}

void DeskAudioService::stopPlayback() {
  if (!playback_) return;
#if USE_CYPHER_DESK_SD
  if (gDeskPlaybackFile) gDeskPlaybackFile.close();
#endif
  engine_.closeStream();
  playback_ = false;
  paused_ = false;
  loop_ = false;
  playbackConsumed_ = 0;
  playbackDataBytes_ = 0;
  if (owner_ != kDeskAudioOwnerRecorder) owner_ = kDeskAudioOwnerNone;
  testStatus_ = "playback stopped";
}

bool DeskAudioService::playing() const { return playback_ && !paused_; }
bool DeskAudioService::paused() const { return paused_; }
void DeskAudioService::setPaused(bool paused) {
  if (!playback_) return;
  paused_ = paused;
  // Leaving the queue in place means resume is instant; the mixer simply runs
  // dry and emits silence while paused.
  if (paused) engine_.closeStream();
#if USE_CYPHER_DESK_SD
  if (!paused && engine_.openStream(playbackRate_, playbackChannels_, playbackBits_)) {
    gDeskPlaybackFile.seek(playbackDataStart_ + playbackConsumed_);
  }
#endif
}

bool DeskAudioService::seekMs(uint32_t positionMs) {
#if USE_CYPHER_DESK_SD
  if (!playback_ || playbackRate_ == 0) return false;
  const uint32_t frameBytes = playbackChannels_ * (playbackBits_ / 8);
  uint64_t byteOffset = (static_cast<uint64_t>(positionMs) * playbackRate_ / 1000ULL) * frameBytes;
  if (byteOffset > playbackDataBytes_) byteOffset = playbackDataBytes_;
  byteOffset -= byteOffset % frameBytes;
  engine_.closeStream();
  if (!engine_.openStream(playbackRate_, playbackChannels_, playbackBits_)) return false;
  playbackConsumed_ = static_cast<uint32_t>(byteOffset);
  return gDeskPlaybackFile.seek(playbackDataStart_ + playbackConsumed_);
#else
  (void)positionMs;
  return false;
#endif
}

String DeskAudioService::playbackPath() const { return playbackPath_; }
String DeskAudioService::playbackFormat() const { return playbackFormat_; }
uint32_t DeskAudioService::playbackDurationMs() const {
  if (playbackRate_ == 0 || playbackChannels_ == 0) return 0;
  const uint32_t frameBytes = playbackChannels_ * (playbackBits_ / 8);
  return static_cast<uint32_t>((static_cast<uint64_t>(playbackDataBytes_ / frameBytes) * 1000ULL) /
                               playbackRate_);
}
uint32_t DeskAudioService::playbackPositionMs() const {
  if (playbackRate_ == 0 || playbackChannels_ == 0) return 0;
  const uint32_t frameBytes = playbackChannels_ * (playbackBits_ / 8);
  // Report what has been HEARD, not what has been read off the card - the ring
  // holds up to 1.5 s, so a read-based progress bar would run ahead of the
  // music by a visible margin.
  const uint32_t queued = engine_.streamQueuedFrames();
  uint32_t readFrames = playbackConsumed_ / frameBytes;
  const uint32_t queuedSourceFrames =
      static_cast<uint32_t>((static_cast<uint64_t>(queued) * playbackRate_) /
                            DeskAudioEngine::kOutputRate);
  readFrames = readFrames > queuedSourceFrames ? readFrames - queuedSourceFrames : 0;
  return static_cast<uint32_t>((static_cast<uint64_t>(readFrames) * 1000ULL) / playbackRate_);
}

// --- Raw stream (video audio) ---------------------------------------------

bool DeskAudioService::openRawStream(uint32_t sampleRate, uint16_t channels, uint16_t bits,
                                     DeskAudioOwner owner) {
  if (!speakerAvailable()) return false;
  if (owner_ != kDeskAudioOwnerNone && owner_ != owner) return false;
  stopPlayback();
  if (!engine_.openStream(sampleRate, channels, bits)) return false;
  owner_ = owner;
  playbackRate_ = sampleRate;
  playbackChannels_ = channels;
  playbackBits_ = bits;
  playbackFormat_ = String(sampleRate / 1000) + " kHz " + (channels == 1 ? "mono" : "stereo");
  return true;
}
size_t DeskAudioService::pushRaw(const uint8_t *bytes, size_t length) {
  return engine_.pushStream(bytes, length);
}
uint32_t DeskAudioService::rawFreeFrames() const { return engine_.streamFreeSourceFrames(); }
uint64_t DeskAudioService::streamPlayedFrames() const { return engine_.streamPlayedFrames(); }
uint32_t DeskAudioService::underruns() const { return engine_.streamUnderruns(); }

// --- Typing sounds ---------------------------------------------------------

void DeskAudioService::setKeySound(uint8_t sound) { keyClick_.setSound(sound); }
uint8_t DeskAudioService::keySound() const { return keyClick_.sound(); }
const char *DeskAudioService::keySoundName() const { return keyClick_.soundName(); }
void DeskAudioService::keyPress() { keyClick_.press(engine_, volume_); }
void DeskAudioService::keyRelease() { keyClick_.release(engine_, volume_); }

// --- Writer ambience -------------------------------------------------------

const char *DeskAudioService::ambienceName(uint8_t ambience) {
  static const char *const kNames[kAmbienceCount] = {"Off", "Rainy Cafe", "Vinyl Room", "Fireplace",
                                                     "Brown Noise"};
  return kNames[ambience < kAmbienceCount ? ambience : 0];
}
const char *DeskAudioService::ambienceName() const { return ambienceName(ambience_); }
uint8_t DeskAudioService::ambience() const { return ambience_; }

bool DeskAudioService::setAmbience(uint8_t ambience) {
  static const char *const kFiles[kAmbienceCount] = {"", "rainy-cafe.wav", "vinyl-room.wav",
                                                     "fireplace.wav", "brown-noise.wav"};
  ambience_ = ambience < kAmbienceCount ? ambience : 0;
  if (owner_ == kDeskAudioOwnerWriter) stopPlayback();
  if (ambience_ == 0) return true;
  const String path = String(CYPHER_DESK_AUDIO_DIR) + "/" + kFiles[ambience_];
  if (!playWav(path, kDeskAudioOwnerWriter, /*loop=*/true)) {
    // Say which loop is missing rather than silently reverting to Off.
    testStatus_ = String(ambienceName(ambience_)) + ": " + testStatus_;
    ambience_ = 0;
    return false;
  }
  return true;
}

// --- Reporting -------------------------------------------------------------

String DeskAudioService::recordingPath() const { return recordingPath_; }
uint32_t DeskAudioService::recordingDurationMs() const { return recording_ ? millis() - testStartedMs_ : 0; }
void DeskAudioService::setVolume(uint8_t volume) {
  volume_ = volume > 100 ? 100 : volume;
  engine_.setVolume(volume_);
}
uint8_t DeskAudioService::volume() const { return volume_; }
String DeskAudioService::testStatus() const { return testStatus_; }
uint32_t DeskAudioService::inputLevel() const { return lastLevel_; }
bool DeskAudioService::recording() const { return recording_; }
String DeskAudioService::status() const {
  if (recording_) return "recording";
  if (!speakerAvailable() && !microphoneAvailable()) return engine_.status();
  if (playback_) return paused_ ? "paused" : String("playing ") + playbackFormat_;
  return owner_ == kDeskAudioOwnerNone ? "idle" : "audio in use";
}
void DeskAudioService::print(Print &out) const {
  out.print(F("[audio] owner=")); out.print(owner_);
  out.print(F(" speaker=")); out.print(speakerAvailable() ? "ready" : "unavailable");
  out.print(F(" microphone=")); out.print(microphoneAvailable() ? "ready" : "unavailable");
  out.print(F(" engine=")); out.print(engine_.status());
  out.print(F(" key_sound=")); out.print(keySoundName());
  out.print(F(" ambience=")); out.print(ambienceName());
  out.print(F(" volume=")); out.print(volume_);
  out.print(F(" underruns=")); out.print(engine_.streamUnderruns());
  out.print(F(" level=")); out.print(lastLevel_);
  out.print(F(" test=")); out.print(testStatus_);
  out.print(F(" status=")); out.println(status());
}
