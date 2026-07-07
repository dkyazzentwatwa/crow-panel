# RelayOps demo script

A Serial-first walkthrough you can film with no extra hardware. 115200 baud,
line ending **Newline**.

## 1. Boot (mock mode)

Flash the default build and open Serial:

```sh
../../scripts/upload-project.sh projects/04-relayops-wifi-control-hub <PORT>
```

Narrate the boot log: hardware profile (`CROWPANEL_P4_7IN_V1_2`), then the
`mock: no web server (USE_WIFI=0); use \`feed\` to inject readings` line — the
hub is honest that it is offline.

## 2. Sensors arrive

Within a few seconds the synthetic source starts printing
`[screen:dashboard] node=... tempC=...`. On a `USE_DISPLAY` build the roster
fills with sensor cards. Tap one to pin its gauges + sparkline.

Inject a specific reading on cue:

```
feed SENSOR,ATTIC,29.5,40,88,0,-58
feed PRESENCE,GARAGE,-70,heartbeat
```

## 3. Control a device

```
devices
set shop-light on
set shop-light off
set porch-relay toggle
```

Each `set` prints `GET http://.../gpio?pin=..&state=.. (mock; log-only)` and the
device tile flips ON/OFF. This is the exact call path the `USE_WIFI` build makes
for real — the only difference is the HTTP GET actually goes out.

## 4. Replay the log

```
status
help
```

`status` shows `USE_WIFI=0 USE_DISPLAY=0` so the audience sees which paths are
live.

## 5. (Optional) Go live

Rebuild with `EXTRA_FLAGS="-DUSE_DISPLAY=1 -DUSE_WIFI=1"`, flash, and use the
`curl`/`/gpio` recipe in `mock-api/README.md` to show a real POST landing on the
dashboard and a tile tap logging on the fake device. Call it what it is:
compile-verified today, hardware-verified once it runs on your bench.
