#include "config/ProjectConfig.h"
#include <CrowPanelShared.h>

#include "src/StarbeamTypes.h"
#include "src/RadioBus.h"
#include "src/Nrf24Array.h"
#include "src/Cc1101Pair.h"
#include "src/Recording.h"
#include "src/CoProcLink.h"
#include "src/StarbeamUi.h"

// Project 19: Starbeam Console — a full 1:1 port of project-starbeam onto the
// CrowPanel Advanced ESP32-P4. The P4 drives all seven radios natively over one
// shared SPI bus (5x nRF24L01+, 2x CC1101); the Wi-Fi/BLE/attack half runs on a
// UART-attached ESP32 dev module (stock starbeam_v2) reached through CoProcLink.
// A touch dashboard (StarbeamUi) replaces the 128x64 3-button menu.

RadioBus bus;
Nrf24Array nrf(bus);
Cc1101Pair cc(bus);
Recording rec;
CoProcLink coproc;
StarbeamUi ui;
SerialCommandRouter router;

StarbeamState st;
uint32_t lastSpectrumMs = 0;
uint32_t lastHeartbeatMs = 0;

// ---------------------------------------------------------------------------
static void setBanner(const char *b) {
  strncpy(st.banner, b, sizeof(st.banner) - 1);
  st.banner[sizeof(st.banner) - 1] = 0;
  ui.setBanner(st.banner);
}

static void probeRadios() {
  st.nrfPresentCount = 0;
  for (uint8_t i = 0; i < 5; ++i) {
    uint8_t status = 0xFF;
    st.nrf[i].present = bus.probeNrf(i, status);
    st.nrf[i].reg = status;
    if (st.nrf[i].present) ++st.nrfPresentCount;
  }
  st.ccPresentCount = 0;
  for (uint8_t i = 0; i < 2; ++i) {
    uint8_t part = 0xFF, ver = 0xFF;
    st.cc[i].present = bus.probeCc(i, part, ver);
    st.cc[i].reg = ver;
    if (st.cc[i].present) ++st.ccPresentCount;
  }
}

static bool armedFor(const StarbeamActionInfo &info) {
  return !info.requiresTx || st.txConfirmed;
}

static void stopAll(const char *why) {
  nrf.stopAll();
  cc.jam(2, false);
  coproc.send("stop_all");
  st.running = false;
  st.active = ACT_NONE;
  st.jamCycles = 0;
  setBanner(why);
}

// Execute a launched action.
static void launchAction(StarbeamAction a) {
  const StarbeamActionInfo &info = starbeamAction(a);
  st.active = a;
  st.running = false;

  if (a == ACT_STOP_ALL) { stopAll("all operations stopped"); return; }

  if (info.requiresTx && !st.txConfirmed) {
    ui.showOperation(a);
    setBanner("TX disarmed: copy LabProfile.h to arm");
    // still fall through for coproc? no — refuse transmit
    if (info.target != TGT_COPROC) return;
  }

  switch (info.target) {
    case TGT_COPROC:
      if (info.requiresTx && !st.txConfirmed) return;
      coproc.send(info.command);
      st.running = true;
      setBanner(info.command);
      break;

    case TGT_LOCAL:
      if (a == ACT_SETTINGS) setBanner("settings — see serial 'status'");
      else if (a == ACT_HELP) setBanner("touch a tile; STOP halts; serial 'help'");
      break;

    case TGT_NATIVE:
      switch (a) {
        case ACT_BT_JAM: case ACT_DRONE_JAM: case ACT_WIFI_JAM:
        case ACT_CC1_JAM: case ACT_CC1_SINGLE: case ACT_CC2_SINGLE:
          st.running = armedFor(info);
          setBanner(st.running ? "jamming — STOP to halt" : "TX disarmed");
          break;
        case ACT_NRF_SCAN:
          st.running = true; setBanner("nRF 2.4 GHz spectrum"); break;
        case ACT_CC_SCAN:
          st.running = true; setBanner("CC1101 sweep"); break;
        case ACT_GET_RSSI:
          st.running = true; setBanner("CC1101 RSSI"); break;
        case ACT_REC_RAW:
          rec.recordRaw(100, Recording::kBufferSize);
          st.recBytes = rec.byteCount(); st.recBufferValid = rec.valid();
          rec.save();
          setBanner("captured raw 433 MHz"); break;
        case ACT_PLAY_RAW:
          rec.playRaw(100, armedFor(info));
          setBanner(armedFor(info) ? "replayed buffer" : "TX disarmed"); break;
        case ACT_SHOW_RAW: case ACT_SHOW_BUFF:
          st.recBytes = rec.byteCount(); st.recBufferValid = rec.valid();
          setBanner("buffer shown — serial for hex"); break;
        case ACT_FLUSH_BUFF:
          rec.flush(); st.recBytes = 0; st.recBufferValid = false;
          setBanner("buffer flushed"); break;
        case ACT_TEST_NRF: case ACT_TEST_HSPI: case ACT_TEST_CC:
          probeRadios(); setBanner("register proof refreshed"); break;
        case ACT_RESET_CC:
          cc.reset(); probeRadios(); setBanner("CC1101 reset"); break;
        case ACT_FREQ_43440: cc.setFrequency(434.40f); st.ccFreqMhz = 434.40f; setBanner("434.40 MHz"); break;
        case ACT_FREQ_43430: cc.setFrequency(434.30f); st.ccFreqMhz = 434.30f; setBanner("434.30 MHz"); break;
        case ACT_FREQ_43400: cc.setFrequency(434.00f); st.ccFreqMhz = 434.00f; setBanner("434.00 MHz"); break;
        case ACT_FREQ_43390: cc.setFrequency(433.90f); st.ccFreqMhz = 433.90f; setBanner("433.90 MHz"); break;
        default: break;
      }
      break;
  }
  ui.showOperation(a);
}

// Continuous work for the active operation, called every loop while running.
static void runActive() {
  if (!st.running) return;
  const StarbeamActionInfo &info = starbeamAction(st.active);
  bool armed = armedFor(info);
  switch (st.active) {
    case ACT_BT_JAM:    nrf.btJam(armed); ++st.jamCycles; break;
    case ACT_DRONE_JAM: nrf.droneJam(armed); ++st.jamCycles; break;
    case ACT_WIFI_JAM:  nrf.wifiJam(armed); ++st.jamCycles; break;
    case ACT_CC1_JAM:   cc.jam(2, armed); ++st.jamCycles; break;
    case ACT_CC1_SINGLE:cc.jam(0, armed); ++st.jamCycles; break;
    case ACT_CC2_SINGLE:cc.jam(1, armed); ++st.jamCycles; break;
    case ACT_NRF_SCAN:
      if (millis() - lastSpectrumMs >= STARBEAM_SPECTRUM_INTERVAL_MS) {
        lastSpectrumMs = millis();
        st.spectrumPeak = nrf.sampleSpectrum(st.spectrum);
      }
      break;
    case ACT_CC_SCAN: {
      float f, r;
      cc.sweepStep(300.0f, 348.0f, f, r);
      st.ccFreqMhz = f; st.ccRssiDbm = r;
      break;
    }
    case ACT_GET_RSSI:
      st.ccRssiDbm = cc.rssi(0); st.ccLqi = cc.lqi(0);
      break;
    default: break;  // coproc ops update via coproc.poll()
  }
}

// ---------------------------------------------------------------------------
static void cmdStatus(const String &) {
  printSystemStatus(Serial, "starbeam-console", 0, &router);
  Serial.printf("[starbeam] nrf_present=%u cc_present=%u active=%s running=%s tx=%s coproc=%s\n",
                st.nrfPresentCount, st.ccPresentCount, starbeamAction(st.active).label,
                st.running ? "yes" : "no", st.txConfirmed ? "ARMED" : "SAFE",
                st.co.linked ? "linked" : "offline");
  for (uint8_t i = 0; i < 5; ++i)
    Serial.printf("  nRF%u STATUS=0x%02X %s\n", i, st.nrf[i].reg, st.nrf[i].present ? "ok" : "--");
  for (uint8_t i = 0; i < 2; ++i)
    Serial.printf("  CC%u  VER=0x%02X %s\n", i, st.cc[i].reg, st.cc[i].present ? "ok" : "--");
}
static void cmdProbe(const String &) { probeRadios(); cmdStatus(""); }
static void cmdStop(const String &) { stopAll("stopped via serial"); ui.toHome(); }
static void cmdTx(const String &) {
  Serial.printf("[tx] gate=%s profile=%s. Arm by copying LabProfile.example.h -> LabProfile.h.\n",
                st.txConfirmed ? "ARMED" : "SAFE", STARBEAM_PROFILE_NAME);
}
static void cmdFwd(const String &arg) {
  // forward an arbitrary Starbeam terminal command to the co-processor
  coproc.send(arg.c_str());
  Serial.printf("[coproc] sent: %s\n", arg.c_str());
}

void setup() {
  Logger::begin(115200);
  Logger::info("app", "Starbeam Console — Project 19");
  printHardwareProfile(Serial, activeHardwareProfile());

  st.txConfirmed = (STARBEAM_TX_CONFIRMED != 0);
  Serial.printf("[safety] TX gate=%s profile=%s\n",
                st.txConfirmed ? "ARMED" : "SAFE", STARBEAM_PROFILE_NAME);

  bus.begin();
  probeRadios();
  nrf.begin();
  cc.begin();
  rec.begin();
  st.recBytes = rec.byteCount();
  st.recBufferValid = rec.valid();
  st.ccFreqMhz = STARBEAM_CC1101_MHZ_X100 / 100.0f;
  coproc.begin();
  ui.begin();
  setBanner("ready");

  router.begin(Serial, "starbeam");
  router.on("status", "radios, active op, TX gate, co-proc link", cmdStatus);
  router.on("probe", "re-read every radio's ID registers", cmdProbe);
  router.on("stop", "stop all operations and return home", cmdStop);
  router.on("tx", "show / explain the transmit arming gate", cmdTx);
  router.on("fwd", "forward a raw command to the UART co-processor", cmdFwd);
}

void loop() {
  router.poll();
  coproc.poll(st.co);
  runActive();

  StarbeamAction launched = ui.tick(st);
  if (launched != ACT_NONE) launchAction(launched);

  if (millis() - lastHeartbeatMs >= 5000UL) {
    lastHeartbeatMs = millis();
    Serial.printf("[heartbeat] nrf=%u cc=%u active=%s run=%s tx=%s coproc=%s\n",
                  st.nrfPresentCount, st.ccPresentCount, starbeamAction(st.active).label,
                  st.running ? "y" : "n", st.txConfirmed ? "armed" : "safe",
                  st.co.linked ? "up" : "down");
  }
  delay(2);
}
