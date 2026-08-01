#include "config/ProjectConfig.h"
#include <CrowPanelShared.h>

static EventLog gEvents;
static SerialCommandRouter gRouter;

static void cmdStatus(const String &args) {
  (void)args;
  Serial.println("Cypher Stick — scaffold");
}

void setup() {
  Serial.begin(115200);
  delay(200);
  // NOTE: begin() takes a Stream REFERENCE plus an app name, and registers its
  // own `help`. EventLog has no begin() — it is add()-only.
  gRouter.begin(Serial, "Cypher Stick");
  gRouter.on("status", "show stick status", cmdStatus);
  gEvents.add("boot");
  Serial.println("Cypher Stick ready");
}

void loop() {
  gRouter.poll();
}
