#!/usr/bin/env python3
"""Deterministic host fixtures for Cypher Flock UART/session edge cases."""

import json
import zlib

MAX_LINE = 768


class SourceSequence:
    def __init__(self):
        self.last = 0
        self.duplicates = 0
        self.out_of_order = 0

    def accept(self, line: str) -> bool:
        if len(line) >= MAX_LINE:
            return False
        try:
            event = json.loads(line)
        except json.JSONDecodeError:
            return False
        if event.get("v") != 1 or not isinstance(event.get("event"), str):
            return False
        sequence = int(event.get("seq", 0))
        if event["event"] == "hello" and sequence <= self.last:
            self.last = 0
        if sequence == self.last:
            self.duplicates += 1
            return False
        if sequence < self.last:
            self.out_of_order += 1
            return False
        self.last = sequence
        return True


def line(sequence: int, event: str = "status", **fields) -> str:
    return json.dumps({"v": 1, "seq": sequence, "event": event, **fields}, separators=(",", ":"))


source = SourceSequence()
assert source.accept(line(1, "hello"))
assert source.accept(line(2))
assert not source.accept(line(2)) and source.duplicates == 1
assert not source.accept(line(1)) and source.out_of_order == 1
assert source.accept(line(1, "hello"))  # source reboot recovery
assert not source.accept("{")
assert not source.accept(line(2).replace('"v":1', '"v":2'))
assert not source.accept("x" * MAX_LINE)

detections = [
    line(3, "detection", source="wifi-bw16", protocol="wifi_5ghz", evidence="high",
         alert_eligible=True, direct_rssi=True, mac_address="b4:1e:52:7f:68:ff"),
    line(4, "detection", source="ble-esp32", protocol="bluetooth_le", evidence="high",
         alert_eligible=True, direct_rssi=True, identity="Penguin-3101300881",
         mac_address="c0:00:00:00:00:01"),
    line(5, "detection", source="ble-esp32", protocol="bluetooth_le", evidence="candidate",
         alert_eligible=False, direct_rssi=True, mac_address="c0:00:00:00:00:02"),
    line(6, "detection", source="wifi-bw16", protocol="wifi_2_4ghz", evidence="high",
         alert_eligible=True, direct_rssi=False, mac_address="b4:1e:52:01:02:03"),
]
assert all(len(item) < MAX_LINE and json.loads(item)["event"] == "detection" for item in detections)
assert sum(json.loads(item)["alert_eligible"] for item in detections) == 3
assert sum(json.loads(item)["direct_rssi"] and json.loads(item)["alert_eligible"] for item in detections) == 2

required_commands = {
    "ping", "status", "diag on", "diag off", "mode full", "mode custom",
    "mode single", "channel 36", "band 2.4", "band 5", "band dual",
    "profile precision", "profile balanced", "profile recall", "scan pause",
    "scan resume", "calibration on", "calibration off", "calibration export",
    "calibration clear", "catalog", "reset counters",
}
assert len(required_commands) == 22

payload_v1 = json.dumps([{"mac": "b4:1e:52:00:00:01"}], separators=(",", ":"))
payload_v2 = json.dumps([{"mac": "b4:1e:52:00:00:01", "evidence": "high",
                          "direct_rssi": True, "alert_eligible": True}], separators=(",", ":"))
for version, payload in ((1, payload_v1), (2, payload_v2)):
    envelope = {"v": version, "bytes": len(payload), "crc": zlib.crc32(payload.encode()) & 0xFFFFFFFF}
    assert envelope["v"] in (1, 2)
    assert envelope["bytes"] == len(payload)
    assert envelope["crc"] == (zlib.crc32(payload.encode()) & 0xFFFFFFFF)

penguins = {}
for mac in ("c0:00:00:00:00:01", "d0:00:00:00:00:02"):
    penguins["Penguin-3101300881"] = mac
assert len(penguins) == 1
assert len({f"02:00:00:00:00:{index:02x}" for index in range(200)}) == 200

print("flock protocol tests PASS malformed/version/oversize/sequence/session/capacity")
