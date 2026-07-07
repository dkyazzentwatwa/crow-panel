#include "CreatorOpsDataSource.h"
#include "../config/ProjectConfig.h"

static void setMetric(CreatorOpsMetric &metric, const char *title,
                      unsigned long value, const char *meta,
                      const char *detailTitle, const char *detailBody) {
  metric.title = title;
  metric.value = String(value);
  metric.meta = meta;
  metric.detailTitle = detailTitle;
  metric.detailBody = detailBody;
}

void CreatorOpsDataSource::begin() {
  refresh();
}

bool CreatorOpsDataSource::refresh() {
#if USE_CREATOROPS_API
  loadApiCacheSnapshot();
#else
  loadStaticSnapshot();
#endif
  snapshot_.loadedAtMs = millis();
  return true;
}

const CreatorOpsSnapshot &CreatorOpsDataSource::snapshot() const {
  return snapshot_;
}

const CreatorOpsMetric &CreatorOpsDataSource::metric(CreatorOpsMetricIndex index) const {
  return snapshot_.metrics[(uint8_t)index];
}

void CreatorOpsDataSource::printSummary(Stream &out) const {
  out.print(F("[creatorops] source="));
  out.print(snapshot_.sourceMode);
  out.print(F(" label=\""));
  out.print(snapshot_.sourceLabel);
  out.print(F("\" proof=\""));
  out.print(snapshot_.proofState);
  out.println(F("\""));

  for (uint8_t i = 0; i < kCreatorOpsMetricCount; i++) {
    const CreatorOpsMetric &item = snapshot_.metrics[i];
    out.print(F("  "));
    out.print(item.title);
    out.print(F(": "));
    out.print(item.value);
    out.print(F(" - "));
    out.println(item.meta);
  }
}

void CreatorOpsDataSource::loadStaticSnapshot() {
  snapshot_.sourceMode = "static";
  snapshot_.sourceLabel = "compiled local mock";
  snapshot_.sourceDetail = "Project-local CreatorOpsDataSource mock snapshot";
  snapshot_.banner = "creator pipeline ready";
  snapshot_.footer = "Static mock data; dashboard only; no posting or scheduling actions";
  snapshot_.proofState = "compile-ready local mock";

  setMetric(snapshot_.metrics[CREATOROPS_IDEAS], "Ideas", 18, "inbox + backlog",
            "Ideas",
            "Files are the brain|CrowPanel as physical command surface|Turn into short + newsletter");
  setMetric(snapshot_.metrics[CREATOROPS_DRAFTS], "Drafts", 7, "needs review",
            "Drafts",
            "NFC Field Lab reel hook|Status: draft only|Needs source/proof pass");
  setMetric(snapshot_.metrics[CREATOROPS_FILMING], "Filming", 3, "ready scripts",
            "Filming",
            "Bench closeups queued|Need clean Serial proof clips|No public claims before hardware evidence");
  setMetric(snapshot_.metrics[CREATOROPS_SCHEDULED], "Scheduled", 5, "channel queue",
            "Scheduled",
            "Calendar view is mock-only|No scheduler integration|Use this as a local planning surface");
  setMetric(snapshot_.metrics[CREATOROPS_PUBLISHED], "Published", 888, "indexed objects",
            "Published",
            "Indexed corpus count only|Not a live platform read|Treat as prior local content-brain proof");
  setMetric(snapshot_.metrics[CREATOROPS_TASKS], "Tasks", 12, "follow-ups",
            "Tasks",
            "Capture hardware proof clips|Separate compile/upload/field proof|Review claims before publishing");
  setMetric(snapshot_.metrics[CREATOROPS_CHANNELS], "Channels", 4, "IG TikTok YT Substack",
            "Channels",
            "Instagram short-form|TikTok short-form|YouTube demos|Substack expansion");
}

void CreatorOpsDataSource::loadApiCacheSnapshot() {
  snapshot_.sourceMode = "api-cache";
  snapshot_.sourceLabel = CREATOROPS_API_CACHE_LABEL;
  snapshot_.sourceDetail = CREATOROPS_API_CACHE_DETAIL;
  snapshot_.banner = "api cache snapshot loaded";
  snapshot_.footer = "Read-only API cache; no POST, publish, schedule, delete, or credential path";
  snapshot_.proofState = "compile-ready api-cache";

  setMetric(snapshot_.metrics[CREATOROPS_IDEAS], "Ideas", CREATOROPS_API_CACHE_IDEAS,
            "cached inbox + backlog", "Ideas",
            "Cached idea count|Source exported from local API cache|Read-only on device");
  setMetric(snapshot_.metrics[CREATOROPS_DRAFTS], "Drafts", CREATOROPS_API_CACHE_DRAFTS,
            "cached review queue", "Drafts",
            "Cached draft count|Review-only surface|No send or publish command exists");
  setMetric(snapshot_.metrics[CREATOROPS_FILMING], "Filming", CREATOROPS_API_CACHE_FILMING,
            "cached shoot list", "Filming",
            "Cached filming count|Proof clips still need manual capture|No camera upload path");
  setMetric(snapshot_.metrics[CREATOROPS_SCHEDULED], "Scheduled", CREATOROPS_API_CACHE_SCHEDULED,
            "cached channel queue", "Scheduled",
            "Cached schedule count|Display-only queue|No scheduler or calendar mutation");
  setMetric(snapshot_.metrics[CREATOROPS_PUBLISHED], "Published", CREATOROPS_API_CACHE_PUBLISHED,
            "cached index total", "Published",
            "Cached published count|Local content index only|Not a live platform confirmation");
  setMetric(snapshot_.metrics[CREATOROPS_TASKS], "Tasks", CREATOROPS_API_CACHE_TASKS,
            "cached follow-ups", "Tasks",
            "Cached task count|Use for review prompts|No task deletion or external sync");
  setMetric(snapshot_.metrics[CREATOROPS_CHANNELS], "Channels", CREATOROPS_API_CACHE_CHANNELS,
            "cached channel roster", "Channels",
            "Cached channel count|Names stay local to the cache|No account credentials stored");
}
