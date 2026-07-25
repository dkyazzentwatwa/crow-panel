# FieldOps Demo Script

1. Show the repo is Arduino CLI only.
2. Run `./scripts/compile-all.sh` — point out the ESP32-P4 FQBN and the "compile-verified vs hardware-verified" banner.
3. Explain mock mode and why it keeps demos moving before driver work.
4. Open Serial Monitor (115200, line ending Newline).
5. Narrate each mock packet as a field sensor check-in.
6. Type `help`, then `status` — flags, heap, and hardware profile on demand.
7. Type `inject 1 40 12` — a LOW_BATTERY and TEMP_WARNING alert fires live, on your cue instead of the timer's.
8. Type `history` — the event ring buffer replays the session's alerts.
9. When an alert appears, switch to the alert engine code.
10. End on `HardwareProfile.cpp` and explain why V1.2 radio pins are revision-aware.

## What To Film

- Compile command
- Serial dashboard output
- `inject 1 40 12` firing alerts on cue
- `history` replay
- AI summary output
- Hardware profile warning
