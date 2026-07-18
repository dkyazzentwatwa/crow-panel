# Cypher Desk OS Device Acceptance

Use Arduino CLI only. This checklist separates a successful build from proof
on the physical CrowPanel.

## USB and boot-log recovery

1. Close any serial monitor, then run `arduino-cli board list` and record the
   detected `/dev/cu.usbmodem*` port.
2. Compile the full OS before upload using the project FQBN and 460800 baud.
3. Upload to the newly detected port. Treat `Hash of data verified` as
   **uploaded**, not runtime proof.
4. Re-run `arduino-cli board list` after the reset. The `USBMode=hwcdc` runtime
   port can disappear, so inspect the screen first and only claim boot-log
   proof when a newly enumerated monitor produces actual output.

## Required acceptance path

1. Boot with SD inserted. Confirm the status bar says Mounted and open Today.
2. Create a Daily Page, a Calendar event, and a Contact. Reboot and reopen all
   three records from SD.
3. Open Files. Create a folder, preview a text file, copy or move a non-system
   file, cancel one delete, then confirm one delete. Safely eject and remount.
4. Test touch release behavior, home/back history, empty states, status-bar
   clipping, and each home tile.
5. Connect hosted-C6 Wi-Fi, test visible and hidden onboarding, a wrong
   password, reconnect, forget, captive-portal warning, NTP sync, and a saved
   POSIX timezone using `time zone <value>`.
6. Run `audio speaker`, then `audio mic`. If both paths initialize, record a
   short named WAV, reboot, play it from Recorder or Music, and verify the SD
   file remains present. If either path fails, retain the displayed unavailable
   state and report it as unproven.

## Proof labels

- `compile-ready`: the matching Arduino CLI matrix row succeeds.
- `uploaded`: the board reports a verified upload.
- `device-proven`: the checklist action was observed on the actual panel.

Do not upgrade a microphone, speaker, SD, Wi-Fi, or boot-log claim from
compile-ready merely because another subsystem is working.
