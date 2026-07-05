# CrowPanel Hardware Notes

This scaffold targets the Elecrow CrowPanel Advanced 7-inch ESP32-P4 HMI AI Display. Keep these notes close while turning mock demos into hardware demos.

## Official Hardware Snapshot

- Main chip: ESP32-P4NRW32
- CPU: RISC-V high-performance cores up to 400 MHz plus low-power core up to 40 MHz
- Memory: 16 MB Flash and 32 MB PSRAM
- Display: 7.0-inch IPS, 1024x600
- Touch: capacitive 5-point touch
- Wireless: onboard ESP32-C6 module with Wi-Fi 6, Bluetooth 5.3, and BLE
- Interfaces: USB2.0, UART, I2C, GPIO headers, SD card holder, battery socket, speaker jack, camera header, and module headers
- Audio: amplifier, dual microphones, and dual speakers

## Touch Notes

The official notes show GT911 touch with an I2C address that can be `0x5D` or `0x14` depending on INT level during reset.

Placeholder touch pins in `HardwareProfile`:

- `I2C1_SCL`: IO46
- `I2C1_SDA`: IO45
- `INT_TP`: IO42
- `RESET_TP`: IO40

## Audio Notes

Placeholder audio output pins:

- `AUDIO_GPIO_LRCLK`: IO21
- `AUDIO_GPIO_BCLK`: IO22
- `AUDIO_GPIO_SDATA`: IO23
- `AUDIO_GPIO_CTRL`: IO30

## Revision Boundary

The V1.2 wireless socket change matters. Do not paste module pins directly into project code. Put revision-aware mappings in `shared/CrowPanelShared/HardwareProfile.*`, then verify with your board silkscreen and Elecrow's matching example.
