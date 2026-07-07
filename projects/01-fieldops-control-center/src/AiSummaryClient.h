#ifndef FIELDOPS_AI_SUMMARY_CLIENT_H
#define FIELDOPS_AI_SUMMARY_CLIENT_H

#include <Arduino.h>
#include <CrowPanelShared.h>
#include "SensorNode.h"

class AiSummaryClient {
 public:
  void begin(CrowNetworkClient *network);
  String summarize(const SensorPacket &packet, const String &alert);

 private:
  CrowNetworkClient *network_ = nullptr;
};

#endif
