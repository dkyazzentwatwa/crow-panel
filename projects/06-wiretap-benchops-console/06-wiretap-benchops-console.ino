#include "config/ProjectConfig.h"
#include "src/BenchProbeBus.h"
#include <CrowPanelShared.h>

OpsDashboard dashboard;
SerialCommandRouter router;
EventLog eventLog;
StorageManager storage;
BenchProbeBus benchProbes;
String modeName = "hiz";

void refreshBench(const String &banner) {
  dashboard.setTile(0, "Mode", modeName, benchProbes.driverName());
  dashboard.setTile(1, "Pins", "audit", "3.3V only");
  dashboard.setTile(2, "I2C", "scan", USE_BENCH_PROBES ? "address only" : "mock 0x3C 0x50");
  dashboard.setTile(3, "SPI", "jedec", WIRETAP_ALLOW_SPI_ID_CLOCKING ? "clocked opt-in" : "disabled by default");
  dashboard.setTile(4, "UART", "rx", USE_BENCH_PROBES ? "RX only" : "mock AT response");
  dashboard.setTile(5, "GPIO", "read", USE_BENCH_PROBES ? "INPUT high-Z" : "no real drive in v1");
  dashboard.setBanner(banner);
  dashboard.setFooter("BenchOps mock-first; USE_BENCH_PROBES is compile-verified, not field-proven");
}

void showProbeResult(const BenchProbeResult &result, const String &banner) {
  Serial.println(result.serialLine);
  if (result.event.length() > 0) {
    eventLog.add(result.event);
  }
  dashboard.setDetail(result.dashboardTitle, result.dashboardDetail);
  refreshBench(result.ok ? banner : "probe blocked");
}

void cmdStatus(const String &) {
  printSystemStatus(Serial, "wiretap-benchops", storage.eventCount());
  Serial.print(F("[status] flags USE_BENCH_PROBES="));
  Serial.println(USE_BENCH_PROBES);
  Serial.print(F("[status] probe_driver="));
  Serial.println(benchProbes.driverName());
  Serial.print(F("[status] WIRETAP_ALLOW_SPI_ID_CLOCKING="));
  Serial.println(WIRETAP_ALLOW_SPI_ID_CLOCKING);
}

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
  Serial.println(F("[pins] use 3.3V only; no default probe pins are configured"));
  Serial.println(F("[pins] GPIO reads use INPUT/high-Z with no pullups or outputs"));
  Serial.println(F("[pins] I2C clocks address probes only; SPI ID is disabled unless explicitly clock-enabled"));
  Serial.println(F("[pins] UART probe configures RX only; TX is not assigned"));
  dashboard.setDetail("Pin Audit", "3.3V targets only|No guessed probe pins|Copy config/Pins.example.h to Pins.h");
  refreshBench("pin safety audit shown");
}

void cmdI2c(const String &args) {
  showProbeResult(benchProbes.i2cScan(args), "I2C scan complete");
}

void cmdSpi(const String &args) {
  showProbeResult(benchProbes.spiId(args), "SPI id read complete");
}

void cmdUart(const String &args) {
  showProbeResult(benchProbes.uartRx(args), "UART RX complete");
}

void cmdGpio(const String &args) {
  showProbeResult(benchProbes.gpioRead(args), "GPIO read complete");
}

void setup() {
  Logger::begin(115200);
  Logger::info("app", "CrowPanel WireTap BenchOps Console");
  const HardwareProfile &profile = activeHardwareProfile();
  printHardwareProfile(Serial, profile);
  benchProbes.begin(profile);
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
