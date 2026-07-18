#include "RfLabSessionStore.h"
#include "../config/ProjectConfig.h"

#if USE_RF_LAB_PERSISTENCE
#include <FFat.h>
#endif

namespace {
constexpr const char *kSummaryPath = "/rflab-last-session.csv";
constexpr const char *kTempPath = "/rflab-last-session.tmp";
}

bool RfLabSessionStore::begin(String &status) {
#if USE_RF_LAB_PERSISTENCE
  ready_ = FFat.begin(true);
  status = ready_ ? "FFat summary store ready" : "FFat unavailable; RAM only";
#else
  ready_ = false;
  status = "persistence disabled; RAM only";
#endif
  return ready_;
}

bool RfLabSessionStore::save(const RfLabState &state, String &status) {
#if USE_RF_LAB_PERSISTENCE
  if (!ready_) {
    status = "FFat not ready";
    return false;
  }
  File file = FFat.open(kTempPath, FILE_WRITE, true);
  if (!file) {
    status = "could not open summary temp file";
    return false;
  }
  uint32_t durationMs = state.sessionStartedMs ? millis() - state.sessionStartedMs : 0;
  file.println(F("profile,duration_ms,nrf_samples,nrf_hits,cc_samples,cc_hits,gdo_transitions,cc_min_dbm,cc_max_dbm"));
  file.printf("%s,%lu,%lu,%lu,%lu,%lu,%lu,%d,%d\n", RF_LAB_PROFILE_NAME,
              (unsigned long)durationMs, (unsigned long)state.nrfSamples,
              (unsigned long)state.nrfActivityHits, (unsigned long)state.ccSamples,
              (unsigned long)state.ccActivityHits, (unsigned long)state.gdoTransitions,
              state.ccMinRssiDbm, state.ccMaxRssiDbm);
  file.close();
  FFat.remove(kSummaryPath);
  if (!FFat.rename(kTempPath, kSummaryPath)) {
    status = "could not replace session summary";
    return false;
  }
  status = "aggregate session summary saved";
  return true;
#else
  (void)state;
  status = "persistence disabled";
  return false;
#endif
}

bool RfLabSessionStore::clear(String &status) {
#if USE_RF_LAB_PERSISTENCE
  if (ready_ && FFat.exists(kSummaryPath)) FFat.remove(kSummaryPath);
  status = ready_ ? "saved summary cleared" : "FFat not ready";
  return ready_;
#else
  status = "persistence disabled";
  return false;
#endif
}
