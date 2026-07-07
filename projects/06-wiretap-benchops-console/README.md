# CrowPanel WireTap BenchOps Console

Mock-first bench-protocol console inspired by `WireTap-32`.

V1 never drives real GPIO. It visualizes safe pin guidance and mock bus results
so the CrowPanel can become the large touch surface before any bench wiring.

## Serial Commands

- `help` / `status` / `history`
- `mode <hiz|i2c|spi|uart|gpio>`
- `pins`
- `i2c scan`
- `spi id`
- `uart rx`
- `gpio get <pin>`

## Proof State

Scaffolded and compile-ready only. Treat all protocol output as mock data.
