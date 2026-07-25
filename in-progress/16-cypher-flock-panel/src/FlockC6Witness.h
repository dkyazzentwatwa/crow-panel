#ifndef CYPHER_FLOCK_C6_WITNESS_H
#define CYPHER_FLOCK_C6_WITNESS_H

#include "FlockTypes.h"

struct FlockWitnessNetwork {
  char ssid[33] = "";
  char bssid[18] = "";
  char auth[16] = "UNKNOWN";
  char pairwiseCipher[12] = "?";
  char groupCipher[12] = "?";
  char phy[20] = "?";
  char country[4] = "--";
  int8_t rssi = -127;
  uint8_t channel = 0;
  uint8_t secondaryChannel = 0;
  uint8_t bandwidthMhz = 20;
  uint8_t antenna = 0;
  uint8_t bssColor = 0;
  uint8_t countryStartChannel = 0;
  uint8_t countryChannelCount = 0;
  int8_t countryMaxTxPower = 0;
  bool hidden = false;
  bool wps = false;
  bool ftm = false;
  bool ftmResponder = false;
  bool ftmInitiator = false;
  uint32_t seenAtMs = 0;
};

class FlockC6Witness {
 public:
  void begin();
  void tick();
  void requestScan();
  void printStatus(Stream &output) const;
  void printNetworks(Stream &output) const;

  uint16_t count() const { return count_; }
  uint16_t totalFound() const { return totalFound_; }
  uint32_t scanCount() const { return scanCount_; }
  uint32_t generation() const { return generation_; }
  uint32_t lastScanMs() const { return lastScanMs_; }
  uint32_t scanAgeMs() const { return lastScanMs_ ? millis() - lastScanMs_ : 0; }
  bool scanning() const { return scanning_; }
  bool ready() const { return ready_; }
  bool hardwareEnabled() const;
  const char *driverName() const;
  const char *status() const { return status_; }
  const FlockWitnessNetwork *at(uint16_t index) const;

 private:
  void loadMock_();
  void startScan_();
  void consumeScan_(int16_t found);

  FlockWitnessNetwork networks_[FLOCK_C6_WITNESS_MAX_NETWORKS];
  uint16_t count_ = 0;
  uint16_t totalFound_ = 0;
  uint32_t scanCount_ = 0;
  uint32_t generation_ = 0;
  uint32_t lastScanMs_ = 0;
  uint32_t nextScanMs_ = 0;
  uint32_t scanStartedMs_ = 0;
  bool ready_ = false;
  bool scanning_ = false;
  bool scanRequested_ = false;
  char status_[64] = "not started";
};

#endif
