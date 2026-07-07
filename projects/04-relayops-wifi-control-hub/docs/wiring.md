# RelayOps wiring

RelayOps has **no radio module and no sensor wiring on the panel itself** — it
is a Wi-Fi hub. All the interesting hardware lives on the *remote* ESP32 nodes,
reached over the LAN.

## The panel (CrowPanel Advanced 7-inch ESP32-P4)

- Wi-Fi rides the onboard ESP32-C6 (esp_hosted over SDIO); nothing to wire.
- Optional: an on-board status LED mirroring the hub's link state. Set
  `RELAYOPS_STATUS_LED_PIN` in `config/Pins.h` to a FREE GPIO — it must not
  collide with the DSI backlight/reset (IO31/IO41), the touch I2C bus
  (IO45/46/42/40), or the wireless-socket SPI pins. Verify against the board
  silk first. (The current firmware does not drive this pin yet; the define is
  reserved for a later lesson.)

## A remote actuator node (any ESP32)

The hub sends `GET http://<node-ip><path>?pin=<n>&state=<0|1>`. A minimal node:

- Relay/LED module signal pin → the GPIO you list in `config/Devices.h`
- Relay VCC/GND per its module (most relay boards are 5 V coil, 3.3 V logic)
- Node joins the same Wi-Fi network as the hub and serves `<path>` (default
  `/gpio`), reading `pin`/`state` query args and driving the pin.

## A remote sensor node (any ESP32 + sensor)

- Sensor (e.g. DHT22, BME280) wired to the node per that sensor's datasheet
- Node POSTs JSON to `http://<hub-ip>/sensor` on its own cadence:
  `{"nodeId":"attic","temperatureC":29.5,"humidityPct":40,"batteryPct":88,"rssi":-58}`
- A node that is both sensor and actuator can add `"control_url"` +`"pin"` to
  that POST to self-register as a controllable device.

No node hardware is required to try the firmware — see `docs/tutorial.md` and
`mock-api/README.md` for the hardware-free path.
