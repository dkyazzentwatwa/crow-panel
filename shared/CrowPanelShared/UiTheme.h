#ifndef CROW_PANEL_UI_THEME_H
#define CROW_PANEL_UI_THEME_H

#include <Arduino.h>

struct UiTheme {
  const char *name;
  uint32_t background;
  uint32_t foreground;
  uint32_t accent;
  uint32_t danger;
};

const UiTheme &defaultUiTheme();

#endif
