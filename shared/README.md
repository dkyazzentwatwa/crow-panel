# Shared CrowPanel Library

`shared/CrowPanelShared` is an Arduino library consumed by all three sketches through:

```sh
arduino-cli compile --libraries "$ROOT/shared" ...
```

It contains:

- Common config macros
- Hardware revision profiles
- Serial logging helpers
- Mock data helpers
- Simple event, storage, network, and UI theme utilities

Keep board-revision assumptions here instead of scattering pins across project code.
