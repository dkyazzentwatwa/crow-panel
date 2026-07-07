#include "config/ProjectConfig.h"
#include <CrowPanelShared.h>
#include "src/CreatorOpsDataSource.h"

OpsDashboard dashboard;
SerialCommandRouter router;
EventLog eventLog;
StorageManager storage;
CreatorOpsDataSource creatorSource;

void refreshCreatorDashboard(const String &banner) {
  const CreatorOpsSnapshot &snapshot = creatorSource.snapshot();
  for (uint8_t i = 0; i < kCreatorOpsMetricCount; i++) {
    const CreatorOpsMetric &metric = snapshot.metrics[i];
    dashboard.setTile(i, metric.title, metric.value, metric.meta);
  }
  dashboard.setBanner(banner.length() > 0 ? banner : snapshot.banner);
  dashboard.setFooter(snapshot.footer);
}

void showMetric(CreatorOpsMetricIndex index, const char *eventName) {
  const CreatorOpsMetric &item = creatorSource.metric(index);
  Serial.print(F("["));
  Serial.print(item.title);
  Serial.print(F("] "));
  Serial.print(item.value);
  Serial.print(F(" - "));
  Serial.println(item.meta);
  eventLog.add(eventName);
  dashboard.select((uint8_t)index);
  dashboard.setDetail(item.detailTitle, item.detailBody);
  refreshCreatorDashboard(item.title + " selected");
}

void cmdStatus(const String &) {
  printSystemStatus(Serial, "creatorops", storage.eventCount());
  creatorSource.printSummary(Serial);
}

void cmdHistory(const String &) {
  eventLog.printHistory(Serial);
}

void cmdPipeline(const String &) {
  creatorSource.printSummary(Serial);
  dashboard.setDetail("Pipeline", "Ideas|Drafts|Filming|Scheduled|Published|Tasks|Channels");
  refreshCreatorDashboard("pipeline opened");
}

void cmdIdeas(const String &) {
  showMetric(CREATOROPS_IDEAS, "ideas reviewed");
}

void cmdDrafts(const String &) {
  showMetric(CREATOROPS_DRAFTS, "drafts reviewed");
}

void cmdFilming(const String &) {
  showMetric(CREATOROPS_FILMING, "filming reviewed");
}

void cmdScheduled(const String &) {
  showMetric(CREATOROPS_SCHEDULED, "scheduled reviewed");
}

void cmdPublished(const String &) {
  showMetric(CREATOROPS_PUBLISHED, "published reviewed");
}

void cmdTasks(const String &) {
  showMetric(CREATOROPS_TASKS, "tasks reviewed");
}

void cmdChannels(const String &) {
  showMetric(CREATOROPS_CHANNELS, "channels reviewed");
}

void cmdSource(const String &) {
  const CreatorOpsSnapshot &snapshot = creatorSource.snapshot();
  Serial.print(F("[source] mode="));
  Serial.print(snapshot.sourceMode);
  Serial.print(F(" label=\""));
  Serial.print(snapshot.sourceLabel);
  Serial.print(F("\" detail=\""));
  Serial.print(snapshot.sourceDetail);
  Serial.print(F("\" proof=\""));
  Serial.print(snapshot.proofState);
  Serial.println(F("\""));
  dashboard.setDetail("Source", snapshot.sourceMode + "|" + snapshot.sourceLabel + "|" + snapshot.proofState);
  refreshCreatorDashboard("source inspected");
}

void cmdRefresh(const String &) {
  creatorSource.refresh();
  eventLog.add("snapshot refreshed");
  creatorSource.printSummary(Serial);
  refreshCreatorDashboard("snapshot refreshed");
}

void setup() {
  Logger::begin(115200);
  Logger::info("app", "CrowPanel CreatorOps Board");
  printHardwareProfile(Serial, activeHardwareProfile());
  storage.begin("creatorops");
  creatorSource.begin();
  dashboard.begin("CREATOROPS", "CONTENT BOARD", "LOCAL");
  refreshCreatorDashboard("creator pipeline ready");
  eventLog.add("CreatorOps Board booted");
  router.begin(Serial, "creatorops");
  router.on("status", "uptime, heap, profile, flags", cmdStatus);
  router.on("history", "recent events", cmdHistory);
  router.on("pipeline", "show content pipeline", cmdPipeline);
  router.on("ideas", "show idea backlog state", cmdIdeas);
  router.on("drafts", "show draft review state", cmdDrafts);
  router.on("filming", "show filming queue state", cmdFilming);
  router.on("scheduled", "show scheduled queue state", cmdScheduled);
  router.on("published", "show published index state", cmdPublished);
  router.on("tasks", "show task follow-up state", cmdTasks);
  router.on("channels", "show channel roster state", cmdChannels);
  router.on("source", "show read-only data source proof", cmdSource);
  router.on("refresh", "reload read-only snapshot/cache", cmdRefresh);
}

void loop() {
  router.poll();
  dashboard.tick();
  delay(20);
}
