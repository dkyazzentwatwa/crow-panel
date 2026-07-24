// Stage 0 hardware probe for the CrowPanel Advanced 7" ESP32-P4 camera header.
//
// Answers one question before any driver is written: WHAT is actually on the
// SCCB bus, and at which address? Elecrow's Lesson13-Camera_Real-Time puts the
// camera SCCB on I2C port 1, SCL = IO13, SDA = IO12 (bsp_camera.h). The sensor
// is documented as an SC2336, whose ID lives in 16-bit registers 0x3107/0x3108.
// This sketch confirms both rather than assuming either.
//
// Results render on the PANEL, not Serial: with USBMode=hwcdc the native CDC
// port drops the moment the app runs (see docs/c6-wifi-handoff.md), so serial
// output from a running sketch is unreliable on this board. Serial printing is
// kept anyway for the case where a UART adapter is attached.
//
// Build:
//   CTAGS_WORKAROUND=1 EXTRA_FLAGS="-DUSE_DISPLAY=1" \
//     ./scripts/upload-project.sh <this dir> /dev/cu.usbmodemNN01

#include <CrowPanelShared.h>
#include <Wire.h>

// Camera SCCB bus, from Elecrow Lesson13 peripheral/bsp_camera/include/bsp_camera.h:
//   #define SCCB_MASTER_PORT 1 / SCCB_GPIO_SCL 13 / SCCB_GPIO_SDA 12
// Wire1 is free: the GT911 touch controller sits on Wire (port 0) at IO45/IO46.
constexpr int kSccbSda = 12;
constexpr int kSccbScl = 13;

// CSI_RESET, from the V1.2 schematic (net "IO11_CSI_RESET"). Elecrow's own
// lesson passes reset_pin = -1, but the net exists and sits beside an LDO CE
// pin and the DOVDD_1V8 rail - so it may be enabling the camera's supply rather
// than only releasing its reset. Driving it high first costs nothing and rules
// out the single most confusing failure: a completely silent sensor that looks
// exactly like a badly seated ribbon.
constexpr int kCsiReset = 11;

// SC2336 sensor ID registers and chip ID, from esp-video-components
// (sc2336_regs.h SC2336_REG_SENSOR_ID_H/L, sc2336.h SC2336_PID = 0xcb3a).
constexpr uint16_t kRegSensorIdHigh = 0x3107;
constexpr uint16_t kRegSensorIdLow = 0x3108;
constexpr uint16_t kSc2336ChipId = 0xCB3A;

// Espressif's driver declares SC2336_SCCB_ADDR = 0x30, so that leads. The rest
// are addresses these modules have been seen on; the bus scan is the real
// answer and these only drive ID-read attempts if the scan comes back empty
// (some sensors NAK a bare address probe but answer a register read).
constexpr uint8_t kCandidateAddrs[] = {0x30, 0x36, 0x32, 0x3C};

uint8_t foundAddrs[8];
uint8_t foundCount = 0;

// Reports each line to the panel and to Serial. `row` is a CrowDisplay status
// row (0-5); pass -1 for Serial-only detail that would overflow the 6-row panel.
void report(int row, const String &text) {
  Serial.println(text);
  if (row >= 0) CrowDisplay::setLine((uint8_t)row, text);
}

// SCCB register read: 16-bit register address, 8-bit value. Returns false on
// any NAK or short read so a missing sensor never looks like a value of 0.
bool readReg(uint8_t addr, uint16_t reg, uint8_t &value) {
  Wire1.beginTransmission(addr);
  Wire1.write((uint8_t)(reg >> 8));
  Wire1.write((uint8_t)(reg & 0xFF));
  if (Wire1.endTransmission(false) != 0) return false;
  if (Wire1.requestFrom((int)addr, 1) != 1) return false;
  value = Wire1.read();
  return true;
}

String hex8(uint8_t v) {
  char buf[5];
  snprintf(buf, sizeof(buf), "0x%02X", v);
  return String(buf);
}

String hex16(uint16_t v) {
  char buf[7];
  snprintf(buf, sizeof(buf), "0x%04X", v);
  return String(buf);
}

// Walks the 7-bit address space. Address 0x00 and 0x78-0x7F are reserved.
void scanBus() {
  foundCount = 0;
  String list;
  for (uint8_t addr = 0x08; addr < 0x78; addr++) {
    Wire1.beginTransmission(addr);
    if (Wire1.endTransmission() != 0) continue;
    if (foundCount < sizeof(foundAddrs)) foundAddrs[foundCount++] = addr;
    if (list.length()) list += " ";
    list += hex8(addr);
  }
  if (foundCount == 0) {
    report(1, "scan: NO DEVICES on SDA12/SCL13");
  } else {
    report(1, "scan: " + String(foundCount) + " found -> " + list);
  }
}

// Tries the ID registers at `addr`. Reports the raw bytes either way - a
// successful read of an unexpected value identifies a different sensor, which
// is just as useful an answer as a match.
bool identify(uint8_t addr, uint8_t row) {
  uint8_t hi = 0, lo = 0;
  if (!readReg(addr, kRegSensorIdHigh, hi) || !readReg(addr, kRegSensorIdLow, lo)) {
    report(row, hex8(addr) + ": no reply to ID read");
    return false;
  }
  const uint16_t id = ((uint16_t)hi << 8) | lo;
  const bool match = (id == kSc2336ChipId);
  report(row, hex8(addr) + ": id=" + hex16(id) + (match ? "  SC2336 OK" : "  UNKNOWN PART"));
  return match;
}

void setup() {
  Serial.begin(115200);
  delay(400);

  CrowDisplay::begin(activeHardwareProfile(), "CAM PROBE");
  report(0, "SCCB scan: SDA=12 SCL=13 rst=IO11");

  // Power/reset before the bus, and give it far longer than any sensor needs.
  pinMode(kCsiReset, OUTPUT);
  digitalWrite(kCsiReset, LOW);
  delay(10);
  digitalWrite(kCsiReset, HIGH);
  delay(50);

  Wire1.begin(kSccbSda, kSccbScl, 100000);
  delay(50);

  scanBus();

  // Prefer addresses the scan actually saw; fall back to the known candidates
  // so a sensor that NAKs a bare probe still gets an ID read attempt.
  uint8_t row = 2;
  bool identified = false;
  for (uint8_t i = 0; i < foundCount && row < 5; i++) {
    if (identify(foundAddrs[i], row++)) identified = true;
  }
  if (!identified) {
    for (uint8_t i = 0; i < sizeof(kCandidateAddrs) && row < 5; i++) {
      uint8_t addr = kCandidateAddrs[i];
      bool alreadyTried = false;
      for (uint8_t j = 0; j < foundCount; j++) {
        if (foundAddrs[j] == addr) alreadyTried = true;
      }
      if (alreadyTried) continue;
      if (identify(addr, row++)) identified = true;
    }
  }

  report(5, identified ? "RESULT: SC2336 present - record its address"
                       : "RESULT: no SC2336 - check ribbon seating");
}

void loop() {
  CrowDisplay::tick();
  delay(50);
}
