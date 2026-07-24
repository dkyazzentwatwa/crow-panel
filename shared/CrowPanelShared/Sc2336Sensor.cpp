// SC2336 image sensor over SCCB. COMPILE-VERIFIED on esp32:esp32:esp32p4
// (core 3.3.8).
//
// PARTIALLY HARDWARE-VERIFIED (V1.2 panel, 2026-07-24): the sensor is present
// and answers on the bus - a scan of SDA=12/SCL=13 found exactly one device at
// 0x30 and read chip id 0xCB3A from registers 0x3107/0x3108. So the bus pins,
// the address, the 16-bit register read path and the part identity are all
// proven on real hardware.
//
// NOT yet proven: the mode table below. Writing ~166 registers and getting a
// valid RAW8 stream out of the CSI link is a different claim from reading two
// ID bytes, and it has not been observed.
//
// The mode table below is ported from Espressif's esp-video-components, file
// esp_cam_sensor/sensors/sc2336/private_include/
// sc2336_mipi_2lane_24Minput_1024x600_raw8_30fps.h, which carries:
//
//     SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
//     SPDX-License-Identifier: Apache-2.0
//
// and is titled
// "Cleaned_0xc7_SC2336_MIPI_24Minput_2Lane_288Mbps_8bit_1024x600_30fps_20240628".
// Only the formatting changed: the two symbolic register names in the original
// (SC2336_REG_SLEEP_MODE -> 0x0100) were expanded to their numeric values and
// the SC2336_REG_END sentinel dropped in favour of a counted array. The values
// are byte-for-byte the upstream ones - do not "tidy" them, they are a vendor
// tuning blob and the ordering is significant.
//
// Why port a table instead of pulling the component in: esp_cam_sensor is an
// ESP-IDF component, not an Arduino library, and the Arduino core does not
// bundle it. Everything downstream of the sensor (CSI receiver, ISP, JPEG,
// PPA) IS already in the core - see CameraBringup.cpp.

#include "Sc2336Sensor.h"

#include <Wire.h>
#include "Logger.h"

namespace {

// SCCB is 16-bit register address, 8-bit value.
constexpr uint32_t kSccbHz = 100000;

// Sensor registers this driver touches by name. Numbers from
// esp-video-components sc2336_regs.h.
constexpr uint16_t kRegSensorIdHigh = 0x3107;
constexpr uint16_t kRegSensorIdLow = 0x3108;
constexpr uint16_t kRegSleepMode = 0x0100;   // 1 = streaming, 0 = standby
constexpr uint16_t kRegFlipMirror = 0x3221;
constexpr uint16_t kRegShutterHigh = 0x3e00;
constexpr uint16_t kRegShutterMid = 0x3e01;
constexpr uint16_t kRegShutterLow = 0x3e02;
constexpr uint16_t kRegDigCoarseGain = 0x3e06;
constexpr uint16_t kRegDigFineGain = 0x3e07;
constexpr uint16_t kRegAnalogGain = 0x3e09;

// Bit fields in 0x3221. The sensor mirrors on a 2-bit field per axis rather
// than a single bit, which is why these are 0x60/0x06 and not 0x02/0x01.
constexpr uint8_t kFlipVerticalMask = 0x60;
constexpr uint8_t kFlipHorizontalMask = 0x06;

// Addresses to try when the configured one does not answer. A module wired to
// a non-default address is a plausible bring-up surprise; silently failing on
// it is not worth the few milliseconds saved.
constexpr uint8_t kFallbackAddrs[] = {0x30, 0x36, 0x32, 0x3C};

}  // namespace

// One SCCB register write. Kept out of the header so the table type is private.
struct Sc2336Reg {
  uint16_t reg;
  uint8_t val;
};

// MIPI 2-lane, 24 MHz input, 288 Mbps/lane, RAW8, 1024x600 @ 30 fps.
// See the provenance note at the top of this file before editing anything here.
static const Sc2336Reg kMode1024x600Raw8[] = {
    {0x0103, 0x01}, {0x0100, 0x00}, {0x36e9, 0x80}, {0x37f9, 0x80},
    {0x301f, 0xc7}, {0x3031, 0x08}, {0x3037, 0x00}, {0x3106, 0x05},
    {0x3200, 0x01}, {0x3201, 0xb4}, {0x3202, 0x00}, {0x3203, 0xf0},
    {0x3204, 0x05}, {0x3205, 0xd3}, {0x3206, 0x03}, {0x3207, 0x4f},
    {0x3208, 0x04}, {0x3209, 0x00}, {0x320a, 0x02}, {0x320b, 0x58},
    {0x320c, 0x09}, {0x320d, 0x60}, {0x320e, 0x03}, {0x320f, 0xe8},
    {0x3210, 0x00}, {0x3211, 0x10}, {0x3212, 0x00}, {0x3213, 0x04},
    {0x3248, 0x04}, {0x3249, 0x0b}, {0x3253, 0x08}, {0x3301, 0x09},
    {0x3302, 0xff}, {0x3303, 0x10}, {0x3306, 0x60}, {0x3307, 0x02},
    {0x330a, 0x01}, {0x330b, 0x10}, {0x330c, 0x16}, {0x330d, 0xff},
    {0x3318, 0x02}, {0x3321, 0x0a}, {0x3327, 0x0e}, {0x332b, 0x12},
    {0x3333, 0x10}, {0x3334, 0x40}, {0x335e, 0x06}, {0x335f, 0x0a},
    {0x3364, 0x1f}, {0x337c, 0x02}, {0x337d, 0x0e}, {0x3390, 0x09},
    {0x3391, 0x0f}, {0x3392, 0x1f}, {0x3393, 0x20}, {0x3394, 0x20},
    {0x3395, 0xff}, {0x33a2, 0x04}, {0x33b1, 0x80}, {0x33b2, 0x68},
    {0x33b3, 0x42}, {0x33f9, 0x78}, {0x33fb, 0xd8}, {0x33fc, 0x0f},
    {0x33fd, 0x1f}, {0x349f, 0x03}, {0x34a6, 0x0f}, {0x34a7, 0x1f},
    {0x34a8, 0x42}, {0x34a9, 0x06}, {0x34aa, 0x01}, {0x34ab, 0x28},
    {0x34ac, 0x01}, {0x34ad, 0x90}, {0x3630, 0xf4}, {0x3633, 0x22},
    {0x3639, 0xf4}, {0x363c, 0x47}, {0x3670, 0x09}, {0x3674, 0xf4},
    {0x3675, 0xfb}, {0x3676, 0xed}, {0x367c, 0x09}, {0x367d, 0x0f},
    {0x3690, 0x22}, {0x3691, 0x22}, {0x3692, 0x22}, {0x3698, 0x89},
    {0x3699, 0x96}, {0x369a, 0xd0}, {0x369b, 0xd0}, {0x369c, 0x09},
    {0x369d, 0x0f}, {0x36a2, 0x09}, {0x36a3, 0x0f}, {0x36a4, 0x1f},
    {0x36d0, 0x01}, {0x36ea, 0x08}, {0x36eb, 0x0a}, {0x36ec, 0x1a},
    {0x36ed, 0x18}, {0x3722, 0xe1}, {0x3724, 0x41}, {0x3725, 0xc1},
    {0x3728, 0x20}, {0x37fa, 0x08}, {0x37fb, 0x32}, {0x37fc, 0x11},
    {0x37fd, 0x37}, {0x3900, 0x0d}, {0x3905, 0x98}, {0x391b, 0x81},
    {0x391c, 0x10}, {0x3933, 0x81}, {0x3934, 0xc5}, {0x3940, 0x68},
    {0x3941, 0x00}, {0x3942, 0x01}, {0x3943, 0xc6}, {0x3952, 0x02},
    {0x3953, 0x0f}, {0x3e01, 0x37}, {0x3e02, 0xe0}, {0x3e08, 0x1f},
    {0x3e1b, 0x14}, {0x4509, 0x38}, {0x4819, 0x05}, {0x481b, 0x03},
    {0x481d, 0x0a}, {0x481f, 0x02}, {0x4821, 0x08}, {0x4823, 0x03},
    {0x4825, 0x02}, {0x4827, 0x03}, {0x4829, 0x04}, {0x5799, 0x06},
    {0x5ae0, 0xfe}, {0x5ae1, 0x40}, {0x5ae2, 0x30}, {0x5ae3, 0x28},
    {0x5ae4, 0x20}, {0x5ae5, 0x30}, {0x5ae6, 0x28}, {0x5ae7, 0x20},
    {0x5ae8, 0x3c}, {0x5ae9, 0x30}, {0x5aea, 0x28}, {0x5aeb, 0x3c},
    {0x5aec, 0x30}, {0x5aed, 0x28}, {0x5aee, 0xfe}, {0x5aef, 0x40},
    {0x5af4, 0x30}, {0x5af5, 0x28}, {0x5af6, 0x20}, {0x5af7, 0x30},
    {0x5af8, 0x28}, {0x5af9, 0x20}, {0x5afa, 0x3c}, {0x5afb, 0x30},
    {0x5afc, 0x28}, {0x5afd, 0x3c}, {0x5afe, 0x30}, {0x5aff, 0x28},
    {0x36e9, 0x53}, {0x37f9, 0x53},
};

bool Sc2336Sensor::writeReg(uint16_t reg, uint8_t value) {
  Wire1.beginTransmission(addr_);
  Wire1.write((uint8_t)(reg >> 8));
  Wire1.write((uint8_t)(reg & 0xFF));
  Wire1.write(value);
  return Wire1.endTransmission() == 0;
}

bool Sc2336Sensor::readReg(uint16_t reg, uint8_t &value) {
  Wire1.beginTransmission(addr_);
  Wire1.write((uint8_t)(reg >> 8));
  Wire1.write((uint8_t)(reg & 0xFF));
  // Repeated START (endTransmission(false)) - a STOP here would make some
  // sensors forget the register pointer.
  if (Wire1.endTransmission(false) != 0) return false;
  if (Wire1.requestFrom((int)addr_, 1) != 1) return false;
  value = Wire1.read();
  return true;
}

bool Sc2336Sensor::probeId(uint8_t addr) {
  const uint8_t saved = addr_;
  addr_ = addr;
  uint8_t hi = 0, lo = 0;
  const bool ok = readReg(kRegSensorIdHigh, hi) && readReg(kRegSensorIdLow, lo);
  if (!ok) {
    addr_ = saved;
    return false;
  }
  chipId_ = ((uint16_t)hi << 8) | lo;
  if (chipId_ != kChipId) {
    addr_ = saved;
    return false;
  }
  return true;
}

bool Sc2336Sensor::begin(const CameraPins &pins, uint8_t addr) {
  pins_ = &pins;
  ready_ = false;
  streaming_ = false;
  chipId_ = 0;

  // Release reset / enable the camera rail BEFORE the bus. On this board IO11
  // carries the CSI_RESET net, which the V1.2 schematic places beside an LDO
  // enable and the DOVDD_1V8 rail - so it may be gating the sensor's supply,
  // not just its reset. Either way the sensor cannot answer until this is high.
  // 20 ms is far longer than any sensor needs to come out of reset and costs
  // nothing on a one-time bring-up path.
  if (pins.resetPin >= 0) {
    pinMode(pins.resetPin, OUTPUT);
    digitalWrite(pins.resetPin, LOW);
    delay(5);
    digitalWrite(pins.resetPin, HIGH);
    delay(20);
  }
  if (pins.pwdnPin >= 0) {
    pinMode(pins.pwdnPin, OUTPUT);
    digitalWrite(pins.pwdnPin, LOW);  // active-high power-down: hold it awake
    delay(5);
  }

  if (!Wire1.begin(pins.sccbSda, pins.sccbScl, kSccbHz)) {
    lastError_ = "SCCB bus would not start";
    Logger::warn("sc2336", lastError_);
    return false;
  }

  // Try the caller's address (or the profile's) first, then the known
  // alternatives. Reporting the address we actually landed on matters: it is
  // the value to write back into HardwareProfile if it differs.
  const uint8_t preferred = addr != 0 ? addr : pins.sccbAddr;
  if (probeId(preferred)) {
    ready_ = true;
  } else {
    for (uint8_t candidate : kFallbackAddrs) {
      if (candidate == preferred) continue;
      if (probeId(candidate)) {
        ready_ = true;
        Logger::warn("sc2336", "sensor answered at 0x" + String(candidate, HEX) +
                                   ", not the profile's 0x" + String(preferred, HEX) +
                                   " - update HardwareProfile");
        break;
      }
    }
  }

  if (!ready_) {
    lastError_ = "no SC2336 on the SCCB bus";
    Logger::warn("sc2336", String(lastError_) + " (sda=" + String(pins.sccbSda) +
                               " scl=" + String(pins.sccbScl) + "); check the ribbon seating");
    return false;
  }

  lastError_ = "";
  Logger::info("sc2336", "found chip id 0x" + String(chipId_, HEX) + " at 0x" +
                             String(addr_, HEX));
  return true;
}

bool Sc2336Sensor::writeTable(const Sc2336Reg *table, size_t count) {
  for (size_t i = 0; i < count; i++) {
    if (!writeReg(table[i].reg, table[i].val)) {
      lastError_ = "SCCB write failed mid-table";
      Logger::warn("sc2336", "write failed at table index " + String((uint32_t)i) +
                                 " (reg 0x" + String(table[i].reg, HEX) + ")");
      return false;
    }
  }
  return true;
}

bool Sc2336Sensor::configure() {
  if (!ready_) {
    lastError_ = "configure() before a successful begin()";
    return false;
  }

  if (!writeTable(kMode1024x600Raw8, sizeof(kMode1024x600Raw8) / sizeof(kMode1024x600Raw8[0]))) {
    return false;
  }

  // The table's first entry is a software reset; give the part a moment before
  // anything reads back from it.
  delay(10);

  // Land on a mid-range exposure rather than whatever the reset left behind, so
  // the very first frames are in the neighbourhood of correct even before the
  // AE loop has run. Half of the frame height is a reasonable starting point in
  // typical indoor light.
  setExposure(maxExposure() / 2);
  setAnalogGain(0x00);

  streaming_ = false;  // the table leaves the sensor in standby
  lastError_ = "";
  Logger::info("sc2336", "configured 1024x600 RAW8 @30fps (2-lane MIPI, 288Mbps)");
  return true;
}

bool Sc2336Sensor::setStreaming(bool on) {
  if (!ready_) return false;
  if (!writeReg(kRegSleepMode, on ? 0x01 : 0x00)) {
    lastError_ = "could not toggle streaming";
    return false;
  }
  streaming_ = on;
  return true;
}

bool Sc2336Sensor::setExposure(uint32_t halfLines) {
  if (!ready_) return false;
  if (halfLines < 1) halfLines = 1;
  if (halfLines > maxExposure()) halfLines = maxExposure();

  // 20-bit value split 4 / 8 / 4 across three registers, with the low nibble
  // left-shifted into the high half of 0x3e02.
  const uint8_t hi = (uint8_t)((halfLines >> 12) & 0x0F);
  const uint8_t mid = (uint8_t)((halfLines >> 4) & 0xFF);
  const uint8_t lo = (uint8_t)((halfLines & 0x0F) << 4);

  if (!writeReg(kRegShutterHigh, hi) || !writeReg(kRegShutterMid, mid) ||
      !writeReg(kRegShutterLow, lo)) {
    lastError_ = "exposure write failed";
    return false;
  }
  exposure_ = halfLines;
  return true;
}

bool Sc2336Sensor::setAnalogGain(uint8_t reg) {
  if (!ready_) return false;
  if (!writeReg(kRegAnalogGain, reg)) {
    lastError_ = "analog gain write failed";
    return false;
  }
  analogGain_ = reg;
  return true;
}

bool Sc2336Sensor::setDigitalGain(uint8_t coarse, uint8_t fine) {
  if (!ready_) return false;
  if (!writeReg(kRegDigCoarseGain, coarse) || !writeReg(kRegDigFineGain, fine)) {
    lastError_ = "digital gain write failed";
    return false;
  }
  return true;
}

bool Sc2336Sensor::setFlip(bool vertical, bool horizontal) {
  if (!ready_) return false;
  uint8_t value = 0;
  if (!readReg(kRegFlipMirror, value)) {
    lastError_ = "flip read-modify-write failed";
    return false;
  }
  value = vertical ? (uint8_t)(value | kFlipVerticalMask)
                   : (uint8_t)(value & ~kFlipVerticalMask);
  value = horizontal ? (uint8_t)(value | kFlipHorizontalMask)
                     : (uint8_t)(value & ~kFlipHorizontalMask);
  if (!writeReg(kRegFlipMirror, value)) {
    lastError_ = "flip write failed";
    return false;
  }
  vflip_ = vertical;
  hmirror_ = horizontal;
  return true;
}

void Sc2336Sensor::printStatus(Print &out) const {
  out.print(F("[sc2336] "));
  if (!ready_) {
    out.print(F("absent ("));
    out.print(lastError_);
    out.println(')');
    return;
  }
  out.print(F("id=0x"));
  out.print(chipId_, HEX);
  out.print(F(" addr=0x"));
  out.print(addr_, HEX);
  out.print(F(" mode="));
  out.print(kWidth);
  out.print('x');
  out.print(kHeight);
  out.print(F("@"));
  out.print(kFps);
  out.print(F(" raw8 "));
  out.print(streaming_ ? F("streaming") : F("standby"));
  out.print(F(" exp="));
  out.print(exposure_);
  out.print('/');
  out.print(maxExposure());
  out.print(F(" again=0x"));
  out.print(analogGain_, HEX);
  out.print(F(" flip="));
  out.print(vflip_ ? F("v") : F("-"));
  out.println(hmirror_ ? F("h") : F("-"));
}

