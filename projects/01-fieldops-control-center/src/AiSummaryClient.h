#ifndef FIELDOPS_AI_SUMMARY_CLIENT_H
#define FIELDOPS_AI_SUMMARY_CLIENT_H

#include <Arduino.h>
#include <CrowPanelShared.h>
#include "SensorNode.h"

class AiSummaryClient {
 public:
  void begin(NetworkClient *network);
  String summarize(const SensorPacket &packet, const String &alert);

 private:
  NetworkClient *network_ = nullptr;
};

#endif
