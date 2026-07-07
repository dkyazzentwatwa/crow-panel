#include "config/ProjectConfig.h"
#include <CrowPanelShared.h>

OpsDashboard dashboard;
SerialCommandRouter router;
EventLog eventLog;
StorageManager storage;
String modeName = "hiz";

void refreshBench(const String &banner) {
  dashboard.setTile(0, "Mode", modeName, "safe default is Hi-Z");
  dashboard.setTile(1, "Pins", "audit", "3.3V only");
  dashboard.setTile(2, "I2C", "scan", "mock 0x3C 0x50");
  dashboard.setTile(3, "SPI", "jedec", "mock flash id");
  dashboard.setTile(4, "UART", "rx", "mock AT response");
  dashboard.setTile(5, "GPIO", "read", "no real drive in v1");
  dashboard.setBanner(banner);
  dashboard.setFooter("BenchOps v1 is display/Serial only; connect nothing until real pin safety is verified");
}

void cmdStatus(const String &) { printSystemStatus(Serial, "wiretap-benchops", storage.eventCount()); }
void cmdHistory(const String &) { eventLog.printHistory(Serial); }

void cmdMode(const String &args) {
  String next = args;
  next.trim();
  if (next.length() > 0) modeName = next;
  Serial.print(F("[mode] "));
  Serial.println(modeName);
  eventLog.add(String("Mode changed to ") + modeName);
  dashboard.setDetail("Protocol Mode", String("Current mode: ") + modeName + "|All pins remain mock/high-Z in this CrowPanel port");
  refreshBench("mode changed");
}

void cmdPins(const String &) {
  Serial.println(F("[pins] safe candidates: 2,12,13,14,15,16,17,18,19,21,22,23,25,26,32,33"));
  dashboard.setDetail("Pin Audit", "3.3V GPIO only|Avoid flash pins 6-11|Use level shifting for 5V targets");
  refreshBench("pin safety audit shown");
}

void cmdI2c(const String &args) {
  Serial.println(F("[i2c] scan -> 0x3C OLED, 0x50 EEPROM"));
  eventLog.add("Mock I2C scan");
  dashboard.setDetail("I2C Scan", "0x3C OLED found|0x50 EEPROM found|Pullups required on real hardware");
  refreshBench("mock I2C scan complete");
}

void cmdSpi(const String &args) {
  Serial.println(F("[spi] id -> EF 40 18"));
  eventLog.add("Mock SPI JEDEC ID");
  dashboard.setDetail("SPI ID", "JEDEC EF 40 18|Mode 0, 1 MHz mock|No real CS toggled");
  refreshBench("mock SPI id read complete");
}

void cmdUart(const String &) {
  Serial.println(F("[uart] rx -> OK"));
  dashboard.setDetail("UART RX", "Baud 115200 mock|Received: OK|Cross TX/RX on real wiring");
  refreshBench("mock UART response");
}

void cmdGpio(const String &args) {
  Serial.print(F("[gpio] "));
  Serial.print(args);
  Serial.println(F(" -> LOW (mock)"));
  dashboard.setDetail("GPIO Read", String("Requested: ") + args + "|Value: LOW (mock)|CrowPanel v1 never drives target pins");
  refreshBench("mock GPIO read");
}

void setup() {
  Logger::begin(115200);
  Logger::info("app", "CrowPanel WireTap BenchOps Console");
  printHardwareProfile(Serial, activeHardwareProfile());
  storage.begin("wiretap");
  dashboard.begin("WIRETAP", "BENCHOPS CONSOLE", "MOCK GPIO");
  refreshBench("safe bench console ready");
  eventLog.add("WireTap BenchOps booted");
  router.begin(Serial, "wiretap");
  router.on("status", "uptime, heap, profile, flags", cmdStatus);
  router.on("history", "recent events", cmdHistory);
  router.on("mode", "mode <hiz|i2c|spi|uart|gpio>", cmdMode);
  router.on("pins", "show safe pin audit", cmdPins);
  router.on("i2c", "i2c scan", cmdI2c);
  router.on("spi", "spi id", cmdSpi);
  router.on("uart", "uart rx", cmdUart);
  router.on("gpio", "gpio get <pin>", cmdGpio);
}

void loop() {
  router.poll();
  dashboard.tick();
  delay(20);
}
