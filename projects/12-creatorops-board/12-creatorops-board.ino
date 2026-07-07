#include "config/ProjectConfig.h"
#include <CrowPanelShared.h>

OpsDashboard dashboard;
SerialCommandRouter router;
EventLog eventLog;
StorageManager storage;

void refreshCreator(const String &banner) {
  dashboard.setTile(0, "Ideas", "18", "inbox + backlog");
  dashboard.setTile(1, "Drafts", "7", "needs review");
  dashboard.setTile(2, "Filming", "3", "ready scripts");
  dashboard.setTile(3, "Scheduled", "5", "channel queue");
  dashboard.setTile(4, "Published", "888", "indexed objects");
  dashboard.setTile(5, "Tasks", "12", "follow-ups");
  dashboard.setTile(6, "Channels", "4", "IG TikTok YT Substack");
  dashboard.setBanner(banner);
  dashboard.setFooter("CreatorOps v1 is local mock visibility only; no publishing actions");
}

void cmdStatus(const String &) { printSystemStatus(Serial, "creatorops", storage.eventCount()); }
void cmdHistory(const String &) { eventLog.printHistory(Serial); }

void cmdPipeline(const String &) {
  Serial.println(F("[pipeline] idea=18 draft=7 filmed=3 scheduled=5 published=888"));
  dashboard.setDetail("Pipeline", "Ideas 18|Drafts 7|Filming 3|Scheduled 5|Published index 888");
  refreshCreator("pipeline opened");
}

void cmdIdea(const String &) {
  Serial.println(F("[idea] Files are the brain - CrowPanel demo angle"));
  eventLog.add("Idea reviewed");
  dashboard.setDetail("Idea", "Files are the brain|CrowPanel as physical command surface|Turn into short + newsletter");
  refreshCreator("idea card selected");
}

void cmdDraft(const String &) {
  Serial.println(F("[draft] NFC Field Lab reel hook ready for review"));
  dashboard.setDetail("Draft", "NFC Field Lab reel hook|Status: draft only|Needs source/proof pass");
  refreshCreator("draft ready for review");
}

void cmdChannel(const String &) {
  Serial.println(F("[channels] Instagram, TikTok, YouTube, Substack"));
  dashboard.setDetail("Channels", "Instagram short-form|TikTok short-form|YouTube demos|Substack expansion");
  refreshCreator("channel status shown");
}

void cmdTask(const String &) {
  Serial.println(F("[task] capture hardware proof clips before publishing claims"));
  dashboard.setDetail("Task", "Capture hardware proof clips|Keep compile/upload/field proof separate|No public claims before evidence");
  refreshCreator("task highlighted");
}

void setup() {
  Logger::begin(115200);
  Logger::info("app", "CrowPanel CreatorOps Board");
  printHardwareProfile(Serial, activeHardwareProfile());
  storage.begin("creatorops");
  dashboard.begin("CREATOROPS", "CONTENT BOARD", "LOCAL");
  refreshCreator("creator pipeline ready");
  eventLog.add("CreatorOps Board booted");
  router.begin(Serial, "creatorops");
  router.on("status", "uptime, heap, profile, flags", cmdStatus);
  router.on("history", "recent events", cmdHistory);
  router.on("pipeline", "show content pipeline", cmdPipeline);
  router.on("idea", "show featured idea", cmdIdea);
  router.on("draft", "show draft card", cmdDraft);
  router.on("channel", "show channel status", cmdChannel);
  router.on("task", "show next task", cmdTask);
}

void loop() {
  router.poll();
  dashboard.tick();
  delay(20);
}
