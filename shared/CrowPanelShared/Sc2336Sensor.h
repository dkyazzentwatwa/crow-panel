#ifndef CROW_PANEL_SC2336_SENSOR_H
#define CROW_PANEL_SC2336_SENSOR_H

#include <Arduino.h>
#include "AppConfig.h"
#include "HardwareProfile.h"

// SC2336 image sensor over SCCB (I2C), the camera on the CrowPanel Advanced
// 7-inch camera header.
//
// This is deliberately a plain Arduino I2C driver with no ESP-IDF dependency:
// it only knows how to talk to the sensor. Bringing up the MIPI-CSI receiver
// and the ISP that consume its output is CameraBringup's job. Splitting them
// this way means the sensor can be probed and configured (and its registers
// dumped) on any build, including one where USE_CAMERA_DRIVER is off and the
// CSI hardware is never touched.
//
// Register access is SCCB-style: 16-bit register address, 8-bit value.
//
// Sources: Espressif esp-video-components (Apache-2.0) for the register table
// and control semantics, Elecrow's Lesson13-Camera_Real-Time for the bus pins.
// See Sc2336Sensor.cpp for the exact provenance of the mode table.

class Sc2336Sensor {
 public:
  // 7-bit SCCB address and the chip ID this part reports, from Espressif's
  // sc2336.h (SC2336_SCCB_ADDR / SC2336_PID).
  static constexpr uint8_t kDefaultAddr = 0x30;
  static constexpr uint16_t kChipId = 0xCB3A;

  // The one mode this driver ships: MIPI 2-lane, 24 MHz input, RAW8,
  // 1024x600 @ 30 fps - chosen because it is exactly the panel's resolution.
  static constexpr uint16_t kWidth = 1024;
  static constexpr uint16_t kHeight = 600;
  static constexpr uint8_t kFps = 30;

  // Frame height in sensor line units for the mode above, read straight out of
  // the mode table's 0x320e/0x320f pair (0x03E8). Exposure is expressed in
  // half-lines and the sensor cannot expose longer than the frame it is reading
  // out, so this bounds setExposure(). Keep it in sync if the table ever
  // changes - an over-long exposure is silently clamped by the sensor and shows
  // up as an AE loop that will not converge.
  static constexpr uint16_t kVts = 0x03E8;  // 1000 lines

  // Opens the SCCB bus and confirms the part answers with kChipId. `addr` of 0
  // means "use profile.camera.sccbAddr, and if that does not answer, probe the
  // few other addresses these modules ship on" - a wrong-address guess is the
  // single most likely bring-up failure, so it self-corrects rather than
  // reporting a dead sensor.
  //
  // Returns false if nothing on the bus identifies as an SC2336. Call
  // lastError() for a human-readable reason.
  bool begin(const CameraPins &pins, uint8_t addr = 0);

  // Writes the 1024x600 RAW8 mode table. Leaves the sensor in software standby;
  // call setStreaming(true) once the CSI receiver is armed and ready for data.
  // Streaming before the receiver exists gets you a burst of dropped frames and
  // a confusing first-frame timeout.
  bool configure();

  bool setStreaming(bool on);
  bool streaming() const { return streaming_; }

  // --- Exposure and gain ---------------------------------------------------
  //
  // These exist because the Arduino core ships the ISP's statistics engines but
  // not esp_video's software AE/AWB pipeline controller, so the auto-exposure
  // loop has to live in our code and drive the sensor directly. They are also
  // what the UI's manual exposure control writes to.

  // Exposure in sensor half-lines, 1 .. (kVts - 6) * 2. The sensor splits the
  // value across 0x3e00/0x3e01/0x3e02 in a 4/8/4-bit layout.
  bool setExposure(uint32_t halfLines);
  uint32_t exposure() const { return exposure_; }
  static constexpr uint32_t maxExposure() { return (uint32_t)(kVts - 6) * 2; }

  // Analog gain register value (0x3e09). Not a linear multiplier - it is a
  // coarse/fine encoded field, so callers should treat it as an opaque dial
  // and step it, which is what the AE loop does.
  bool setAnalogGain(uint8_t reg);
  uint8_t analogGain() const { return analogGain_; }

  // Digital gain, split coarse (0x3e06) / fine (0x3e07).
  bool setDigitalGain(uint8_t coarse, uint8_t fine);

  // Orientation. Both default off; the panel and sensor are mounted the same
  // way up, but a case may flip either.
  bool setFlip(bool vertical, bool horizontal);
  bool flippedVertically() const { return vflip_; }
  bool flippedHorizontally() const { return hmirror_; }

  // --- Diagnostics ---------------------------------------------------------
  bool ready() const { return ready_; }
  uint8_t address() const { return addr_; }
  uint16_t chipId() const { return chipId_; }
  const char *lastError() const { return lastError_; }

  // Raw register access, exposed for bring-up and the `cam regs` command.
  bool readReg(uint16_t reg, uint8_t &value);
  bool writeReg(uint16_t reg, uint8_t value);

  void printStatus(Print &out) const;

 private:
  // Reads the two ID registers into chipId_. Returns false on any bus error.
  bool probeId(uint8_t addr);
  bool writeTable(const struct Sc2336Reg *table, size_t count);

  const CameraPins *pins_ = nullptr;
  uint8_t addr_ = kDefaultAddr;
  uint16_t chipId_ = 0;
  uint32_t exposure_ = 0;
  uint8_t analogGain_ = 0;
  bool ready_ = false;
  bool streaming_ = false;
  bool vflip_ = false;
  bool hmirror_ = false;
  const char *lastError_ = "not started";
};

#endif
