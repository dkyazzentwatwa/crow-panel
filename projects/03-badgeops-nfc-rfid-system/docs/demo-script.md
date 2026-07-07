# BadgeOps Demo Script

1. Introduce the kiosk concept.
2. Compile with Arduino CLI — point out the ESP32-P4 FQBN.
3. Open Serial Monitor (115200, line ending Newline).
4. Type `badges` — show the demo registry (active, guest, suspended).
5. Show a mock badge tap arriving on the timer.
6. Type `tap 04:A1:22:9C` — access granted, on cue.
7. Type `tap C2:44:10:AA` — suspended contractor denied.
8. Type `tap 11:22:33:44` — unknown badge denied.
9. Type `history` — the access audit trail replays oldest-first.
10. Read the UID-only warning and explain the threat model boundary.

## What To Film

- Compile command
- `badges` registry listing
- The granted / suspended / unknown `tap` beats
- `history` audit replay
- `pn532-vs-mfrc522.md`
- Security warning
