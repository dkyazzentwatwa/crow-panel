#ifndef CYPHER_DESK_SYSTEM_SERVICES_H
#define CYPHER_DESK_SYSTEM_SERVICES_H

#include "../config/ProjectConfig.h"
#include "DeskAudioEngine.h"
#include "DeskEventBus.h"
#include "DeskKeyClick.h"
#include "DeskOsTypes.h"
#include <Arduino.h>

class DeskDisplayService {
 public:
  bool begin();
  void tick();
  bool ready() const;
  class Arduino_GFX *canvas() const;
};

class DeskTouchService {
 public:
  void tick();
  bool take(DeskTouchEvent &event);
  uint32_t count() const;

 private:
  bool wasPressed_ = false;
  bool pending_ = false;
  int16_t lastX_ = 0;
  int16_t lastY_ = 0;
  uint32_t count_ = 0;
  uint32_t lastPollMs_ = 0;
  DeskTouchEvent event_;
  int16_t mapX(int16_t rawX, int16_t rawY) const;
  int16_t mapY(int16_t rawX, int16_t rawY) const;
};

class DeskStorageService {
 public:
  struct FileEntry {
    String path;
    String name;
    bool directory = false;
    uint32_t size = 0;
  };

  void begin(DeskEventBus *events);
  void tick(uint32_t nowMs);
  DeskSdState state() const;
  const char *stateLabel() const;
  String status() const;
  bool mounted() const;
  uint64_t totalBytes() const;
  uint64_t freeBytes() const;
  uint8_t freePercent() const;
  const char *cardLabel() const;
  bool lowSpace() const;
  // Bumps every time the card transitions from unmounted to mounted, so views
  // that cached SD-backed data can tell they need to reload.
  uint32_t mountGeneration() const;
  bool safeEject();
  bool remount();
  bool ensureDirectory(const String &path);
  bool renamePath(const String &source, const String &destination);
  String readText(const String &path, size_t maxLength = 64000) const;
  bool atomicWrite(const String &path, const String &body);
  bool copyFile(const String &source, const String &destination);
  bool moveFile(const String &source, const String &destination);
  bool removeFile(const String &path);
  uint16_t countFiles(const String &directory, const char *extension = nullptr) const;
  uint8_t listFileNames(const String &directory, String *out, uint8_t maxCount) const;
  uint8_t listDirectory(const String &directory, FileEntry *out, uint8_t maxCount) const;
  bool pathProtected(const String &path) const;
  bool textFile(const String &path) const;
  uint8_t recoverTransactions();
  void print(Print &out) const;

 private:
  DeskEventBus *events_ = nullptr;
  DeskSdState state_ = kDeskSdNotPresent;
  uint32_t lastPollMs_ = 0;
  uint32_t mountGeneration_ = 0;
  bool deliberatelyEjected_ = false;
  bool mountCard();
  void setState(DeskSdState state, const String &reason);
};

class DeskWifiService {
 public:
  static constexpr uint8_t kMaxNetworks = 12;
  static constexpr uint8_t kMaxSaved = 5;

  void begin(DeskEventBus *events);
  void tick(uint32_t nowMs);
  void setOffline(bool offline);
  bool offline() const;
  void scan();
  bool connect(const String &ssid, const String &password, bool saveProfile = true);
  void disconnect();
  bool forget(uint8_t index);
  DeskWifiState state() const;
  const char *stateLabel() const;
  String status() const;
  bool connected() const;
  bool internetVerified() const;
  uint8_t networkCount() const;
  DeskWifiNetwork network(uint8_t index) const;
  uint8_t savedCount() const;
  String savedSsid(uint8_t index) const;
  bool connectSaved(uint8_t index);
  String activeSsid() const;
  void print(Print &out) const;

 private:
  DeskEventBus *events_ = nullptr;
  DeskWifiState state_ = kDeskWifiDisabled;
  DeskWifiNetwork networks_[kMaxNetworks];
  String savedSsids_[kMaxSaved];
  String savedPasswords_[kMaxSaved];
  uint8_t networkCount_ = 0;
  uint8_t savedCount_ = 0;
  String pendingSsid_;
  String pendingPassword_;
  String status_ = "Wi-Fi disabled at compile time";
  uint32_t stateStartedMs_ = 0;
  bool offline_ = false;
  bool savePending_ = false;
  bool connectivityChecked_ = false;
  void setState(DeskWifiState state, const String &status);
  void loadProfiles();
  void persistProfiles();
  void rememberProfile(const String &ssid, const String &password);
  void runConnectivityCheck();
};

class DeskTimeService {
 public:
  void begin(DeskWifiService *wifi, DeskEventBus *events);
  void tick(uint32_t nowMs);
  String timeText() const;
  String dateText() const;
  bool synced() const;
  void requestSync();
  bool setTimezone(const String &timezone);
  String timezone() const;

 private:
  DeskWifiService *wifi_ = nullptr;
  DeskEventBus *events_ = nullptr;
  bool requested_ = false;
  bool configured_ = false;
  bool synced_ = false;
  uint32_t requestedAtMs_ = 0;
  String timezone_ = CYPHER_DESK_TIMEZONE;
};

struct DeskWeatherData {
  float tempC = NAN;
  float feelsC = NAN;
  float windKt = NAN;
  float hiC = NAN;
  float loC = NAN;
  String condition;
};

class DeskWeatherService {
 public:
  void begin(DeskWifiService *wifi, DeskStorageService *storage, DeskEventBus *events);
  void tick(uint32_t nowMs);
  bool setLocation(float latitude, float longitude, const String &label);
  bool configured() const;
  bool requestRefresh();
  bool valid() const;
  bool cached() const;
  const DeskWeatherData &data() const;
  String locationLabel() const;
  String status() const;
  void print(Print &out) const;

 private:
  DeskWifiService *wifi_ = nullptr;
  DeskStorageService *storage_ = nullptr;
  DeskEventBus *events_ = nullptr;
  DeskWeatherData data_;
  float latitude_ = 0.0f;
  float longitude_ = 0.0f;
  String label_;
  String status_ = "Weather disabled at compile time";
  bool configured_ = false;
  bool valid_ = false;
  bool cached_ = false;
  bool refreshRequested_ = false;
  uint32_t lastRefreshMs_ = 0;
  bool fetch();
  void loadCache();
  void saveCache();
};

enum DeskAudioOwner : uint8_t {
  kDeskAudioOwnerNone,
  kDeskAudioOwnerWriter,
  kDeskAudioOwnerMusic,
  kDeskAudioOwnerRecorder
};

// Owns the arbitration and the SD side of audio. All I2S lives one level down
// in DeskAudioEngine, which is the single owner of the peripheral.
//
// THREADING: every method here runs in loop context and is the only thing that
// touches the card. The engine's mixer task never opens a file.
class DeskAudioService {
 public:
  void begin(DeskEventBus *events);
  void tick();
  bool acquire(DeskAudioOwner owner);
  void release(DeskAudioOwner owner);
  DeskAudioOwner owner() const;
  bool speakerAvailable() const;
  bool microphoneAvailable() const;
  bool startSpeakerTest(uint16_t durationMs = 3000);
  bool startMicrophoneTest(uint16_t durationMs = 5000);
  bool startRecording(const String &name = "");
  bool stopRecording();

  // Any PCM WAV the reader admits: 8 kHz to 48 kHz, mono or stereo, 8- or
  // 16-bit. The mixer resamples, so nothing is rejected for being the wrong
  // shape any more.
  bool playWav(const String &path, DeskAudioOwner owner = kDeskAudioOwnerMusic,
               bool loop = false);
  void stopPlayback();
  bool playing() const;
  bool paused() const;
  void setPaused(bool paused);
  bool seekMs(uint32_t positionMs);
  String playbackPath() const;
  String playbackFormat() const;
  uint32_t playbackDurationMs() const;
  uint32_t playbackPositionMs() const;

  // Raw PCM from a source that is not a file on its own - the video player's
  // interleaved audio chunks. Returns bytes accepted; a short return means the
  // ring is full and the caller should retry with the remainder.
  bool openRawStream(uint32_t sampleRate, uint16_t channels, uint16_t bits,
                     DeskAudioOwner owner);
  size_t pushRaw(const uint8_t *bytes, size_t length);
  uint32_t rawFreeFrames() const;
  // Output frames of the current stream already heard. The video clock.
  uint64_t streamPlayedFrames() const;
  uint32_t underruns() const;

  // Typing sounds.
  void setKeySound(uint8_t sound);
  uint8_t keySound() const;
  const char *keySoundName() const;
  void keyPress();
  void keyRelease();

  // Writer ambience: one of the looping WAVs under CYPHER_DESK_AUDIO_DIR.
  bool setAmbience(uint8_t ambience);
  uint8_t ambience() const;
  const char *ambienceName() const;
  static const char *ambienceName(uint8_t ambience);
  static constexpr uint8_t kAmbienceCount = 5;

  String recordingPath() const;
  uint32_t recordingDurationMs() const;
  void setVolume(uint8_t volume);
  uint8_t volume() const;
  String testStatus() const;
  uint32_t inputLevel() const;
  bool recording() const;
  String status() const;
  void print(Print &out) const;

 private:
  DeskEventBus *events_ = nullptr;
  DeskAudioEngine engine_;
  DeskKeyClick keyClick_;
  DeskAudioOwner owner_ = kDeskAudioOwnerNone;
  bool recording_ = false;
  bool testRecording_ = false;
  bool testTone_ = false;
  bool playback_ = false;
  bool paused_ = false;
  bool loop_ = false;
  uint32_t testStartedMs_ = 0;
  uint32_t testDurationMs_ = 0;
  uint32_t toneFrame_ = 0;
  uint32_t recordedBytes_ = 0;
  uint32_t playbackDataStart_ = 0;
  uint32_t playbackDataBytes_ = 0;
  uint32_t playbackConsumed_ = 0;
  uint32_t playbackRate_ = 0;
  uint16_t playbackChannels_ = 0;
  uint16_t playbackBits_ = 0;
  uint8_t ambience_ = 0;
  uint8_t volume_ = 70;
  uint32_t lastLevel_ = 0;
  String recordingPath_;
  String playbackPath_;
  String playbackFormat_;
  String testStatus_ = "not started";

  void pumpPlayback();
  void pumpRecorder();
  void pumpTone();
};

#endif
