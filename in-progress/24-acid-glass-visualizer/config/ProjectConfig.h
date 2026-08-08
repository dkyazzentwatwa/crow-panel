#ifndef ACID_GLASS_PROJECT_CONFIG_H
#define ACID_GLASS_PROJECT_CONFIG_H

// Acid Glass is mock-first. Real display, SD/audio and hosted-C6 builds pass
// these flags through compiler.cpp.extra_flags so shared translation units see
// the same values (see CLAUDE.md's three-layer flag rule).
#ifndef USE_ACID_GLASS_SD
#define USE_ACID_GLASS_SD 0
#endif

#ifndef USE_ACID_GLASS_AUDIO
#define USE_ACID_GLASS_AUDIO 0
#endif

#ifndef USE_ACID_GLASS_REMOTE
#define USE_ACID_GLASS_REMOTE 0
#endif

// Hosted-C6 startup can block on a panel whose companion radio is not powered
// or wired. Keep the visual instrument boot-safe; enable the AP explicitly once
// the C6 path has been verified on the target panel.
#ifndef ACID_GLASS_REMOTE_AUTOSTART
#define ACID_GLASS_REMOTE_AUTOSTART 0
#endif

// PPA scale/blit is a performance path, not a boot requirement. Start on the
// known-safe CPU scaler until this panel's PPA transaction path is observed.
#ifndef ACID_GLASS_USE_PPA
#define ACID_GLASS_USE_PPA 0
#endif

// First device bring-up uses a visual-only loop. GT911 polling is restored only
// after it is proven not to block the first rendered frame on this panel.
#ifndef ACID_GLASS_TOUCH_ENABLED
#define ACID_GLASS_TOUCH_ENABLED 0
#endif

// Staged hardware recovery: ignore persisted state and run only the display
// proof and CPU visual path. The flash command opts into this explicitly.
#ifndef ACID_GLASS_BRINGUP_VISUAL_ONLY
#define ACID_GLASS_BRINGUP_VISUAL_ONLY 0
#endif

#ifndef ACID_GLASS_SDMMC_1BIT
#define ACID_GLASS_SDMMC_1BIT 1
#endif

#ifndef ACID_GLASS_MUSIC_DIR
#define ACID_GLASS_MUSIC_DIR "/acid-glass/music"
#endif

#ifndef ACID_GLASS_MAX_TRACKS
#define ACID_GLASS_MAX_TRACKS 64
#endif

#ifndef ACID_GLASS_AP_PREFIX
#define ACID_GLASS_AP_PREFIX "AcidGlass"
#endif

#ifndef ACID_GLASS_HTTP_PORT
#define ACID_GLASS_HTTP_PORT 80
#endif

#ifndef ACID_GLASS_RENDER_FPS
#define ACID_GLASS_RENDER_FPS 45
#endif

#include <AppConfig.h>

#endif
