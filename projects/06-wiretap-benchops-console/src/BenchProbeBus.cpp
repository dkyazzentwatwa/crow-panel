// WireTap probe path: COMPILE-VERIFIED on esp32:esp32:esp32p4 (core 3.3.x).
// NOT HARDWARE-VERIFIED. Default builds remain mock-only. With
// USE_BENCH_PROBES=1, commands perform read-oriented bench checks:
// GPIO input/high-Z, I2C address scan, and UART RX. SPI read-ID clocking is
// disabled until WIRETAP_ALLOW_SPI_ID_CLOCKING=1 because it must drive
// CS/SCK/MOSI. No probe pins are guessed; copy config/Pins.example.h to Pins.h.

#include "BenchProbeBus.h"

#if USE_BENCH_PROBES
#include <SPI.h>
#include <Wire.h>
#endif

namespace {
BenchProbeResult makeResult(bool ok, const String &line, const String &title,
                            const String &detail, const String &event) {
  BenchProbeResult result;
  result.ok = ok;
  result.serialLine = line;
  result.dashboardTitle = title;
  result.dashboardDetail = detail;
  result.event = event;
  return result;
}

String hexByte(uint8_t value) {
  char buffer[5];
  snprintf(buffer, sizeof(buffer), "%02X", value);
  return String(buffer);
}

bool parsePinArg(String args, int &pin) {
  args.trim();
  if (args.startsWith("get")) {
    args.remove(0, 3);
    args.trim();
  }
  if (args.length() == 0) {
    return false;
  }
  for (uint16_t i = 0; i < args.length(); i++) {
    if (!isDigit(args.charAt(i))) {
      return false;
    }
  }
  pin = args.toInt();
  return true;
}

bool pinConfigured(int pin) {
  return pin >= 0;
}

bool gpioInRange(int pin) {
  return pin >= 0 && pin <= 54;
}

bool isPanelCriticalPin(const HardwareProfile *profile, int pin) {
  if (profile == nullptr) {
    return false;
  }
  return pin == profile->touch.scl ||
         pin == profile->touch.sda ||
         pin == profile->touch.interruptPin ||
         pin == profile->touch.resetPin ||
         pin == profile->display.backlight ||
         pin == profile->display.lcdReset ||
         pin == profile->audio.lrclk ||
         pin == profile->audio.bclk ||
         pin == profile->audio.sdata ||
         pin == profile->audio.control;
}

String pinSafetyNote(const HardwareProfile *profile, int pin) {
  if (profile == nullptr) {
    return "profile unavailable";
  }
  if (pin == profile->wireless.spiClk ||
      pin == profile->wireless.spiMiso ||
      pin == profile->wireless.spiMosi ||
      pin == profile->wireless.sx1262Busy ||
      pin == profile->wireless.sx1262Irq ||
      pin == profile->wireless.sx1262Reset ||
      pin == profile->wireless.sx1262Nss ||
      pin == profile->wireless.nrf24Irq ||
      pin == profile->wireless.nrf24Ce ||
      pin == profile->wireless.nrf24Cs) {
    return "wireless-socket pin; remove socket modules first";
  }
  return "external GPIO only; 3.3V max";
}

String unconfiguredDetail(const char *busName, const char *pinNames) {
  return String(busName) + " pins unset|" + pinNames +
         "|Copy config/Pins.example.h to Pins.h";
}
}  // namespace

void BenchProbeBus::begin(const HardwareProfile &profile) {
  profile_ = &profile;
#if USE_BENCH_PROBES
  Logger::warn("wiretap", "USE_BENCH_PROBES=1; no pins are configured until a Serial command runs");
#else
  Logger::info("wiretap", "bench probes disabled; mock protocol output only");
#endif
}

BenchProbeResult BenchProbeBus::gpioRead(const String &args) {
#if USE_BENCH_PROBES
  int pin = -1;
  if (!parsePinArg(args, pin)) {
    return makeResult(false, "[gpio] usage: gpio get <pin>", "GPIO Read",
                      "Missing numeric pin|No GPIO state changed", "GPIO read rejected");
  }
  if (!gpioInRange(pin)) {
    return makeResult(false, String("[gpio] invalid pin ") + pin, "GPIO Read",
                      "Valid CrowPanel GPIO range here is 0-54|No GPIO state changed",
                      "GPIO read invalid pin");
  }
  if (isPanelCriticalPin(profile_, pin) && !WIRETAP_ALLOW_PANEL_RESERVED_PINS) {
    return makeResult(false, String("[gpio] refused panel-reserved pin ") + pin,
                      "GPIO Read",
                      "Display/touch/audio pins are blocked by default|Set WIRETAP_ALLOW_PANEL_RESERVED_PINS=1 only for lab diagnostics",
                      "GPIO read refused reserved pin");
  }

  // INPUT on ESP32 is high-Z: no pull-up, no pull-down, no output drive.
  pinMode(pin, INPUT);
  int value = digitalRead(pin);
  String state = value == HIGH ? "HIGH" : "LOW";
  String detail = String("Pin ") + pin + ": " + state +
                  "|Configured INPUT only, no pullup/pulldown|" +
                  pinSafetyNote(profile_, pin);
  return makeResult(true, String("[gpio] pin ") + pin + " -> " + state +
                          " (INPUT high-Z)",
                    "GPIO Read", detail, String("GPIO read pin ") + pin + "=" + state);
#else
  return makeResult(true, String("[gpio] ") + args + " -> LOW (mock)",
                    "GPIO Read",
                    String("Requested: ") + args +
                      "|Value: LOW (mock)|CrowPanel v1 never drives target pins",
                    "Mock GPIO read");
#endif
}

BenchProbeResult BenchProbeBus::i2cScan(const String &args) {
  (void)args;
#if USE_BENCH_PROBES
  if (!pinConfigured(WIRETAP_I2C_SDA) || !pinConfigured(WIRETAP_I2C_SCL)) {
    return makeResult(false, "[i2c] pins unset; no scan run", "I2C Scan",
                      unconfiguredDetail("I2C", "WIRETAP_I2C_SDA/SCL"),
                      "I2C scan not configured");
  }

  Wire.begin(WIRETAP_I2C_SDA, WIRETAP_I2C_SCL);
  Wire.setClock(WIRETAP_I2C_CLOCK_HZ);

  String found;
  uint8_t count = 0;
  for (uint8_t address = 1; address < 127; address++) {
    Wire.beginTransmission(address);
    uint8_t error = Wire.endTransmission(true);
    if (error == 0) {
      if (found.length() > 0) {
        found += " ";
      }
      found += "0x";
      found += hexByte(address);
      count++;
    }
    delay(2);
  }

  if (count == 0) {
    return makeResult(true, "[i2c] scan -> no addresses", "I2C Scan",
                      String("SDA ") + WIRETAP_I2C_SDA + ", SCL " + WIRETAP_I2C_SCL +
                        "|No ACKs|Address-only START/STOP probes",
                      "I2C scan no devices");
  }
  return makeResult(true, String("[i2c] scan -> ") + found, "I2C Scan",
                    String("Found ") + count + ": " + found +
                      "|Address-only probes, no register writes|" +
                      "External pullups required",
                    "I2C scan found devices");
#else
  return makeResult(true, "[i2c] scan -> 0x3C OLED, 0x50 EEPROM", "I2C Scan",
                    "0x3C OLED found|0x50 EEPROM found|Pullups required on real hardware",
                    "Mock I2C scan");
#endif
}

BenchProbeResult BenchProbeBus::spiId(const String &args) {
  (void)args;
#if USE_BENCH_PROBES
  if (!WIRETAP_ALLOW_SPI_ID_CLOCKING) {
    return makeResult(false, "[spi] ID clocking disabled; set WIRETAP_ALLOW_SPI_ID_CLOCKING=1",
                      "SPI ID",
                      "Default bench probes do not drive CS/SCK/MOSI|Enable only after checking the target datasheet",
                      "SPI ID clocking disabled");
  }
  if (!pinConfigured(WIRETAP_SPI_SCK) ||
      !pinConfigured(WIRETAP_SPI_MISO) ||
      !pinConfigured(WIRETAP_SPI_MOSI) ||
      !pinConfigured(WIRETAP_SPI_CS)) {
    return makeResult(false, "[spi] pins unset; no ID read run", "SPI ID",
                      unconfiguredDetail("SPI", "WIRETAP_SPI_SCK/MISO/MOSI/CS"),
                      "SPI ID not configured");
  }

  SPI.begin(WIRETAP_SPI_SCK, WIRETAP_SPI_MISO, WIRETAP_SPI_MOSI, WIRETAP_SPI_CS);
  // Prime CS high before OUTPUT mode so an explicit read-ID does not glitch-select.
  digitalWrite(WIRETAP_SPI_CS, HIGH);
  pinMode(WIRETAP_SPI_CS, OUTPUT);
  SPI.beginTransaction(SPISettings(WIRETAP_SPI_CLOCK_HZ, MSBFIRST, SPI_MODE0));
  digitalWrite(WIRETAP_SPI_CS, LOW);
  SPI.transfer(WIRETAP_SPI_READ_ID_OPCODE);
  uint8_t id0 = SPI.transfer(WIRETAP_SPI_DUMMY_BYTE);
  uint8_t id1 = SPI.transfer(WIRETAP_SPI_DUMMY_BYTE);
  uint8_t id2 = SPI.transfer(WIRETAP_SPI_DUMMY_BYTE);
  digitalWrite(WIRETAP_SPI_CS, HIGH);
  SPI.endTransaction();

  String id = hexByte(id0) + " " + hexByte(id1) + " " + hexByte(id2);
  return makeResult(true, String("[spi] id -> ") + id, "SPI ID",
                    String("JEDEC-style ID ") + id +
                      "|Drives CS/SCK/MOSI only during this read-ID command|" +
                      "Do not use on unknown targets without a datasheet",
                    "SPI ID read");
#else
  return makeResult(true, "[spi] id -> EF 40 18", "SPI ID",
                    "JEDEC EF 40 18|Mode 0, 1 MHz mock|No real CS toggled",
                    "Mock SPI JEDEC ID");
#endif
}

BenchProbeResult BenchProbeBus::uartRx(const String &args) {
  (void)args;
#if USE_BENCH_PROBES
  if (!pinConfigured(WIRETAP_UART_RX)) {
    return makeResult(false, "[uart] RX pin unset; no UART started", "UART RX",
                      unconfiguredDetail("UART", "WIRETAP_UART_RX"),
                      "UART RX not configured");
  }

  beginUartIfNeeded();
  String frame;
  unsigned long startMs = millis();
  while (millis() - startMs < WIRETAP_UART_READ_MS) {
    while (Serial1.available() > 0) {
      char c = (char)Serial1.read();
      if (c == '\r') {
        continue;
      }
      if (frame.length() < WIRETAP_UART_MAX_BYTES) {
        frame += c;
      }
    }
    delay(2);
  }
  if (frame.length() == 0) {
    return makeResult(true, "[uart] rx -> <no bytes>", "UART RX",
                      String("RX pin ") + WIRETAP_UART_RX + " @" +
                        WIRETAP_UART_BAUD + "|No TX pin configured|RX window " +
                        WIRETAP_UART_READ_MS + " ms",
                      "UART RX no bytes");
  }
  return makeResult(true, String("[uart] rx -> ") + frame, "UART RX",
                    String("RX pin ") + WIRETAP_UART_RX + " @" +
                      WIRETAP_UART_BAUD + "|No TX pin configured|Bytes: " + frame,
                    "UART RX bytes");
#else
  return makeResult(true, "[uart] rx -> OK", "UART RX",
                    "Baud 115200 mock|Received: OK|Cross TX/RX on real wiring",
                    "Mock UART response");
#endif
}

const char *BenchProbeBus::driverName() const {
#if USE_BENCH_PROBES
  return "bench-probes";
#else
  return "mock-probes";
#endif
}

#if USE_BENCH_PROBES
void BenchProbeBus::beginUartIfNeeded() {
  if (uartStarted_) {
    return;
  }
  Serial1.begin(WIRETAP_UART_BAUD, SERIAL_8N1, WIRETAP_UART_RX, -1);
  uartStarted_ = true;
}
#endif
