#include "config/ProjectConfig.h"
#include <CrowPanelShared.h>
#include "src/CypherDeskOs.h"

CypherDeskOs desk;
SerialCommandRouter router;
EventLog eventLog;

void cmdStatus(const String &args);
void cmdHistory(const String &args);
void cmdFiles(const String &args);
void cmdNew(const String &args);
void cmdOpen(const String &args);
void cmdType(const String &args);
void cmdSave(const String &args);
void cmdBack(const String &args);
void cmdDemo(const String &args);
void cmdTouch(const String &args);
void cmdPage(const String &args);
void cmdDaily(const String &args);
void cmdScrap(const String &args);
void cmdFocus(const String &args);
void cmdRitual(const String &args);
void cmdTheme(const String &args);
void cmdSound(const String &args);
void cmdStats(const String &args);
void cmdSearch(const String &args);
void cmdTime(const String &args);
void cmdStorage(const String &args);
void cmdApp(const String &args);
void cmdApps(const String &args);
void cmdWifi(const String &args);
void cmdOsEvents(const String &args);
void cmdCalculator(const String &args);
void cmdCalendar(const String &args);
void cmdContacts(const String &args);
void cmdAlarm(const String &args);
void cmdMedia(const String &args);
void cmdAudio(const String &args);
void cmdRecovery(const String &args);
void cmdWeather(const String &args);

void cmdStatus(const String &args) {
  (void)args;
  printSystemStatus(Serial, "cypher-desk-os", eventLog.size());
  desk.printStatus(Serial);
}

void cmdHistory(const String &args) {
  (void)args;
  eventLog.printHistory(Serial);
}

void cmdFiles(const String &args) {
  (void)args;
  desk.printFiles(Serial);
}

void cmdNew(const String &args) {
  desk.commandNew(args);
  eventLog.add("new note opened");
}

void cmdOpen(const String &args) {
  desk.commandOpen(args);
  eventLog.add("note open requested");
}

void cmdType(const String &args) {
  desk.commandType(args);
  eventLog.add("text inserted");
}

void cmdSave(const String &args) {
  (void)args;
  desk.commandSave();
  eventLog.add("save requested");
}

void cmdBack(const String &args) {
  (void)args;
  desk.commandBack();
}

void cmdDemo(const String &args) {
  (void)args;
  desk.commandDemo();
  eventLog.add("demo text inserted");
}

void cmdTouch(const String &args) {
  (void)args;
  desk.printTouchDiagnostics(Serial);
}

void cmdPage(const String &args) { desk.commandPage(args); }
void cmdDaily(const String &args) { (void)args; desk.commandDaily(); }
void cmdScrap(const String &args) { (void)args; desk.commandScrap(); }
void cmdFocus(const String &args) { desk.commandFocus(args); }
void cmdRitual(const String &args) { desk.commandRitual(args); }
void cmdTheme(const String &args) { desk.commandTheme(args); }
void cmdSound(const String &args) { desk.commandSound(args); }
void cmdStats(const String &args) { (void)args; desk.commandStats(Serial); }
void cmdSearch(const String &args) { desk.commandSearch(args, Serial); }
void cmdTime(const String &args) { desk.commandTime(args, Serial); }
void cmdStorage(const String &args) { desk.commandStorage(args, Serial); }
void cmdApp(const String &args) { desk.commandApp(args, Serial); }
void cmdApps(const String &args) { (void)args; desk.commandApps(Serial); }
void cmdWifi(const String &args) { desk.commandWifi(args, Serial); }
void cmdOsEvents(const String &args) { (void)args; desk.commandEvents(Serial); }
void cmdCalculator(const String &args) { desk.commandCalculator(args, Serial); }
void cmdCalendar(const String &args) { desk.commandCalendar(args, Serial); }
void cmdContacts(const String &args) { desk.commandContacts(args, Serial); }
void cmdAlarm(const String &args) { desk.commandAlarm(args, Serial); }
void cmdMedia(const String &args) { desk.commandMedia(args, Serial); }
void cmdAudio(const String &args) { desk.commandAudio(args, Serial); }
void cmdRecovery(const String &args) { desk.commandRecovery(args, Serial); }
void cmdWeather(const String &args) { desk.commandWeather(args, Serial); }

void setup() {
  Logger::begin(115200);
  Logger::info("app", "CrowPanel Cypher Desk OS");
  printHardwareProfile(Serial, activeHardwareProfile());
  desk.begin();
  eventLog.add("Cypher Desk OS booted");
  router.begin(Serial, "cypher-desk-os");
  router.on("status", "workspace, editor, flags, and proof state", cmdStatus);
  router.on("history", "recent app events", cmdHistory);
  router.on("files", "list visible folders and notes", cmdFiles);
  router.on("new", "create/open a note: new [txt|md]", cmdNew);
  router.on("open", "open visible note: open <number>", cmdOpen);
  router.on("type", "insert text; use \\n for newline", cmdType);
  router.on("save", "save the open note", cmdSave);
  router.on("back", "leave editor/folder and autosave", cmdBack);
  router.on("demo", "open a note and insert demo copy", cmdDemo);
  router.on("touch", "print raw and mapped touch diagnostics", cmdTouch);
  router.on("page", "open page: page desk|notebooks|focus|ritual|archive|settings", cmdPage);
  router.on("daily", "open or create today's Daily Page", cmdDaily);
  router.on("scrap", "open a new autosaving scrap", cmdScrap);
  router.on("focus", "open focus page or start: focus [10|20|30|45]", cmdFocus);
  router.on("ritual", "open ritual, shuffle, or write: ritual [shuffle|write]", cmdRitual);
  router.on("theme", "cycle, list, or select a theme by name", cmdTheme);
  router.on("sound", "set sound: key <0-3>|ambience <0-4>|volume <0-100>", cmdSound);
  router.on("stats", "print gentle writing totals", cmdStats);
  router.on("search", "search notes: search <text> or search tag <tag>", cmdSearch);
  router.on("time", "time sync|timezone|zone <POSIX TZ>|prev|next|confirm", cmdTime);
  router.on("storage", "storage status, rebuild, or eject", cmdStorage);
  router.on("app", "open OS app: app <name>", cmdApp);
  router.on("apps", "list registered OS apps", cmdApps);
  router.on("wifi", "wifi scan|offline|online|saved N|forget N", cmdWifi);
  router.on("os-events", "print recent fixed-ring OS events", cmdOsEvents);
  router.on("calc", "calculator key: calc 7|+|=|C", cmdCalculator);
  router.on("calendar", "calendar list|add YYYY-MM-DD HH:MM title|delete N", cmdCalendar);
  router.on("contacts", "contacts list|add name|delete N", cmdContacts);
  router.on("alarm", "alarm on|off or timer <minutes>", cmdAlarm);
  router.on("media", "indexed local music, podcast, and recording counts", cmdMedia);
  router.on("audio", "audio speaker|mic|record [name]|play <path>|stop|volume N|status", cmdAudio);
  router.on("recovery", "recover structured writes: recovery run", cmdRecovery);
  router.on("weather", "weather status|refresh|location <lat> <lon> [label]", cmdWeather);
}

void loop() {
  router.poll();
  desk.tick();
  // Tight loop for input latency. The old delay(12) here starved the audio
  // pump - it woke less often than the DMA ring drained. Touch is throttled to
  // CYPHER_DESK_TOUCH_POLL_MS inside tick(), redraws only happen when something
  // is dirty, and the mixer runs on its own task, so this just yields.
  delay(2);
}
