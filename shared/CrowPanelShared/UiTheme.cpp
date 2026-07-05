#include "UiTheme.h"

static const UiTheme THEME = {
  "CrowPanel Demo",
  0x101820,
  0xF2F4F8,
  0x00A6A6,
  0xE84855
};

const UiTheme &defaultUiTheme() {
  return THEME;
}
