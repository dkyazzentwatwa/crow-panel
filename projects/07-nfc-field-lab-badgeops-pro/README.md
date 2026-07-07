# CrowPanel NFC Field Lab / BadgeOps Pro

Combined CrowPanel port inspired by `cypher-pn532` and `cypherbox-mini`.

V1 is PN532-first in concept but mock-first in code. It preserves the important
boundaries: UID-only is not secure access control, APDU lab is limited to public
Type 4 NDEF, and payment/proprietary applets are out of scope.

## Serial Commands

- `help` / `status` / `history`
- `scan`
- `tap [uid]`
- `ndef`
- `apdu`
- `files`
- `badges`

## Required Warning

UID-only RFID/NFC access is suitable for demos, attendance tracking, event
check-in, prototypes, and low-risk internal tools. It should not be treated as
secure access control.
