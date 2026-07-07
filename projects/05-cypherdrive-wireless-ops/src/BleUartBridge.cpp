#include "BleUartBridge.h"
#include <CrowPanelShared.h>

namespace {
uint8_t boundedCount(uint8_t requested, uint8_t available) {
  return requested < available ? requested : available;
}

String csvField(const String &line, uint8_t index) {
  int start = 0;
  for (uint8_t i = 0; i < index; ++i) {
    start = line.indexOf(',', start);
    if (start < 0) return "";
    ++start;
  }

  int end = line.indexOf(',', start);
  String field = end < 0 ? line.substring(start) : line.substring(start, end);
  field.trim();
  return field;
}
}  // namespace

void BleUartBridge::begin(Stream *input) {
  input_ = input;
#if USE_BLE_UART_BRIDGE
  Logger::info("ble-bridge", "USE_BLE_UART_BRIDGE=1; parsing sidecar UART frames only");
#else
  Logger::info("ble-bridge", "mock BLE scan path enabled");
#endif
}

uint8_t BleUartBridge::scan(BleAdvertisementRecord records[], uint8_t maxRecords, Stream &out) {
  if (maxRecords == 0) return 0;

#if USE_BLE_UART_BRIDGE
  uint8_t count = readAvailable(records, maxRecords, out);
  if (count == 0) {
    out.println(F("[scan:ble] no UART bridge frames buffered"));
    out.println(F("[scan:ble] expected: BLE,<label>,<addr>,<rssi>,<vendor>,<note>"));
  }
  return count;
#else
  uint8_t count = boundedCount(maxRecords, 3);
  if (count > 0) {
    records[0].label = "AirPods-Pro";
    records[0].address = "AA:BB:CC:00:00:01";
    records[0].rssi = -58;
    records[0].vendor = "Apple";
    records[0].detail = "mock advertisement";
  }
  if (count > 1) {
    records[1].label = "SensorTag";
    records[1].address = "CC:DD:EE:00:00:02";
    records[1].rssi = -71;
    records[1].vendor = "TI";
    records[1].detail = "mock advertisement";
  }
  if (count > 2) {
    records[2].label = "Beacon";
    records[2].address = "11:22:33:44:55:66";
    records[2].rssi = -82;
    records[2].vendor = "generic";
    records[2].detail = "mock advertisement";
  }

  for (uint8_t i = 0; i < count; ++i) {
    printRecord(records[i], out);
  }
  return count;
#endif
}

uint8_t BleUartBridge::readAvailable(BleAdvertisementRecord records[], uint8_t maxRecords,
                                      Stream &out) {
  if (maxRecords == 0) return 0;

#if USE_BLE_UART_BRIDGE
  if (input_ == nullptr) return 0;

  uint8_t count = 0;
  while (input_->available() > 0 && count < maxRecords) {
    char c = (char)input_->read();
    if (c == '\r') continue;
    if (c == '\n') {
      if (lineLen_ == 0) continue;
      line_[lineLen_] = '\0';
      String frame(line_);
      lineLen_ = 0;
      if (parseFrame(frame, records[count], out)) {
        printRecord(records[count], out);
        ++count;
      }
      continue;
    }
    if (lineLen_ >= CYPHERDRIVE_BLE_UART_MAX_LINE - 1) {
      lineLen_ = 0;
      out.println(F("[ble-bridge] dropped oversized frame"));
      continue;
    }
    line_[lineLen_++] = c;
  }
  return count;
#else
  (void)records;
  (void)out;
  return 0;
#endif
}

bool BleUartBridge::injectLine(const String &line, BleAdvertisementRecord &record, Stream &out) {
#if USE_BLE_UART_BRIDGE
  if (parseFrame(line, record, out)) {
    printRecord(record, out);
    return true;
  }
  return false;
#else
  (void)line;
  (void)record;
  out.println(F("[ble-bridge] disabled; build with -DUSE_BLE_UART_BRIDGE=1"));
  return false;
#endif
}

const char *BleUartBridge::driverName() const {
#if USE_BLE_UART_BRIDGE
  return "uart-sidecar";
#else
  return "mock";
#endif
}

bool BleUartBridge::parseFrame(const String &frame, BleAdvertisementRecord &record,
                               Stream &out) const {
  String line = frame;
  line.trim();
  if (!(line.startsWith("BLE,") || line.startsWith("ADV,"))) {
    out.print(F("[ble-bridge] ignored frame prefix: "));
    out.println(line);
    return false;
  }

  record.label = csvField(line, 1);
  record.address = csvField(line, 2);
  String rssiField = csvField(line, 3);
  record.rssi = rssiField.toInt();
  record.vendor = csvField(line, 4);
  record.detail = csvField(line, 5);

  if (record.address.length() == 0 || rssiField.length() == 0) {
    out.print(F("[ble-bridge] malformed frame: "));
    out.println(line);
    return false;
  }
  if (record.label.length() == 0) record.label = "(unnamed)";
  if (record.vendor.length() == 0) record.vendor = "unknown";
  return true;
}

void BleUartBridge::printRecord(const BleAdvertisementRecord &record, Stream &out) const {
  out.print(F("[scan:ble] "));
  out.print(record.label);
  out.print(F(" addr="));
  out.print(record.address);
  out.print(F(" rssi="));
  out.print(record.rssi);
  out.print(F(" vendor="));
  out.print(record.vendor);
  out.print(F(" source="));
  out.println(driverName());
}
