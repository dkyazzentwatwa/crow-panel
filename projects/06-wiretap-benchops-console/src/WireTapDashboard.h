#ifndef WIRETAP_DASHBOARD_H
#define WIRETAP_DASHBOARD_H

#include "../config/ProjectConfig.h"
#include <Arduino.h>

// Purpose-built display surface for WireTap BenchOps. Serial remains the
// source of truth; this mirrors the probe state to the CrowPanel when
// USE_DISPLAY=1 and no-ops in Serial-only builds.
class WireTapDashboard {
 public:
  static const uint8_t kMaxTiles = 8;

  void begin(const char *title, const char *subtitle, const char *linkLabel);
  void tick();

  void setTile(uint8_t index, const String &title, const String &value,
               const String &meta, bool active = true);
  void clearTile(uint8_t index);
  void select(uint8_t index);
  int8_t selectedIndex() const;

  void setBanner(const String &text);
  void setDetail(const String &title, const String &body);
  void setFooter(const String &text);
  void repaint();

 private:
  struct Tile {
    String title;
    String value;
    String meta;
    bool active = false;
  };

  void markDirty();
  void drawChrome();
  void drawHeader();
  void drawProtocolLanes();
  void drawBanner();
  void drawSafetyPanel();
  void drawDetail();
  void drawFooter();
  void selectFromTouch(uint8_t index);

  String title_;
  String subtitle_;
  String linkLabel_;
  String banner_;
  String detailTitle_;
  String detailBody_;
  String footer_;
  Tile tiles_[kMaxTiles];
  int8_t selected_ = -1;
  bool ready_ = false;
  bool dirty_ = true;
  bool wasTouched_ = false;
};

#endif
