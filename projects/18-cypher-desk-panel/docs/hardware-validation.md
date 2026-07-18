# Cypher Desk OS Hardware Validation

Use the repo upload helper and record each result independently. A successful
compile or upload does not promote a runtime behavior.

## Boot and UI

- [ ] OS home renders as a 3x4 grid at 1024x600.
- [ ] Each tile opens the matching app and OS Home returns to the grid.
- [ ] OS-native buttons activate on release, not initial contact.
- [ ] Shared keyboard letters, shift, symbols, backspace, space, and return work.
- [ ] Wi-Fi password text remains masked and does not appear in Serial output.
- [ ] Writer opens existing notes and its OS Home button returns safely.

## Storage and recovery

- [ ] Boot without SD reports `NotPresent` without crashing.
- [ ] Inserted SD mounts and creates only the documented desk directories.
- [ ] Writer save, reboot, reopen, export, and Cardputer/macOS readability pass.
- [ ] Calendar and Contacts survive reboot with their schema headers intact.
- [ ] Safe eject stops writes and remount restores access.
- [ ] Idle removal reports `RemovedUnexpectedly`.
- [ ] Removal during a structured write retains a recoverable primary or backup.
- [ ] Full/low-space media blocks new structured writes without deleting data.

## Hosted-C6 Wi-Fi and time

- [ ] Asynchronous scan returns visible networks.
- [ ] Open, secured, saved, and hidden-network flows connect as expected.
- [ ] Wrong password, missing SSID, timeout, disconnect, and reconnect are clear.
- [ ] HTTP 204 reports verified internet; redirects/content warn about a portal.
- [ ] Offline mode does not start new network work.
- [ ] NTP updates the status time without the time service owning Wi-Fi.

## Audio and media

- [ ] Speaker initialization and generated key sound pass before `audio-proven`.
- [ ] Supported 16-bit 16 kHz mono WAV plays; malformed WAV fails safely.
- [ ] Recorder remains unavailable until microphone pins and RX format are known.
- [ ] Microphone meter, short record, reboot, persisted WAV, and playback pass
      before `microphone-proven`.

## First acceptance path

Boot, mount SD, open the app grid, open Writer, save a note, configure Wi-Fi,
reboot, reopen the same note, and confirm the status line reflects real Wi-Fi,
SD, and time states.
