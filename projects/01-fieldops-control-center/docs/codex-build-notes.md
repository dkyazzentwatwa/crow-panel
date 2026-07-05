# Codex Build Notes

Proof targets:

- `scaffolded`: all FieldOps files exist
- `compile-ready`: Arduino CLI compile succeeds with mock mode
- `uploaded`: upload succeeds to a detected CrowPanel serial port
- `field-proven`: mock or real dashboard output is observed on the physical display or Serial monitor

Do not claim radio behavior until a real SX1262 module is wired and verified.
