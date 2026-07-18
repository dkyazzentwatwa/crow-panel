#ifndef CYPHER_DESK_WRITER_APPLICATION_H
#define CYPHER_DESK_WRITER_APPLICATION_H

#include "DeskApp.h"
#include "DeskApplication.h"

class DeskWriterApplication : public DeskApplication {
 public:
  DeskAppId id() const override { return kDeskAppWriter; }
  const char *title() const override { return "Writer"; }
  void begin(DeskAppContext &context) override;
  void onEnter() override;
  void tick(uint32_t nowMs) override;
  bool handleTouch(const DeskTouchEvent &event) override;
  void draw() override;
  bool handleBack() override;
  DeskApp &writer();

 private:
  DeskAppContext *context_ = nullptr;
  DeskApp writer_;
  bool begun_ = false;
};

#endif
