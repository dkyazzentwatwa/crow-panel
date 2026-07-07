#include "HardwareProfile.h"

static const TouchPins TOUCH_GT911 = {
  46,
  45,
  42,
  40,
  0x5D,
  0x14
};

static const AudioPins AUDIO_OUT = {
  21,
  22,
  23,
  30
};

// Display wiring and MIPI-DSI timings from Elecrow's official V1.0 Arduino
// example (board_config.h / esp_panel_board_custom_conf.h): EK79007 panel,
// 2-lane DSI @ 1000 Mbps, 51 MHz DPI clock, backlight IO31, LCD reset IO41.
// Identical across V1.0/V1.1/V1.2 as far as the official examples show.
static const DisplayPins DISPLAY_PINS = {
  31,
  41
};

static const DisplayTiming DISPLAY_TIMING = {
  70,
  160,
  160,
  10,
  23,
  21,
  51000000,
  1000
};

static const HardwareProfile PROFILE_V1_0 = {
  CROWPANEL_P4_7IN_V1_0,
  "CROWPANEL_P4_7IN_V1_0",
  TOUCH_GT911,
  {
    8,
    7,
    6,
    9,
    53,
    54,
    10,
    9,
    53,
    54
  },
  AUDIO_OUT,
  DISPLAY_PINS,
  DISPLAY_TIMING,
  "V1.0 wireless mapping confirmed against Elecrow's official V1.0 Arduino examples (board_config.h). Verify your board revision before driver work."
};

static const HardwareProfile PROFILE_V1_1 = {
  CROWPANEL_P4_7IN_V1_1,
  "CROWPANEL_P4_7IN_V1_1",
  TOUCH_GT911,
  {
    8,
    7,
    6,
    9,
    53,
    54,
    10,
    9,
    53,
    54
  },
  AUDIO_OUT,
  DISPLAY_PINS,
  DISPLAY_TIMING,
  "V1.1 keeps the V1.0-style wireless pins (matches Elecrow's V1.1 example tree). Verify against board markings."
};

static const HardwareProfile PROFILE_V1_2 = {
  CROWPANEL_P4_7IN_V1_2,
  "CROWPANEL_P4_7IN_V1_2",
  TOUCH_GT911,
  {
    8,
    7,
    6,
    9,
    27,
    28,
    10,
    9,
    27,
    28
  },
  AUDIO_OUT,
  DISPLAY_PINS,
  DISPLAY_TIMING,
  "UNVERIFIED: Official README says V1.2 reallocates wireless socket IO53/IO54 to IO27/IO28 (no V1.2 examples exist upstream yet). If a V1.2 radio fails, try the V1_1 profile before debugging anything else."
};

const HardwareProfile &profileFor(int profileId) {
  switch (profileId) {
    case CROWPANEL_P4_7IN_V1_0:
      return PROFILE_V1_0;
    case CROWPANEL_P4_7IN_V1_1:
      return PROFILE_V1_1;
    case CROWPANEL_P4_7IN_V1_2:
    default:
      return PROFILE_V1_2;
  }
}

const HardwareProfile &activeHardwareProfile() {
  return profileFor(CROWPANEL_HARDWARE_PROFILE);
}

void printHardwareProfile(Stream &out, const HardwareProfile &profile) {
  out.print(F("[hardware] profile="));
  out.println(profile.name);
  out.print(F("[hardware] touch scl="));
  out.print(profile.touch.scl);
  out.print(F(" sda="));
  out.print(profile.touch.sda);
  out.print(F(" int="));
  out.print(profile.touch.interruptPin);
  out.print(F(" reset="));
  out.println(profile.touch.resetPin);
  out.print(F("[hardware] wireless spi clk="));
  out.print(profile.wireless.spiClk);
  out.print(F(" miso="));
  out.print(profile.wireless.spiMiso);
  out.print(F(" mosi="));
  out.print(profile.wireless.spiMosi);
  out.print(F(" sx_irq="));
  out.print(profile.wireless.sx1262Irq);
  out.print(F(" sx_reset="));
  out.println(profile.wireless.sx1262Reset);
  out.print(F("[hardware] display backlight="));
  out.print(profile.display.backlight);
  out.print(F(" lcd_reset="));
  out.println(profile.display.lcdReset);
  out.print(F("[hardware] note="));
  out.println(profile.revisionNote);
}
