#ifndef CYPHER_STICK_PROJECT_CONFIG_H
#define CYPHER_STICK_PROJECT_CONFIG_H

// Cypher Stick — touch fightstick. Flags default off (mock-first); real builds
// pass -D flags so the shared library sees them too (see CLAUDE.md).

// Max keys in one profile. Sizes fixed arrays — do not raise casually.
#ifndef STICK_MAX_KEYS
#define STICK_MAX_KEYS 20
#endif

// Profiles held in RAM / stored on SD.
#ifndef STICK_MAX_PROFILES
#define STICK_MAX_PROFILES 8
#endif

// A lift is committed after this many consecutive empty polls. 1 keeps latency
// at one poll; raise ONLY if hardware shows the GT911 dropping frames mid-hold.
// Project 21 uses a 30 ms wall-clock timer here; that is ~2 frames and is
// exactly what this project exists to avoid.
#ifndef STICK_LIFT_CONFIRM_POLLS
#define STICK_LIFT_CONFIRM_POLLS 1
#endif

// Stick task cadence on core 1. The GT911 reports at 100 Hz, so polling much
// faster than this only burns I2C bandwidth.
#ifndef STICK_POLL_MS
#define STICK_POLL_MS 2
#endif

#include <AppConfig.h>

#endif
