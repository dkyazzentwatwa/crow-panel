#ifndef WIRETAP_BENCH_PROBE_BUS_H
#define WIRETAP_BENCH_PROBE_BUS_H

#include "../config/ProjectConfig.h"
#include <Arduino.h>
#include <CrowPanelShared.h>

struct BenchProbeResult {
  bool ok;
  String serialLine;
  String dashboardTitle;
  String dashboardDetail;
  String event;
};

class BenchProbeBus {
 public:
  void begin(const HardwareProfile &profile);
  BenchProbeResult gpioRead(const String &args);
  BenchProbeResult i2cScan(const String &args);
  BenchProbeResult spiId(const String &args);
  BenchProbeResult uartRx(const String &args);
  const char *driverName() const;

 private:
  const HardwareProfile *profile_ = nullptr;
#if USE_BENCH_PROBES
  bool uartStarted_ = false;
  void beginUartIfNeeded();
#endif
};

#endif
