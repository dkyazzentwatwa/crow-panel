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
  "Original placeholder wireless mapping from official README notes. Verify your exact V1.0 board before driver work."
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
  "V1.1 placeholder keeps the original README-style wireless pins. Verify against Elecrow examples and board markings."
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
  "TODO: Official README says V1.2 reallocates wireless socket IO53/IO54 to IO27/IO28 and IO27/IO28 to IO53/IO54. This profile maps the old 53/54 module placeholders to 27/28; verify the shipped board revision before enabling a real radio driver."
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
  out.print(F("[hardware] note="));
  out.println(profile.revisionNote);
}
