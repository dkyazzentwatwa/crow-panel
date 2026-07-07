# Mock API

Small Express API for local tutorials and Serial-to-HTTP experiments.

## Run

```sh
cd mock-api
npm install
npm start
```

Default port: `8787`

Use a temporary database for tests:

```sh
DB_PATH=/tmp/crowpanel-aiot-db.json npm start
```

## Endpoints

- `GET /health`
- `POST /events` — body must be a JSON object; `source` truncated to 64 chars, `message` to 256
- `GET /events?limit=N` — newest last; default 100, max 500
- `POST /summary`
- `GET /badges`
- `POST /badges` — `uid` must match `AA:BB:CC:DD` (4–10 hex byte pairs); duplicate uid → `409`; `allowed_zones` up to 16 strings of 32 chars
- `POST /inspection` — `result` must be `"pass"` or `"fail"`
- `GET|POST /gpio?pin=<n>&state=<0|1>` — fake controllable device for project 04 (RelayOps). Logs the command and echoes `{ok, pin, state}`; `pin` must be a non-negative integer

## Project 04 (RelayOps) demo without hardware

RelayOps flips the usual direction: the **panel** runs the web server and the
nodes are HTTP clients. Two things to try against a hub reachable at
`<hub-ip>` (build the sketch with `-DUSE_WIFI=1`):

Push a sensor reading *into* the hub (drives the dashboard roster):

```sh
curl -X POST http://<hub-ip>/sensor -H 'Content-Type: application/json' \
  -d '{"nodeId":"attic","temperatureC":29.5,"humidityPct":40,"batteryPct":88,"motion":false,"rssi":-58}'
```

Let this mock API stand in for a light/relay: set a `ControlDevice` in
`config/Devices.h` with `host` = this machine's IP and `path` = `/gpio`, then
tap the tile (or run `set <id> on`). Each command shows up here as
`[gpio] pin=<n> state=ON`.

## Errors

All errors use one shape:

```json
{ "error": { "code": "invalid_uid", "message": "uid must look like AA:BB:CC:DD (4-10 hex byte pairs)." } }
```

Unknown routes return a JSON `404` (`not_found`), and malformed JSON bodies
return `400` (`invalid_json`) instead of an HTML error page.

## Storage limits

`events` and `inspections` are capped at 500 entries — oldest entries drop
first, mirroring the firmware's fixed-size EventLog ring buffer. `badges`
are not capped (enrollment data), but duplicates are rejected by uid.

The Arduino sketches do not require this server in mock mode. It is here for later Wi-Fi/API demos.
