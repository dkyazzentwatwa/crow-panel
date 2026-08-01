#include "CrowGamepadTransport.h"

#if CROW_GAMEPAD_USB_LIVE
#include <USB.h>
#include <USBHIDGamepad.h>

static USBHIDGamepad gGamepad;

// Project 23's StickHat values are passed straight through as these HAT_*
// values. If the core ever renumbers them, this breaks loudly here rather
// than silently sending wrong directions.
static_assert(HAT_CENTER == 0 && HAT_UP == 1 && HAT_UP_RIGHT == 2 && HAT_RIGHT == 3 &&
                  HAT_DOWN_RIGHT == 4 && HAT_DOWN == 5 && HAT_DOWN_LEFT == 6 &&
                  HAT_LEFT == 7 && HAT_UP_LEFT == 8,
              "TinyUSB HAT_* values no longer match project 23's StickHat");
#endif

void GamepadTransport::begin() {
  if (begun_) return;
  begun_ = true;
#if CROW_GAMEPAD_USB_LIVE
  gGamepad.begin();
  USB.begin();
#endif
}

void GamepadTransport::sendState(uint8_t hat, uint32_t buttons) {
  if (hat > 8) hat = 0;
  reports_++;
#if CROW_GAMEPAD_USB_LIVE
  // All six axes centred: this is a leverless, there are no analog sticks.
  gGamepad.send(0, 0, 0, 0, 0, 0, hat, buttons);
#else
  (void)hat;
  (void)buttons;
#endif
}
