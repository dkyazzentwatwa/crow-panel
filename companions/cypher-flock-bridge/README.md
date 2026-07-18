# Cypher Flock ESP32 BLE Aggregator

Headless generic ESP32 DevKit companion for Project 16. Wi-Fi is disabled. It
runs passive NimBLE matching, validates and sequences BW16 events, exposes
separate health nodes, drives the detection LED, and sends one JSON stream to
the CrowPanel.

```sh
CTAGS_WORKAROUND=1 ./scripts/build-flock-bridge.sh
arduino-cli board list
arduino-cli upload \
  --fqbn 'esp32:esp32:esp32:PartitionScheme=huge_app' \
  -p <DETECTED_PORT> \
  --input-dir _arduino-build/cypher-flock-bridge
```

Panel UART1 uses RX16/TX17. BW16 UART2 uses RX32/TX33. Both use 115200 baud.
USB Serial accepts the same routed commands as the panel.

This scanner does not join target networks, deauthenticate clients, capture
credentials, or forward packet payloads. It forwards only signature-derived
detection metadata, sanitized calibration observations, and diagnostics.
