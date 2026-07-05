#ifndef VISION_GUARD_INSPECTION_WORKFLOW_H
#define VISION_GUARD_INSPECTION_WORKFLOW_H

#include <Arduino.h>

struct InspectionResult {
  String qr;
  String status;
  uint8_t checksPassed;
  uint8_t checksTotal;
  String reason;
};

class InspectionWorkflow {
 public:
  void begin();
  InspectionResult run(const String &qr);

 private:
  uint32_t runCount_ = 0;
};

#endif
