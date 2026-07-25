#include "FlockC6Witness.h"

#include <CrowPanelShared.h>

#if USE_FLOCK_C6_WITNESS && defined(CONFIG_IDF_TARGET_ESP32P4)
#include <WiFi.h>
#include <esp_wifi_types_generic.h>
#define FLOCK_HAS_C6_WITNESS 1
#else
#define FLOCK_HAS_C6_WITNESS 0
#endif

namespace {
#if FLOCK_HAS_C6_WITNESS
const char *authName(wifi_auth_mode_t mode) {
  switch (mode) {
    case WIFI_AUTH_OPEN: return "OPEN";
    case WIFI_AUTH_WEP: return "WEP";
    case WIFI_AUTH_WPA_PSK: return "WPA";
    case WIFI_AUTH_WPA2_PSK: return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK: return "WPA/WPA2";
#ifdef WIFI_AUTH_WPA2_ENTERPRISE
    case WIFI_AUTH_WPA2_ENTERPRISE: return "WPA2-ENT";
#endif
#ifdef WIFI_AUTH_WPA3_PSK
    case WIFI_AUTH_WPA3_PSK: return "WPA3";
#endif
#ifdef WIFI_AUTH_WPA2_WPA3_PSK
    case WIFI_AUTH_WPA2_WPA3_PSK: return "WPA2/3";
#endif
    default: return "UNKNOWN";
  }
}

const char *cipherName(wifi_cipher_type_t cipher) {
  switch (cipher) {
    case WIFI_CIPHER_TYPE_NONE: return "NONE";
    case WIFI_CIPHER_TYPE_WEP40: return "WEP40";
    case WIFI_CIPHER_TYPE_WEP104: return "WEP104";
    case WIFI_CIPHER_TYPE_TKIP: return "TKIP";
    case WIFI_CIPHER_TYPE_CCMP: return "CCMP";
    case WIFI_CIPHER_TYPE_TKIP_CCMP: return "TKIP/CCMP";
#ifdef WIFI_CIPHER_TYPE_GCMP
    case WIFI_CIPHER_TYPE_GCMP: return "GCMP";
#endif
#ifdef WIFI_CIPHER_TYPE_GCMP256
    case WIFI_CIPHER_TYPE_GCMP256: return "GCMP256";
#endif
    default: return "UNKNOWN";
  }
}

void phyName(const wifi_ap_record_t &record, char *output, size_t capacity) {
  output[0] = '\0';
  auto append = [output, capacity](const char *value) {
    if (output[0]) strlcat(output, "/", capacity);
    strlcat(output, value, capacity);
  };
  if (record.phy_11b) append("b");
  if (record.phy_11g) append("g");
  if (record.phy_11n) append("n");
  if (record.phy_11ax) append("ax");
  if (record.phy_lr) append("LR");
  if (!output[0]) strlcpy(output, "?", capacity);
}

uint8_t bandwidthMhz(wifi_bandwidth_t bandwidth) {
  switch (bandwidth) {
#ifdef WIFI_BW40
    case WIFI_BW40: return 40;
#endif
#ifdef WIFI_BW80
    case WIFI_BW80: return 80;
#endif
#ifdef WIFI_BW160
    case WIFI_BW160: return 160;
#endif
    default: return 20;
  }
}
#endif

void setMock(FlockWitnessNetwork &network, const char *ssid, const char *bssid,
             const char *auth, int8_t rssi, uint8_t channel, const char *phy,
             bool wps, bool ftm) {
  strlcpy(network.ssid, ssid, sizeof(network.ssid));
  strlcpy(network.bssid, bssid, sizeof(network.bssid));
  strlcpy(network.auth, auth, sizeof(network.auth));
  strlcpy(network.phy, phy, sizeof(network.phy));
  strlcpy(network.pairwiseCipher, strcmp(auth, "OPEN") == 0 ? "NONE" : "CCMP",
          sizeof(network.pairwiseCipher));
  strlcpy(network.groupCipher, network.pairwiseCipher, sizeof(network.groupCipher));
  strlcpy(network.country, "US", sizeof(network.country));
  network.rssi = rssi;
  network.channel = channel;
  network.wps = wps;
  network.ftm = ftm;
  network.seenAtMs = millis();
}
}  // namespace

void FlockC6Witness::begin() {
#if FLOCK_HAS_C6_WITNESS
  if (!configureCrowPanelHostedWiFiPins("flock-c6-witness")) {
    strlcpy(status_, "hosted SDIO pin setup failed", sizeof(status_));
    Logger::error("flock-c6", status_);
    return;
  }
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(false, false);
  ready_ = true;
  scanRequested_ = true;
  nextScanMs_ = millis() + 250;
  strlcpy(status_, "ready; passive scan queued", sizeof(status_));
  Logger::info("flock-c6", "hosted C6 passive witness enabled; never affects alerts");
#else
  ready_ = true;
  loadMock_();
  strlcpy(status_, "mock witness; hardware flag disabled", sizeof(status_));
  Logger::info("flock-c6", status_);
#endif
}

void FlockC6Witness::tick() {
#if FLOCK_HAS_C6_WITNESS
  if (!ready_) return;
  if (scanning_) {
    int16_t result = WiFi.scanComplete();
    if (result >= 0) {
      consumeScan_(result);
      WiFi.scanDelete();
      scanning_ = false;
      lastScanMs_ = millis();
      nextScanMs_ = lastScanMs_ + FLOCK_C6_WITNESS_INTERVAL_MS;
      ++scanCount_;
      ++generation_;
      snprintf(status_, sizeof(status_), "%u captured / %u nearby", count_, totalFound_);
    } else if (result == WIFI_SCAN_FAILED || millis() - scanStartedMs_ > 12000UL) {
      WiFi.scanDelete();
      scanning_ = false;
      nextScanMs_ = millis() + 5000UL;
      snprintf(status_, sizeof(status_), "passive scan failed code=%d", result);
      Logger::warn("flock-c6", status_);
    }
    return;
  }
  if (scanRequested_ || millis() >= nextScanMs_) startScan_();
#endif
}

void FlockC6Witness::requestScan() {
#if FLOCK_HAS_C6_WITNESS
  scanRequested_ = true;
#else
  loadMock_();
  ++scanCount_;
  ++generation_;
  lastScanMs_ = millis();
  strlcpy(status_, "mock witness refreshed", sizeof(status_));
#endif
}

void FlockC6Witness::startScan_() {
#if FLOCK_HAS_C6_WITNESS
  if (scanning_) return;
  scanRequested_ = false;
  int16_t result = WiFi.scanNetworks(true, true, true, FLOCK_C6_WITNESS_DWELL_MS);
  if (result == WIFI_SCAN_RUNNING) {
    scanning_ = true;
    scanStartedMs_ = millis();
    strlcpy(status_, "passive 2.4 GHz scan running", sizeof(status_));
  } else if (result >= 0) {
    consumeScan_(result);
    WiFi.scanDelete();
    lastScanMs_ = millis();
    nextScanMs_ = lastScanMs_ + FLOCK_C6_WITNESS_INTERVAL_MS;
    ++scanCount_;
    ++generation_;
  } else {
    nextScanMs_ = millis() + 5000UL;
    snprintf(status_, sizeof(status_), "scan start failed code=%d", result);
  }
#endif
}

void FlockC6Witness::consumeScan_(int16_t found) {
#if FLOCK_HAS_C6_WITNESS
  memset(networks_, 0, sizeof(networks_));
  totalFound_ = found > 0 ? (uint16_t)found : 0;
  count_ = min((uint16_t)FLOCK_C6_WITNESS_MAX_NETWORKS, totalFound_);
  for (uint16_t index = 0; index < count_; ++index) {
    FlockWitnessNetwork &network = networks_[index];
    wifi_ap_record_t *raw = (wifi_ap_record_t *)WiFi.getScanInfoByIndex(index);
    String ssid = WiFi.SSID(index);
    network.hidden = ssid.length() == 0;
    strlcpy(network.ssid, network.hidden ? "(hidden)" : ssid.c_str(), sizeof(network.ssid));
    strlcpy(network.bssid, WiFi.BSSIDstr(index).c_str(), sizeof(network.bssid));
    network.rssi = (int8_t)constrain(WiFi.RSSI(index), -127, 20);
    network.channel = (uint8_t)WiFi.channel(index);
    strlcpy(network.auth, authName(WiFi.encryptionType(index)), sizeof(network.auth));
    network.seenAtMs = millis();
    if (!raw) continue;
    strlcpy(network.pairwiseCipher, cipherName(raw->pairwise_cipher), sizeof(network.pairwiseCipher));
    strlcpy(network.groupCipher, cipherName(raw->group_cipher), sizeof(network.groupCipher));
    phyName(*raw, network.phy, sizeof(network.phy));
    network.bandwidthMhz = bandwidthMhz(raw->bandwidth);
    network.secondaryChannel = (uint8_t)raw->second;
    network.antenna = (uint8_t)raw->ant;
    network.wps = raw->wps;
    network.ftmResponder = raw->ftm_responder;
    network.ftmInitiator = raw->ftm_initiator;
    network.ftm = network.ftmResponder || network.ftmInitiator;
    network.bssColor = raw->he_ap.bss_color;
    network.countryStartChannel = raw->country.schan;
    network.countryChannelCount = raw->country.nchan;
    network.countryMaxTxPower = raw->country.max_tx_power;
    if (raw->country.cc[0]) {
      network.country[0] = raw->country.cc[0];
      network.country[1] = raw->country.cc[1];
      network.country[2] = raw->country.cc[2];
      network.country[3] = '\0';
    }
  }
#else
  (void)found;
#endif
}

void FlockC6Witness::loadMock_() {
  memset(networks_, 0, sizeof(networks_));
  count_ = totalFound_ = 6;
  setMock(networks_[0], "StudioNet", "02:11:22:33:44:55", "WPA2", -42, 6, "b/g/n", true, false);
  setMock(networks_[1], "GuestLab", "02:aa:bb:cc:dd:01", "OPEN", -57, 11, "g/n", false, false);
  setMock(networks_[2], "(hidden)", "02:aa:bb:cc:dd:02", "WPA2/3", -64, 1, "b/g/n/ax", false, true);
  networks_[2].hidden = true;
  networks_[2].bssColor = 17;
  setMock(networks_[3], "MakerAP", "02:aa:bb:cc:dd:03", "WPA3", -71, 3, "g/n/ax", false, true);
  setMock(networks_[4], "IoT-Devices", "02:aa:bb:cc:dd:04", "WPA2", -78, 9, "b/g/n", true, false);
  setMock(networks_[5], "Coffee-WiFi", "02:aa:bb:cc:dd:05", "OPEN", -84, 1, "b/g/n", false, false);
  lastScanMs_ = millis();
}

const FlockWitnessNetwork *FlockC6Witness::at(uint16_t index) const {
  return index < count_ ? &networks_[index] : nullptr;
}

bool FlockC6Witness::hardwareEnabled() const {
  return FLOCK_HAS_C6_WITNESS == 1;
}

const char *FlockC6Witness::driverName() const {
  return FLOCK_HAS_C6_WITNESS ? "hosted-c6-passive" : "mock-c6-witness";
}

void FlockC6Witness::printStatus(Stream &output) const {
  output.printf("[witness] driver=%s ready=%s scanning=%s rows=%u total=%u scans=%lu age_ms=%lu status=%s\n",
                driverName(), ready_ ? "yes" : "no", scanning_ ? "yes" : "no",
                count_, totalFound_, (unsigned long)scanCount_,
                (unsigned long)scanAgeMs(), status_);
}

void FlockC6Witness::printNetworks(Stream &output) const {
  printStatus(output);
  for (uint16_t index = 0; index < count_; ++index) {
    const FlockWitnessNetwork &network = networks_[index];
    output.printf("[witness:ap] i=%u ssid=%s bssid=%s rssi=%d ch=%u second=%u auth=%s cipher=%s/%s phy=%s bw=%u ant=%u country=%s country_start=%u country_channels=%u country_max_tx=%d bss_color=%u wps=%u ftm_r=%u ftm_i=%u hidden=%u\n",
                  index, network.ssid, network.bssid, network.rssi, network.channel,
                  network.secondaryChannel, network.auth, network.pairwiseCipher,
                  network.groupCipher, network.phy, network.bandwidthMhz, network.antenna,
                  network.country, network.countryStartChannel, network.countryChannelCount,
                  network.countryMaxTxPower, network.bssColor, network.wps,
                  network.ftmResponder, network.ftmInitiator, network.hidden);
  }
}
