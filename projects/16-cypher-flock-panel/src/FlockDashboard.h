#ifndef CYPHER_FLOCK_DASHBOARD_H
#define CYPHER_FLOCK_DASHBOARD_H

#include "FlockDetectionStore.h"
#include "FlockC6Witness.h"

enum FlockUiAction : uint8_t {
  kFlockUiNone = 0,
  kFlockUiPauseToggle,
  kFlockUiModeCycle,
  kFlockUiBandCycle,
  kFlockUiProfileCycle,
  kFlockUiChannelNext,
  kFlockUiDiagnosticsToggle,
  kFlockUiCalibrationToggle,
  kFlockUiStealthToggle,
  kFlockUiSave,
  kFlockUiWitnessRefresh,
  kFlockUiReset
};

struct FlockUiEvent {
  FlockUiAction action = kFlockUiNone;
};

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
#include <CrowPanelShared.h>
#endif

class FlockDashboard {
 public:
  void begin();
  void setBanner(const String &banner);
  void setScreen(FlockScreen screen);
  void nextScreen();
  FlockScreen screen() const { return screen_; }
  void setFilter(FlockFilter filter);
  FlockFilter filter() const { return filter_; }
  void setPrevious(bool previous);
  bool showingPrevious() const { return showingPrevious_; }
  void setStealth(bool enabled);
  bool stealth() const { return stealth_; }
  void requestRepaint();
  bool tick(const FlockDetectionStore &current, const FlockDetectionStore &previous,
            const FlockBridgeStatus &bridge, const FlockLifetimeStats &lifetime,
            const FlockC6Witness &witness,
            bool persistenceReady, FlockUiEvent &event);

 private:
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  void handleTouch_(FlockUiEvent &event);
  bool handleTouchAt_(int16_t x, int16_t y, FlockUiEvent &event);
  void drawFull_(const FlockDetectionStore &store, const FlockBridgeStatus &bridge,
                 const FlockLifetimeStats &lifetime, const FlockC6Witness &witness,
                 bool persistenceReady);
  void drawHeader_(const FlockBridgeStatus &bridge);
  void drawTabs_();
  void drawScope_(const FlockDetectionStore &store);
  void drawFeed_(const FlockDetectionStore &store);
  void drawWitness_(const FlockC6Witness &witness);
  void drawStats_(const FlockDetectionStore &store, const FlockBridgeStatus &bridge,
                  const FlockLifetimeStats &lifetime, bool persistenceReady);
  void drawControl_(const FlockBridgeStatus &bridge, bool persistenceReady);
  void drawFooter_(const FlockDetectionStore &store);
  void drawAlert_(const FlockDetectionStore &store);
  const FlockDetectionStore &activeStore_(const FlockDetectionStore &current,
                                          const FlockDetectionStore &previous) const;

  bool ready_ = false;
  bool dirty_ = true;
  bool wasTouched_ = false;
  uint32_t lastTouchMs_ = 0;
  uint16_t lastCount_ = 0;
  uint32_t lastWitnessGeneration_ = 0;
  bool lastWitnessScanning_ = false;
  uint32_t alertUntilMs_ = 0;
  Throttle refreshGate_{250};
#endif
  FlockScreen screen_ = kFlockScopeScreen;
  FlockFilter filter_ = kFlockFilterAll;
  FlockSort sort_ = kFlockSortRecent;
  uint8_t witnessPage_ = 0;
  bool showingPrevious_ = false;
  bool stealth_ = false;
  String banner_ = "detector dashboard ready";
};

#endif
