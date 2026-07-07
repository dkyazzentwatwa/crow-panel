# Vision Guard Demo Script

1. Introduce the kiosk concept.
2. Compile with Arduino CLI — point out the ESP32-P4 FQBN.
3. Open Serial Monitor (115200, line ending Newline).
4. Show live camera/status mock output.
5. Let the QR scanner simulate a product or visitor check-in.
6. Type `scan INSPECT-CUSTOM-1` — an on-cue check-in with your own label.
7. Show checklist pass/fail state (every 4th scan fails — keep scanning until you catch one on camera).
8. Type `history` — the inspection audit trail replays.
9. Explain the honest camera story: the P4 camera path is ESP-IDF-only for now, and the stub says so instead of pretending.

## What To Film

- Compile command
- Serial camera status
- `scan INSPECT-CUSTOM-1` on-cue check-in
- A failing checklist result
- `history` replay
- AI vision stub note
