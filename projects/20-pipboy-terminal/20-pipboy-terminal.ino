#include "config/ProjectConfig.h"
#include <CrowPanelShared.h>
#include "src/PipBoyTerminal.h"

PipBoyTerminal pipboy;
SerialCommandRouter router;
EventLog eventLog;

void cmdStatus(const String &) { printSystemStatus(Serial, "pipboy-terminal", eventLog.size()); pipboy.printStatus(Serial); }
void cmdPage(const String &args) {
  String value = args; value.toLowerCase(); value.trim();
  if (value == "home") pipboy.page(kPipHome); else if (value == "stat") pipboy.page(kPipStat); else if (value == "map") pipboy.page(kPipMap);
  else if (value == "items") pipboy.page(kPipItems); else if (value == "data") pipboy.page(kPipData); else if (value == "radio") pipboy.page(kPipRadio);
}
void cmdRadio(const String &args) { pipboy.commandRadio(args, Serial); }
void cmdWeather(const String &) { pipboy.commandWeather(Serial); }
void cmdStorage(const String &) { pipboy.commandStorage(Serial); }
void cmdTouch(const String &) { pipboy.printTouch(Serial); }
void cmdHistory(const String &) { eventLog.printHistory(Serial); }

void setup() {
  Logger::begin(115200); Logger::info("app", "Pip-Boy 3000 CrowPanel Terminal");
  printHardwareProfile(Serial, activeHardwareProfile()); pipboy.begin(); eventLog.add("Pip-Boy terminal booted");
  router.begin(Serial, "pipboy-terminal");
  router.on("status", "page, SD, audio, network, and proof state", cmdStatus);
  router.on("page", "home|stat|map|items|data|radio", cmdPage);
  router.on("radio", "play|next|stop|test|volume N", cmdRadio);
  router.on("weather", "refresh live weather", cmdWeather);
  router.on("storage", "SD media status", cmdStorage);
  router.on("touch", "last mapped touch point", cmdTouch);
  router.on("history", "boot and command history", cmdHistory);
}
void loop() { router.poll(); pipboy.tick(); delay(8); }
