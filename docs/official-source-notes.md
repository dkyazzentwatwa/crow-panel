# Official Source Notes

Use these sources as the truth anchors before replacing mock code with real drivers.

## Product Page

https://www.elecrow.com/crowpanel-advanced-7inch-esp32-p4-hmi-ai-display-1024x600-ips-touch-screen-with-wifi-6-compatible-with-arduino-lvgl-micropython.html

Useful facts reflected in this scaffold:

- CrowPanel Advanced 7-inch ESP32-P4 HMI AI Display
- ESP32-P4NRW32 main chip
- 1024x600 IPS capacitive touch display
- 16 MB Flash and 32 MB PSRAM
- Onboard ESP32-C6 wireless module
- Audio amplifier, dual microphones, and dual speakers

## Official GitHub

https://github.com/Elecrow-RD/CrowPanel-Advanced-7inch-ESP32-P4-HMI-AI-Display-1024x600-IPS-Touch-Screen

Useful facts reflected in this scaffold:

- Official LVGL dependency: `lvgl/lvgl@9.2`
- README lists hardware/software V1.2 as latest
- V1.2 changes wireless module socket pin allocation

## Rule For This Repo

Treat this scaffold as compile-oriented tutorial code. Treat Elecrow's examples plus your physical board revision as the authority for final hardware driver wiring.
