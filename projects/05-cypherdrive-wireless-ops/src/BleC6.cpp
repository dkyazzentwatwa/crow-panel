#include "BleC6.h"
#include <CrowPanelShared.h>

#if USE_BLE_C6
#include <WiFi.h>  // WiFi.setPins() configures the shared esp_hosted SDIO link
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <BLEClient.h>

namespace {
bool gInited = false;
// A few well-known BLE company identifiers -> vendor name.
const char *bleCompany(uint16_t id) {
  switch (id) {
    case 0x004C: return "Apple";
    case 0x0075: return "Samsung";
    case 0x0006: return "Microsoft";
    case 0x00E0: return "Google";
    case 0x0087: return "Garmin";
    case 0x0157: return "Huami/Amazfit";
    case 0x038F: return "Xiaomi";
    case 0x0059: return "Nordic";
    case 0x000D: return "Texas Instruments";
    case 0x0499: return "Ruuvi";
    default: return "";
  }
}
// Friendly labels for a few well-known GATT service UUIDs (short form).
const char *serviceLabel(const String &uuid) {
  String u = uuid;
  u.toLowerCase();
  if (u.indexOf("180f") >= 0) return "Battery";
  if (u.indexOf("180a") >= 0) return "Device Info";
  if (u.indexOf("1812") >= 0) return "HID";
  if (u.indexOf("1809") >= 0) return "Health Thermometer";
  if (u.indexOf("180d") >= 0) return "Heart Rate";
  if (u.indexOf("1801") >= 0) return "Generic Attribute";
  if (u.indexOf("1800") >= 0) return "Generic Access";
  return "";
}
}  // namespace
#endif

void BleC6::begin() {
#if USE_BLE_C6
  const HostedSdioPins &p = activeHardwareProfile().hostedSdio;
  WiFi.setPins(p.clk, p.cmd, p.d0, p.d1, p.d2, p.d3, p.reset);  // before init!
  BLEDevice::init("");
  gInited = true;
  available_ = true;  // stack came up far enough to init; scan proves the rest
  Logger::info("ble-c6", "USE_BLE_C6=1; NimBLE central over hosted C6 (unproven HW)");
#else
  available_ = true;  // simulated so the UI shows data with no radio
  Logger::info("ble-c6", "mock BLE central path enabled");
#endif
}

bool BleC6::hardwareEnabled() const { return USE_BLE_C6 == 1; }

const char *BleC6::driverName() const {
#if USE_BLE_C6
  return "hosted-c6-central";
#else
  return "mock";
#endif
}

uint8_t BleC6::scan(BleDeviceRecord records[], uint8_t maxRecords, Stream &out) {
  if (maxRecords == 0) return 0;
#if USE_BLE_C6
  if (!gInited) {
    out.println(F("[scan:ble] central stack not up"));
    available_ = false;
    return 0;
  }
  BLEScan *scanner = BLEDevice::getScan();
  scanner->setActiveScan(true);   // request scan responses (names)
  scanner->setInterval(100);
  scanner->setWindow(99);
  BLEScanResults *results = scanner->start(4, false);
  int found = results ? results->getCount() : 0;
  uint8_t count = 0;
  for (int i = 0; i < found && count < maxRecords; ++i) {
    BLEAdvertisedDevice d = results->getDevice((uint32_t)i);
    records[count].address = d.getAddress().toString();
    records[count].name = d.haveName() ? d.getName() : String("(unnamed)");
    records[count].rssi = d.getRSSI();
    records[count].vendor = "";
    records[count].detail = d.haveServiceUUID() ? d.getServiceUUID().toString() : String("");
    records[count].connectable = true;
    records[count].txPower = d.haveTXPower() ? (int16_t)d.getTXPower() : 0;
    records[count].addrType = d.getAddressType() == 0 ? "public" : "random";
    if (d.haveManufacturerData()) {
      String md = d.getManufacturerData();
      if (md.length() >= 2) {
        uint16_t cid = (uint16_t)((uint8_t)md[0] | ((uint8_t)md[1] << 8));
        records[count].vendor = bleCompany(cid);
      }
    }
    out.print(F("[scan:ble] "));
    out.print(records[count].name);
    out.print(F(" "));
    out.print(records[count].address);
    out.print(F(" rssi="));
    out.print(records[count].rssi);
    out.println(F(" source=hosted-c6-central"));
    ++count;
  }
  scanner->clearResults();
  return count;
#else
  struct MockDev { const char *name; const char *addr; int32_t rssi; const char *vendor; bool conn; };
  static const MockDev kMock[] = {
      {"Cypher-Tag", "d1:22:33:44:55:66", -52, "Espressif", true},
      {"AirPods Pro", "a0:11:22:aa:bb:cc", -61, "Apple", false},
      {"Mi Band 7", "c8:47:8c:12:34:56", -70, "Xiaomi", true},
      {"HR-Monitor", "e2:99:88:77:66:55", -66, "Polar", true},
      {"BT-Speaker", "f4:12:34:56:78:9a", -75, "Generic", false},
      {"Bench-BLE", "b0:aa:bb:cc:dd:ee", -80, "Nordic", true},
  };
  const uint8_t available = sizeof(kMock) / sizeof(kMock[0]);
  uint8_t count = maxRecords < available ? maxRecords : available;
  for (uint8_t i = 0; i < count; ++i) {
    records[i].name = kMock[i].name;
    records[i].address = kMock[i].addr;
    records[i].rssi = kMock[i].rssi;
    records[i].vendor = kMock[i].vendor;
    records[i].detail = "";
    records[i].connectable = kMock[i].conn;
    records[i].txPower = -4;
    records[i].addrType = (i % 2) ? "random" : "public";
    out.print(F("[scan:ble] "));
    out.print(records[i].name);
    out.print(F(" "));
    out.print(records[i].address);
    out.print(F(" rssi="));
    out.print(records[i].rssi);
    out.println(F(" source=mock"));
  }
  return count;
#endif
}

bool BleC6::connect(const BleDeviceRecord &device, Stream &out) {
  serviceCount_ = 0;
  connected_ = false;
  if (!device.connectable) {
    out.println(F("[ble] device not connectable"));
    return false;
  }
#if USE_BLE_C6
  if (!gInited) { out.println(F("[ble] central stack not up")); return false; }
  BLEClient *client = BLEDevice::createClient();
  out.print(F("[ble] connecting "));
  out.println(device.address);
  if (!client->connect(BLEAddress(device.address))) {
    out.println(F("[ble] connect failed"));
    return false;
  }
  connected_ = true;
  connectedAddr_ = device.address;
  const std::map<std::string, BLERemoteService *> *svcs = client->getServices();
  for (auto const &kv : *svcs) {
    if (serviceCount_ >= kMaxServices) break;
    String uuid = kv.first.c_str();
    services_[serviceCount_].uuid = uuid;
    services_[serviceCount_].label = serviceLabel(uuid);
    std::map<std::string, BLERemoteCharacteristic *> *chars =
        kv.second->getCharacteristics();
    services_[serviceCount_].charCount = chars ? (uint8_t)chars->size() : 0;
    out.print(F("[ble] service "));
    out.print(uuid);
    out.print(F(" chars="));
    out.println(services_[serviceCount_].charCount);
    ++serviceCount_;
  }
  out.print(F("[ble] connected, services="));
  out.println(serviceCount_);
  return true;
#else
  connected_ = true;
  connectedAddr_ = device.address;
  struct MockSvc { const char *uuid; const char *label; uint8_t chars; };
  static const MockSvc kMock[] = {
      {"0x1800", "Generic Access", 3},
      {"0x1801", "Generic Attribute", 1},
      {"0x180a", "Device Info", 6},
      {"0x180f", "Battery", 1},
      {"0x1812", "HID", 5},
  };
  const uint8_t available = sizeof(kMock) / sizeof(kMock[0]);
  serviceCount_ = kMaxServices < available ? kMaxServices : available;
  out.print(F("[ble] connecting "));
  out.println(device.address);
  for (uint8_t i = 0; i < serviceCount_; ++i) {
    services_[i].uuid = kMock[i].uuid;
    services_[i].label = kMock[i].label;
    services_[i].charCount = kMock[i].chars;
    out.print(F("[ble] service "));
    out.print(services_[i].uuid);
    out.print(F(" ("));
    out.print(services_[i].label);
    out.print(F(") chars="));
    out.println(services_[i].charCount);
  }
  out.print(F("[ble] connected, services="));
  out.println(serviceCount_);
  return true;
#endif
}

void BleC6::disconnect(Stream &out) {
  out.println(F("[ble] disconnect"));
  connected_ = false;
  connectedAddr_ = "";
  serviceCount_ = 0;
}

uint8_t BleC6::services(BleServiceRecord out_[], uint8_t maxRecords, Stream &out) {
  (void)out;
  uint8_t count = maxRecords < serviceCount_ ? maxRecords : serviceCount_;
  for (uint8_t i = 0; i < count; ++i) out_[i] = services_[i];
  return count;
}
