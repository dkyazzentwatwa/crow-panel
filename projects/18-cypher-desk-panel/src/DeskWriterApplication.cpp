#include "DeskWriterApplication.h"

#include "DeskAppRouter.h"
#include "DeskEventBus.h"

void DeskWriterApplication::begin(DeskAppContext &context) { context_ = &context; }
void DeskWriterApplication::onEnter() {
  if (!begun_) {
    writer_.begin(false, context_ != nullptr ? context_->wifi : nullptr,
                  context_ != nullptr ? context_->storage : nullptr);
    begun_ = true;
  } else writer_.reloadPreferences();
  if (context_ != nullptr && context_->events != nullptr)
    context_->events->publish(kDeskEventInfo, "Writer opened");
}
void DeskWriterApplication::tick(uint32_t) {
  if (!begun_) return;
  writer_.tick();
  if (writer_.consumeOsHomeRequest() && context_ != nullptr && context_->router != nullptr)
    context_->router->home();
}
bool DeskWriterApplication::handleTouch(const DeskTouchEvent &) {
  // The legacy writer retains its calibrated touch loop during this adapter
  // phase. All OS-native applications use DeskTouchService tap-on-release.
  return false;
}
void DeskWriterApplication::draw() {}
bool DeskWriterApplication::handleBack() {
  if (!begun_) return false;
  writer_.commandBack();
  return true;
}
DeskApp &DeskWriterApplication::writer() { return writer_; }
