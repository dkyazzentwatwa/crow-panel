#include "config/ProjectConfig.h"
#include <CrowPanelShared.h>
#include <SPI.h>

#include "src/C6RadioMonitor.h"
#include "src/Cc1101Probe.h"
#include "src/Nrf24Probe.h"
#include "src/RfLabDashboard.h"
#include "src/RfLabRadioBus.h"
#include "src/RfLabSessionStore.h"
#include "src/RfLabTypes.h"

// Project 17: LittleHakr RF Lab.
// Hardware capability stays intentionally narrower than the reference projects:
// no TX strobe, no payload reads, no packet/identity data, and no runtime
// profile editing. The panel only records aggregate authorized-lab activity.

SPIClass radioSpi(FSPI);
RfLabRadioBus radioBus(radioSpi);
Nrf24Probe nrf24(radioBus);
Cc1101Probe cc1101(radioBus);
RfLabDashboard dashboard;
RfLabSessionStore sessions;
C6RadioMonitor c6Radio;
SerialCommandRouter router;
RfLabState lab;
String storageStatus;
uint32_t lastSampleMs = 0;
uint32_t lastHeartbeatMs = 0;

void updateProof() {
  if (!lab.spiReady) lab.proof = kRfLabError;
  else if (lab.nrfDetected && lab.ccDetected) lab.proof = kRfLabBothDetected;
  else if (lab.nrfDetected) lab.proof = kRfLabNrfDetected;
  else if (lab.ccDetected) lab.proof = kRfLabCcDetected;
  else lab.proof = kRfLabSpiReady;
}

void refreshGdo() {
  bool oldGdo0 = lab.gdo0High;
  bool oldGdo2 = lab.gdo2High;
  lab.gdo0High = digitalRead(RF_LAB_CC1101_GDO0) == HIGH;
  lab.gdo2High = digitalRead(RF_LAB_CC1101_GDO2) == HIGH;
  if (oldGdo0 != lab.gdo0High) ++lab.gdoTransitions;
  if (oldGdo2 != lab.gdo2High) ++lab.gdoTransitions;
}

void refreshProbe(const char *reason) {
  if (lab.detectorRunning) {
    nrf24.stop();
    cc1101.stop();
    lab.detectorRunning = false;
  }
  lab.spiReady = radioBus.begin();
  lab.nrfDetected = nrf24.detect(lab.nrfStatus);
  lab.ccDetected = cc1101.detect(lab.ccPartnum, lab.ccVersion);
  refreshGdo();
  updateProof();
  Serial.printf("[probe] reason=%s spi=%s nrf=%s status=0x%02X cc=%s part=0x%02X ver=0x%02X gdo0=%d gdo2=%d proof=%s\n",
                reason, lab.spiReady ? "ready" : "error", lab.nrfDetected ? "yes" : "no",
                lab.nrfStatus, lab.ccDetected ? "yes" : "no", lab.ccPartnum, lab.ccVersion,
                lab.gdo0High, lab.gdo2High, rfLabProofLabel(lab.proof));
  dashboard.setBanner(String("register proof: ") + rfLabProofLabel(lab.proof));
}

void resetSessionCounters() {
  lab.nrfRpd = 0;
  lab.ccRssiDbm = -127;
  lab.ccMinRssiDbm = 127;
  lab.ccMaxRssiDbm = -127;
  lab.nrfSamples = 0;
  lab.nrfActivityHits = 0;
  lab.ccSamples = 0;
  lab.ccActivityHits = 0;
  lab.gdoTransitions = 0;
  lab.sessionStartedMs = millis();
}

void startDetector(const char *source) {
#if USE_RF_LAB_DETECTOR && RF_LAB_PROFILE_CONFIRMED
  if (!lab.nrfDetected && !lab.ccDetected) {
    dashboard.setBanner("detector refused: radios missing");
    Serial.println(F("[detector] refused: no detected radio"));
    return;
  }
  resetSessionCounters();
  bool nrfReady = !lab.nrfDetected || nrf24.startReceiveOnly();
  bool ccReady = !lab.ccDetected || cc1101.startReceiveOnly();
  lab.detectorAuthorized = true;
  lab.detectorRunning = nrfReady && ccReady;
  lastSampleMs = 0;
  dashboard.setBanner(lab.detectorRunning ? "authorized receive-only detector running"
                                           : "detector setup failed");
  Serial.printf("[detector] source=%s authorized=yes profile=%s running=%s nrf=%s cc=%s tx=disabled\n",
                source, RF_LAB_PROFILE_NAME, lab.detectorRunning ? "yes" : "no",
                nrfReady ? "ready" : "fail", ccReady ? "ready" : "fail");
#else
  (void)source;
  lab.detectorAuthorized = false;
  lab.detectorRunning = false;
  dashboard.setBanner("detector locked: confirm local LabProfile first");
  Serial.println(F("[detector] locked: build with USE_RF_LAB_DETECTOR=1 and local confirmed LabProfile.h"));
#endif
}

void pauseDetector(const char *source) {
  nrf24.stop();
  cc1101.stop();
  lab.detectorRunning = false;
  dashboard.setBanner("receive-only detector paused");
  Serial.printf("[detector] source=%s paused tx=disabled\n", source);
}

void sampleDetector() {
  if (!lab.detectorRunning || millis() - lastSampleMs < RF_LAB_SAMPLE_INTERVAL_MS) return;
  lastSampleMs = millis();
  if (lab.nrfDetected && nrf24.sampleActivity(lab.nrfRpd)) {
    ++lab.nrfSamples;
    if (lab.nrfRpd) ++lab.nrfActivityHits;
  }
  bool gdo0 = false;
  bool gdo2 = false;
  if (lab.ccDetected && cc1101.sampleActivity(lab.ccRssiDbm, gdo0, gdo2)) {
    bool oldGdo0 = lab.gdo0High;
    bool oldGdo2 = lab.gdo2High;
    lab.gdo0High = gdo0;
    lab.gdo2High = gdo2;
    ++lab.ccSamples;
    if (gdo0 || gdo2) ++lab.ccActivityHits;
    if (oldGdo0 != gdo0) ++lab.gdoTransitions;
    if (oldGdo2 != gdo2) ++lab.gdoTransitions;
    lab.ccMinRssiDbm = min(lab.ccMinRssiDbm, lab.ccRssiDbm);
    lab.ccMaxRssiDbm = max(lab.ccMaxRssiDbm, lab.ccRssiDbm);
  }
}

void printStatus(const String &) {
  printSystemStatus(Serial, "littlehakr-rf-lab", 0);
  Serial.printf("[rflab] proof=%s spi=%s nrf=%s status=0x%02X nrf_samples=%lu nrf_hits=%lu cc=%s part=0x%02X ver=0x%02X cc_samples=%lu cc_hits=%lu rssi=%d gdo0=%d gdo2=%d transitions=%lu detector=%s authorized=%s persistence=%s\n",
                rfLabProofLabel(lab.proof), lab.spiReady ? "ready" : "error",
                lab.nrfDetected ? "yes" : "no", lab.nrfStatus,
                (unsigned long)lab.nrfSamples, (unsigned long)lab.nrfActivityHits,
                lab.ccDetected ? "yes" : "no", lab.ccPartnum, lab.ccVersion,
                (unsigned long)lab.ccSamples, (unsigned long)lab.ccActivityHits,
                lab.ccRssiDbm, lab.gdo0High, lab.gdo2High,
                (unsigned long)lab.gdoTransitions, lab.detectorRunning ? "running" : "paused",
                lab.detectorAuthorized ? "yes" : "no", sessions.ready() ? "ready" : "disabled");
  const C6RadioSnapshot &c6 = c6Radio.snapshot();
  Serial.printf("[c6] wifi_enabled=%s wifi_ready=%s wifi_scanning=%s wifi_networks=%u wifi_strongest_rssi=%d wifi_scans=%lu wifi_status=%s ble_enabled=%s ble_available=%s ble_reports=%lu ble_status=%s\n",
                c6.wifiEnabled ? "yes" : "no", c6.wifiReady ? "yes" : "no",
                c6.wifiScanning ? "yes" : "no", c6.wifiNetworks, c6.wifiStrongestRssi,
                (unsigned long)c6.wifiScans, c6.wifiStatus, c6.bleEnabled ? "yes" : "no",
                c6.bleAvailable ? "yes" : "no", (unsigned long)c6.bleReports, c6.bleStatus);
}

void cmdProbe(const String &) { refreshProbe("serial"); }
void cmdStart(const String &) { startDetector("serial"); }
void cmdPause(const String &) { pauseDetector("serial"); }

void cmdSave(const String &) {
  if (sessions.save(lab, storageStatus)) dashboard.setBanner(storageStatus);
  else dashboard.setBanner(storageStatus);
  Serial.println(String("[session] ") + storageStatus);
}

void cmdClear(const String &) {
  resetSessionCounters();
  sessions.clear(storageStatus);
  dashboard.setBanner(storageStatus);
  Serial.println(String("[session] ") + storageStatus);
}

void cmdWifi(const String &) {
  c6Radio.requestWifiScan();
  dashboard.setBanner("C6 Wi-Fi aggregate scan requested");
  Serial.println(F("[c6] aggregate Wi-Fi scan requested; SSIDs/BSSIDs are not retained"));
}

void cmdProof(const String &) {
  Serial.println(F("[proof] TX disabled. No payload reads, IDs, protocol decoding, raw traces, replay, jamming, or brute force."));
}

void handleUiAction(RfLabUiAction action) {
  switch (action) {
    case kRfLabUiProbe: refreshProbe("touch"); break;
    case kRfLabUiDetectorToggle:
      if (lab.detectorRunning) pauseDetector("touch");
      else startDetector("touch");
      break;
    case kRfLabUiSave: cmdSave(""); break;
    case kRfLabUiClear: cmdClear(""); break;
    case kRfLabUiWifiScan: cmdWifi(""); break;
    case kRfLabUiProof: cmdProof(""); break;
    default: break;
  }
}

void setup() {
  Logger::begin(115200);
  Logger::info("app", "LittleHakr RF Lab Project 17");
  printHardwareProfile(Serial, activeHardwareProfile());
  Serial.println(F("[safety] TX disabled; aggregate authorized-lab detection only"));
  refreshProbe("boot");
  sessions.begin(storageStatus);
  c6Radio.begin();
  dashboard.begin();
  dashboard.setBanner(String("ready: ") + rfLabProofLabel(lab.proof));
  router.begin(Serial, "rflab");
  router.on("status", "show proof, aggregate RF state, and C6 status", printStatus);
  router.on("probe", "re-read radio proof registers", cmdProbe);
  router.on("start", "start confirmed receive-only detector", cmdStart);
  router.on("pause", "pause receive-only detector", cmdPause);
  router.on("save", "save aggregate session summary", cmdSave);
  router.on("clear", "clear aggregate session counters", cmdClear);
  router.on("wifi", "request C6 aggregate Wi-Fi scan", cmdWifi);
  router.on("proof", "print project safety boundary", cmdProof);
}

void loop() {
  router.poll();
  c6Radio.tick();
  sampleDetector();
  RfLabUiEvent event;
  if (dashboard.tick(lab, c6Radio.snapshot(), sessions.ready(), event)) handleUiAction(event.action);
  if (millis() - lastHeartbeatMs >= 5000UL) {
    lastHeartbeatMs = millis();
    Serial.printf("[heartbeat] SPI=%s NRF24=%s CC1101=%s GDO=%d/%d DETECTOR=%s TX=disabled\n",
                  lab.spiReady ? "ready" : "error", lab.nrfDetected ? "yes" : "no",
                  lab.ccDetected ? "yes" : "no", lab.gdo0High, lab.gdo2High,
                  lab.detectorRunning ? "running" : "paused");
  }
  delay(20);
}
