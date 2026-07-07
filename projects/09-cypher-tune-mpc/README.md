# CrowPanel Cypher Tune MPC

Touch groovebox inspired by `cardputer-mpc`.

V1 includes a 4x4 pad surface, 16-step pattern state, BPM, play/stop, record,
and visual voices. Audio is mock/silent until `USE_AUDIO` is implemented for the
CrowPanel.

## Serial Commands

- `help` / `status` / `history`
- `pad <1-16>`
- `step <1-16>`
- `bpm <value>`
- `play`
- `stop`
- `record`
- `pattern`
