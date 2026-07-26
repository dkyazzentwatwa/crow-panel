#include "SdStore.h"
#include <CrowPanelShared.h>

#if USE_CYPHERDRIVE_SD
#include <SD_MMC.h>  // direct include behind the flag (never __has_include - see CLAUDE.md)
#endif

namespace {
// Minimal CSV escaping: quote a field that contains a comma or quote.
String csvField(const String &v) {
  bool needs = v.indexOf(',') >= 0 || v.indexOf('"') >= 0 || v.indexOf('\n') >= 0;
  if (!needs) return v;
  String out = "\"";
  for (uint16_t i = 0; i < v.length(); ++i) {
    char c = v[i];
    if (c == '"') out += '"';  // double the quote
    out += c;
  }
  out += '"';
  return out;
}
}  // namespace

bool SdStore::begin() {
#if USE_CYPHERDRIVE_SD
  bool mounted = SD_MMC.cardType() != CARD_NONE;  // don't double-mount
  ready_ = mounted || SD_MMC.begin("/sdcard", CYPHERDRIVE_SDMMC_1BIT != 0);
  if (ready_) {
    SD_MMC.mkdir("/cypherdrive");
    SD_MMC.mkdir("/cypherdrive/payloads");
    uint64_t freeBytes = SD_MMC.totalBytes() - SD_MMC.usedBytes();
    snprintf(status_, sizeof(status_), "SD %.1f GB free", freeBytes / 1e9);
    Logger::info("sd", status_);
  } else {
    snprintf(status_, sizeof(status_), "SD mount failed");
    Logger::warn("sd", "SD_MMC mount failed; export is log-only");
  }
  return ready_;
#else
  ready_ = false;
  snprintf(status_, sizeof(status_), "SD disabled");
  Logger::info("sd", "USE_CYPHERDRIVE_SD=0; export is log-only");
  return false;
#endif
}

const char *SdStore::statusLine() const { return status_; }

bool SdStore::appendCsv(const char *path, const char *header, const String &row) {
#if USE_CYPHERDRIVE_SD
  if (!ready_) {
    Logger::info("sd", String("(no card) ") + path + " <- " + row);
    return false;
  }
  bool existed = SD_MMC.exists(path);
  File f = SD_MMC.open(path, FILE_APPEND);
  if (!f) {
    Logger::error("sd", String("open failed ") + path);
    return false;
  }
  if (!existed || f.size() == 0) f.println(header);
  f.println(row);
  f.flush();
  f.close();
  return true;
#else
  Logger::info("sd", String("(disabled) ") + path + " <- " + row);
  return false;
#endif
}

bool SdStore::appendWifi(const WifiNetworkRecord &net) {
  String row = csvField(net.hidden ? String("(hidden)") : net.ssid) + "," +
               csvField(net.bssid) + "," + String(net.channel) + "," + net.band() + "," +
               String((long)net.rssi) + "," + csvField(net.auth) + "," + net.phy + "," +
               (net.wps ? "wps" : "") + "," + String(millis());
  return appendCsv("/cypherdrive/wifi.csv",
                   "ssid,bssid,channel,band,rssi,auth,phy,wps,uptime_ms", row);
}

bool SdStore::appendBle(const BleDeviceRecord &dev) {
  String row = csvField(dev.name) + "," + csvField(dev.address) + "," +
               String((long)dev.rssi) + "," + csvField(dev.vendor) + "," +
               String(dev.txPower) + "," + dev.addrType + "," +
               (dev.connectable ? "yes" : "no") + "," + csvField(dev.detail) + "," +
               String(millis());
  return appendCsv("/cypherdrive/ble.csv",
                   "name,address,rssi,vendor,tx_power,addr_type,connectable,service,uptime_ms",
                   row);
}

bool SdStore::appendRecon(const String &category, const String &summary, const String &detail) {
  String row = csvField(category) + "," + csvField(summary) + "," + csvField(detail) + "," +
               String(millis());
  return appendCsv("/cypherdrive/recon.csv", "category,summary,detail,uptime_ms", row);
}

uint8_t SdStore::listPayloads(String out[], uint8_t maxCount) {
#if USE_CYPHERDRIVE_SD
  if (!ready_ || maxCount == 0) return 0;
  File dir = SD_MMC.open("/cypherdrive/payloads");
  if (!dir || !dir.isDirectory()) return 0;
  uint8_t count = 0;
  for (File entry = dir.openNextFile(); entry && count < maxCount;
       entry = dir.openNextFile()) {
    if (!entry.isDirectory()) {
      String name = entry.name();
      int slash = name.lastIndexOf('/');
      if (slash >= 0) name = name.substring(slash + 1);
      if (name.endsWith(".txt") || name.endsWith(".dd")) {
        out[count++] = name;
      }
    }
    entry.close();
  }
  dir.close();
  return count;
#else
  (void)out; (void)maxCount;
  return 0;
#endif
}

bool SdStore::readPayload(const String &name, String &out) {
#if USE_CYPHERDRIVE_SD
  if (!ready_) return false;
  String path = String("/cypherdrive/payloads/") + name;
  File f = SD_MMC.open(path, FILE_READ);
  if (!f) return false;
  out = "";
  while (f.available()) out += (char)f.read();
  f.close();
  return true;
#else
  (void)name; (void)out;
  return false;
#endif
}
