# Hardware Revision Profiles

All board-revision-specific pins live in `shared/CrowPanelShared/HardwareProfile.*`.

Available profile macros:

- `CROWPANEL_P4_7IN_V1_0`
- `CROWPANEL_P4_7IN_V1_1`
- `CROWPANEL_P4_7IN_V1_2`

Default:

```cpp
#define CROWPANEL_HARDWARE_PROFILE CROWPANEL_P4_7IN_V1_2
```

## Wireless Pin Warning

The official README says V1.2 changed the wireless module socket pin allocation:

- Original IO53 and IO54 adjusted to IO27 and IO28.
- IO27 and IO28 adjusted to IO53 and IO54.

This repo models V1.2 by routing the official IO53/IO54-style wireless placeholders to IO27/IO28. That is a documented scaffold choice, not field proof.

## Verify Before Driver Work

Before enabling `USE_LORA_DRIVER`, `USE_PN532_DRIVER`, `USE_MFRC522_DRIVER`, or any other module:

1. Confirm the board revision.
2. Compare the physical module socket with Elecrow's matching example.
3. Update `HardwareProfile` or project `Pins.example.h`.
4. Compile with the verified FQBN.
5. Upload and verify runtime behavior on the real board.
