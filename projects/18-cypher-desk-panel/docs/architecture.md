# Cypher Desk OS Architecture

`CypherDeskOs` is the sketch-level coordinator. It owns the bounded app router,
fixed-size event bus, system services, home screen, and serial QA. Twelve
applications implement `DeskApplication`; the mature Writer is preserved
behind `DeskWriterApplication` while the other eleven use the shared
tap-on-release shell.

`DeskStorageService` owns OS directory setup, capacity and low-space reporting,
safe eject, atomic structured writes, and boot recovery. Writer's existing
`DeskStorage` recognizes an already-mounted card and retains its path and file
format compatibility.

`DeskWifiService` is the OS owner of hosted-C6 scan/connect/state operations.
It persists at most five recent profiles in `cypher-desk`, masks credentials,
and runs the configurable 204 connectivity check. `DeskTimeService` consumes
the Wi-Fi state and requests NTP without disconnecting the radio.

Recorder and media UI are feature-gated. Compile availability is never treated
as microphone, speaker, or end-to-end playback proof.
