#include "UsbTransport.h"
#include "HidTypes.h"

#if CYPHER_KEYS_USB_LIVE
#include "USB.h"
#include "USBHIDConsumerControl.h"
#include "USBHIDKeyboard.h"
#include "USBHIDMouse.h"
namespace {
USBHIDKeyboard gKeyboard;
USBHIDMouse gMouse;
USBHIDConsumerControl gConsumer;
void pressMods(uint8_t mods) {
  if (mods & kModCmd) gKeyboard.press(KEY_LEFT_GUI);
  if (mods & kModShift) gKeyboard.press(KEY_LEFT_SHIFT);
  if (mods & kModOpt) gKeyboard.press(KEY_LEFT_ALT);
  if (mods & kModCtrl) gKeyboard.press(KEY_LEFT_CTRL);
}
}  // namespace
#endif

void UsbTransport::begin() {
#if CYPHER_KEYS_USB_LIVE
  gKeyboard.begin();
  gMouse.begin();
  gConsumer.begin();
  USB.begin();
#endif
}

void UsbTransport::keyDown(uint8_t mods, uint8_t key) {
#if CYPHER_KEYS_USB_LIVE
  pressMods(mods);
  if (key) gKeyboard.press(key);
#else
  (void)mods; (void)key;
#endif
}
void UsbTransport::keyUp() {
#if CYPHER_KEYS_USB_LIVE
  gKeyboard.releaseAll();
#endif
}
void UsbTransport::consumerDown(uint16_t usage) {
#if CYPHER_KEYS_USB_LIVE
  gConsumer.press(usage);
#else
  (void)usage;
#endif
}
void UsbTransport::consumerUp() {
#if CYPHER_KEYS_USB_LIVE
  gConsumer.release();
#endif
}
void UsbTransport::mouseMove(int8_t dx, int8_t dy) {
#if CYPHER_KEYS_USB_LIVE
  gMouse.move(dx, dy);
#else
  (void)dx; (void)dy;
#endif
}
void UsbTransport::mouseDown(uint8_t button) {
#if CYPHER_KEYS_USB_LIVE
  gMouse.press(button);
#else
  (void)button;
#endif
}
void UsbTransport::mouseUp(uint8_t button) {
#if CYPHER_KEYS_USB_LIVE
  gMouse.release(button);
#else
  (void)button;
#endif
}
void UsbTransport::mouseWheel(int8_t wheel) {
#if CYPHER_KEYS_USB_LIVE
  gMouse.move(0, 0, wheel);
#else
  (void)wheel;
#endif
}
