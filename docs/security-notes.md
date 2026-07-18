# Security Notes

BadgeOps is a teaching scaffold. It is useful for demos, attendance, event check-in, and internal prototypes. It is not a secure physical access-control system by default.

Required warning:

"UID-only RFID/NFC access is suitable for demos, attendance tracking, event check-in, prototypes, and low-risk internal tools. It should not be treated as secure access control. Many low-cost RFID/NFC cards and tags can be cloned. For serious access control, use stronger credential design, signed tokens, backend validation, secure elements, audit logging, and proper threat modeling."

## Practical Guidance

- Never present UID-only checks as strong authentication.
- Do not put secrets in Serial logs.
- Treat mock badges and sample data as fake data.
- Add backend validation before serious check-in workflows.
- Add audit logs before allowing any real-world access decision.

## Cypher Flock radio boundary

Project 16 is a passive field-visibility demo. The BW16 evaluates Wi-Fi frame
headers, the ESP32 evaluates BLE advertisements and aggregates both links, and
the P4 receives only derived detections, sanitized calibration metadata, and
aggregate diagnostics.

The optional onboard-C6 witness performs passive 2.4 GHz AP scans only. Its
SSID/BSSID metadata is ephemeral, never increases identity confidence, never
triggers an alert, and is not written into the Flock session store.

- Do not add network joins, deauthentication, injection, credential collection,
  or packet-payload forwarding.
- Treat observed MAC addresses and SSIDs as potentially sensitive data.
- The scope maps RSSI to approximate signal proximity only. It does not provide
  direction, distance, identity proof, or physical location.
- Addr1 and non-transmitting addr3 RSSI describe the captured transmitter, not
  the inferred peer, so those records must never appear on the proximity scope.
- Use the detector only where radio monitoring is lawful and authorized.
