#ifndef LITEGO_TOUCH_COACH_PROJECT_CONFIG_H
#define LITEGO_TOUCH_COACH_PROJECT_CONFIG_H

#include <AppConfig.h>

// --- GT911 touch calibration -------------------------------------------------
// The CrowPanel Advanced 7-inch panel reports touch in screen coordinates
// already: no axis swap, no inversion, 0..1023 by 0..599. These defaults match
// the values projects 08, 18, and 21 use on the same hardware. Override them
// here (or with -D flags) if a board revision ever needs different ones, and
// confirm with the Serial `touchcal` command, which prints raw and mapped
// coordinates side by side.
#ifndef LITEGO_TOUCH_SWAP_XY
#define LITEGO_TOUCH_SWAP_XY 0
#endif

#ifndef LITEGO_TOUCH_INVERT_X
#define LITEGO_TOUCH_INVERT_X 0
#endif

#ifndef LITEGO_TOUCH_INVERT_Y
#define LITEGO_TOUCH_INVERT_Y 0
#endif

#ifndef LITEGO_TOUCH_MIN_X
#define LITEGO_TOUCH_MIN_X 0
#endif

#ifndef LITEGO_TOUCH_MAX_X
#define LITEGO_TOUCH_MAX_X 1023
#endif

#ifndef LITEGO_TOUCH_MIN_Y
#define LITEGO_TOUCH_MIN_Y 0
#endif

#ifndef LITEGO_TOUCH_MAX_Y
#define LITEGO_TOUCH_MAX_Y 599
#endif

// --- Game defaults -----------------------------------------------------------
// Komi is carried doubled so scoring stays integer: 13 is 6.5 points, the
// usual 9x9 value, and an odd numerator guarantees no draws.
#ifndef LITEGO_KOMI_X2
#define LITEGO_KOMI_X2 13
#endif

// 0 easy (instant one-ply heuristic), 1 normal, 2 hard (Monte-Carlo playouts).
#ifndef LITEGO_DEFAULT_LEVEL
#define LITEGO_DEFAULT_LEVEL 1
#endif

// 'B' means you play Black and move first.
#ifndef LITEGO_HUMAN_COLOR
#define LITEGO_HUMAN_COLOR 'B'
#endif

// Milliseconds of search per loop() pass. Small enough that touch stays
// responsive while the opponent is thinking, large enough to make progress.
#ifndef LITEGO_AI_SLICE_MS
#define LITEGO_AI_SLICE_MS 40
#endif

#endif
