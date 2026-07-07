#ifndef CREATOROPS_DATA_SOURCE_H
#define CREATOROPS_DATA_SOURCE_H

#include <Arduino.h>
#include "CreatorOpsModel.h"

class CreatorOpsDataSource {
 public:
  void begin();
  bool refresh();

  const CreatorOpsSnapshot &snapshot() const;
  const CreatorOpsMetric &metric(CreatorOpsMetricIndex index) const;
  void printSummary(Stream &out) const;

 private:
  void loadStaticSnapshot();
  void loadApiCacheSnapshot();

  CreatorOpsSnapshot snapshot_;
};

#endif
