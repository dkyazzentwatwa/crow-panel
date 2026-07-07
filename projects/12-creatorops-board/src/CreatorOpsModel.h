#ifndef CREATOROPS_MODEL_H
#define CREATOROPS_MODEL_H

#include <Arduino.h>

static const uint8_t kCreatorOpsMetricCount = 7;

enum CreatorOpsMetricIndex : uint8_t {
  CREATOROPS_IDEAS = 0,
  CREATOROPS_DRAFTS = 1,
  CREATOROPS_FILMING = 2,
  CREATOROPS_SCHEDULED = 3,
  CREATOROPS_PUBLISHED = 4,
  CREATOROPS_TASKS = 5,
  CREATOROPS_CHANNELS = 6
};

struct CreatorOpsMetric {
  String title;
  String value;
  String meta;
  String detailTitle;
  String detailBody;
};

struct CreatorOpsSnapshot {
  CreatorOpsMetric metrics[kCreatorOpsMetricCount];
  String sourceMode;
  String sourceLabel;
  String sourceDetail;
  String banner;
  String footer;
  String proofState;
  uint32_t loadedAtMs = 0;
};

#endif
