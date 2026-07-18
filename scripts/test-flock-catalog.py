#!/usr/bin/env python3
"""Small deterministic regression checks for catalog policy and name grammars."""

import json
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
data = json.loads((ROOT / "signatures/cypher-flock/catalog.json").read_text())
fixtures = json.loads((ROOT / "signatures/cypher-flock/fixtures.json").read_text())

enabled = {x["value"]: x for x in data["wifi_ouis"] if x.get("enabled", True)}
rejected = {x["value"] for x in data["rejected"] if x["type"] == "wifi_oui"}

assert enabled["b4:1e:52"]["standalone"] is True
assert enabled["82:6b:f2"]["standalone"] is False
assert not (set(enabled) & rejected)
assert {"f8:a2:d6", "94:2a:6f", "f4:e2:c6", "d4:11:d6"} <= rejected
rules = {x["id"]: x for x in data["rules"]}
assert rules["ble.company.xuntong"]["evidence"] == "candidate"
assert rules["wifi.wildcard_multichannel"]["evidence"] == "high"
assert rules["ble.raven.custom"]["rejection_reason"].startswith("standard 180A")

flock = re.compile(r"^Flock-([0-9A-F]{6})$")
penguin = re.compile(r"^Penguin-([0-9]{10})$")

assert flock.fullmatch("Flock-7F68FF")
assert not flock.fullmatch("FLOCK-21DD56")
assert not flock.fullmatch("Flock-21dd56")
assert not flock.fullmatch("my-flock-camera")
assert not flock.fullmatch("Flock-12345")
assert penguin.fullmatch("Penguin-3101300881")
assert not penguin.fullmatch("Penguin-31013")

ssid = flock.fullmatch("Flock-7F68FF")
mac = "70:c9:4e:7f:68:ff".replace(":", "")
assert ssid and mac[-6:].lower() == ssid.group(1).lower()

fixture_by_id = {item["id"]: item for item in fixtures}
assert fixture_by_id["wifi-owned-5g"]["channel"] == 36
assert fixture_by_id["wifi-wildcard-two-channel"]["window_ms"] <= 10000
assert fixture_by_id["wifi-indirect-peer"]["direct_rssi"] is False
assert fixture_by_id["ble-raven-single"]["expected"] == "medium"
assert fixture_by_id["ble-raven-multiple"]["expected"] == "high"
assert fixture_by_id["ble-context-only"]["expected"] == "reject"
assert fixture_by_id["ble-xuntong-only"]["expected"] == "candidate"
for fixture in fixtures:
    if fixture.get("expected") == "reject" and "mac" in fixture:
        assert fixture["mac"][:8] in rejected or fixture["id"] == "ble-context-only"

print(
    f"flock catalog tests PASS version={data['version']} "
    f"enabled_ouis={len(enabled)} rejected={len(rejected)} fixtures={len(fixtures)}"
)
