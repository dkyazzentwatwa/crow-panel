#include "CrowBleTransport.h"

#include "CrowHidKeycodes.h"

#if USE_BLE_HID
#include <WiFi.h>  // WiFi.setPins() configures the shared esp_hosted SDIO link
#include <BLEDevice.h>
#include <BLEHIDDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <CrowPanelShared.h>  // activeHardwareProfile() for hosted SDIO pins

// NimBLE host API to erase all bonds (see clearBonds()); declared here to avoid
// depending on the NimBLE host include path.
extern "C" int ble_store_clear(void);
// NimBLE free mbuf count. The high-rate mouse path checks this before notifying
// and drops the report if the pool is low, so a fast trackpad drag can never
// exhaust the buffers and panic the chip (interval-independent flow control).
extern "C" int os_msys_num_free(void);
static const int kMinFreeMbufs = 4;

namespace {
BLEHIDDevice *gHid = nullptr;
BLECharacteristic *gKbd = nullptr;       // report ID 1
BLECharacteristic *gMouse = nullptr;     // report ID 2
BLECharacteristic *gConsumer = nullptr;  // report ID 3
volatile bool gConnected = false;
bool gStarted = false;

// Combined report descriptor: keyboard (1), mouse (2), consumer (3).
const uint8_t kReportMap[] = {
    // Keyboard, Report ID 1
    0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x85, 0x01, 0x05, 0x07, 0x19, 0xE0,
    0x29, 0xE7, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02,
    0x95, 0x01, 0x75, 0x08, 0x81, 0x03, 0x95, 0x06, 0x75, 0x08, 0x15, 0x00,
    0x25, 0x65, 0x05, 0x07, 0x19, 0x00, 0x29, 0x65, 0x81, 0x00, 0xC0,
    // Mouse, Report ID 2 (3 buttons, X, Y, wheel)
    0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x85, 0x02, 0x09, 0x01, 0xA1, 0x00,
    0x05, 0x09, 0x19, 0x01, 0x29, 0x03, 0x15, 0x00, 0x25, 0x01, 0x95, 0x03,
    0x75, 0x01, 0x81, 0x02, 0x95, 0x01, 0x75, 0x05, 0x81, 0x03, 0x05, 0x01,
    0x09, 0x30, 0x09, 0x31, 0x09, 0x38, 0x15, 0x81, 0x25, 0x7F, 0x75, 0x08,
    0x95, 0x03, 0x81, 0x06, 0xC0, 0xC0,
    // Consumer, Report ID 3 (16-bit usage)
    0x05, 0x0C, 0x09, 0x01, 0xA1, 0x01, 0x85, 0x03, 0x15, 0x00, 0x26, 0xFF,
    0x03, 0x19, 0x00, 0x2A, 0xFF, 0x03, 0x75, 0x10, 0x95, 0x01, 0x81, 0x00,
    0xC0};

class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer *) override { gConnected = true; }
  void onDisconnect(BLEServer *s) override {
    gConnected = false;
    s->getAdvertising()->start();
  }
};

void sendKbd(uint8_t mods, uint8_t usage) {
  if (!gConnected || gKbd == nullptr) return;
  uint8_t report[8] = {mods, 0, usage, 0, 0, 0, 0, 0};
  gKbd->setValue(report, 8);
  gKbd->notify();
}
void sendMouse(uint8_t buttons, int8_t dx, int8_t dy, int8_t wheel) {
  if (!gConnected || gMouse == nullptr) return;
  // Flow control: skip this report if the BLE buffer pool is running low. The
  // connection drains and frees buffers each interval; dropping a mouse report
  // under backpressure is a momentary hitch, never a crash.
  if (os_msys_num_free() < kMinFreeMbufs) return;
  uint8_t report[4] = {buttons, (uint8_t)dx, (uint8_t)dy, (uint8_t)wheel};
  gMouse->setValue(report, 4);
  gMouse->notify();
}
void sendConsumer(uint16_t usage) {
  if (!gConnected || gConsumer == nullptr) return;
  uint8_t report[2] = {(uint8_t)(usage & 0xFF), (uint8_t)(usage >> 8)};
  gConsumer->setValue(report, 2);
  gConsumer->notify();
}
}  // namespace
#endif  // USE_BLE_HID

void BleTransport::setDeviceName(const char *name) {
  if (name != nullptr && name[0] != '\0') deviceName_ = name;
}

void BleTransport::begin() {
#if USE_BLE_HID
  const HostedSdioPins &p = activeHardwareProfile().hostedSdio;
  WiFi.setPins(p.clk, p.cmd, p.d0, p.d1, p.d2, p.d3, p.reset);  // before init!
  BLEDevice::init(deviceName_);

  BLEServer *server = BLEDevice::createServer();
  server->setCallbacks(new ServerCallbacks());

  gHid = new BLEHIDDevice(server);
  gKbd = gHid->inputReport(1);
  gMouse = gHid->inputReport(2);
  gConsumer = gHid->inputReport(3);
  gHid->manufacturer()->setValue("Cypher");
  gHid->pnp(0x02, 0xE502, 0xA111, 0x0210);
  gHid->hidInfo(0x00, 0x01);

  BLESecurity *sec = new BLESecurity();
  sec->setAuthenticationMode(ESP_LE_AUTH_BOND);  // Just Works, no passkey

  gHid->reportMap((uint8_t *)kReportMap, sizeof(kReportMap));
  gHid->startServices();
  gHid->setBatteryLevel(100);

  BLEAdvertising *adv = server->getAdvertising();
  adv->setAppearance(HID_KEYBOARD);
  adv->addServiceUUID(gHid->hidService()->getUUID());
  // Request a fast connection interval (7.5-15 ms) via the preferred-connection
  // parameters. macOS honors this for HID, giving the mouse-report stream enough
  // headroom that a trackpad drag can't back up the BLE tx path.
  adv->setMinPreferred(0x06);  // 6 * 1.25 ms = 7.5 ms
  adv->setMaxPreferred(0x0C);  // 12 * 1.25 ms = 15 ms
  adv->start();
  gStarted = true;
#endif
}

bool BleTransport::ready() const {
#if USE_BLE_HID
  return gConnected;
#else
  return false;
#endif
}
bool BleTransport::advertising() const {
#if USE_BLE_HID
  return gStarted;
#else
  return false;
#endif
}
void BleTransport::clearBonds() {
#if USE_BLE_HID
  // This P4 core is backed by NimBLE (not Bluedroid), so the esp_ble_* bond
  // APIs are unavailable. ble_store_clear() wipes all NimBLE bonds/security
  // material so the host can pair fresh (declared at file scope above).
  ble_store_clear();
#endif
}

void BleTransport::keyDown(uint8_t mods, uint8_t key) {
#if USE_BLE_HID
  uint8_t usage = 0;
  bool needsShift = false;
  if (!hidUsageForKey(key, usage, needsShift)) return;
  uint8_t modByte = hidModifierByte(mods);
  if (needsShift) modByte |= 0x02;  // add Left Shift
  sendKbd(modByte, usage);
#else
  (void)mods; (void)key;
#endif
}
void BleTransport::keyUp() {
#if USE_BLE_HID
  sendKbd(0, 0);
#endif
}
void BleTransport::consumerDown(uint16_t usage) {
#if USE_BLE_HID
  sendConsumer(usage);
#else
  (void)usage;
#endif
}
void BleTransport::consumerUp() {
#if USE_BLE_HID
  sendConsumer(0);
#endif
}
// Mouse over BLE is intentionally disabled: notifying the mouse HID report
// characteristic reliably panics this NimBLE/esp_hosted stack (keyboard,
// consumer, and USB mouse are all fine). The trackpad works over USB; in BLE
// output the trackpad view shows a "USB only" note. These are no-ops so nothing
// can reach the crashing path. (void)-cast args to keep -Wunused quiet.
void BleTransport::mouseMove(int8_t dx, int8_t dy) { (void)dx; (void)dy; }
void BleTransport::mouseDown(uint8_t button) { (void)button; }
void BleTransport::mouseUp(uint8_t button) { (void)button; }
void BleTransport::mouseWheel(int8_t wheel) { (void)wheel; }
