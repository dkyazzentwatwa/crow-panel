#include "InspectionWorkflow.h"
#include <CrowPanelShared.h>

void InspectionWorkflow::begin() {
  Logger::info("inspection", "mock checklist ready");
}

InspectionResult InspectionWorkflow::run(const String &qr) {
  runCount_++;
  bool pass = (runCount_ % 4) != 0;
  InspectionResult result;
  result.qr = qr;
  result.status = pass ? "pass" : "fail";
  result.checksTotal = 5;
  result.checksPassed = pass ? 5 : 3;
  result.reason = pass ? "All mock checks passed" : "Mock defect: label mismatch";
  return result;
}
