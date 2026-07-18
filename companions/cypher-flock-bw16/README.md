# Cypher Flock BW16 Wi-Fi Node

Headless receive-only 2.4/5 GHz Wi-Fi node for Project 16. It is derived from
`/Users/cypher/Documents/GitHub/5ghz-wardriver-bw16` and uses the installed
Realtek AmebaD receive APIs only. No frame-transmission or deauthentication
code is included.

Build from the CrowPanel repository:

```bash
./scripts/build-flock-bw16.sh
```

Wiring to the ESP32 BLE aggregator:

- BW16 Serial1 TX PB1 / board pin 4 -> ESP32 GPIO32 RX
- BW16 Serial1 RX PB2 / board pin 5 <- ESP32 GPIO33 TX
- Shared ground, 3.3 V UART, 115200 baud
- Power both boards separately over USB during bring-up

The default radio path uses `RTW_PROMISC_ENABLE_2` and targeted non-DFS channel
hopping. If raw initialization fails, the node visibly enters `scan-fallback`
and uses lower-level receive-only passive beacon scans. Raw 5 GHz operation remains compile-ready,
not hardware-proven, until channel 36+ frame counters rise on a real BW16.
