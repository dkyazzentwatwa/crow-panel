// CrowPanel gate: arduino-cli compiles every .c under src/ regardless of
// feature flags, so the whole file is gated. Undefined evaluates to 0 in #if,
// so a build without -DUSE_NES_CORE=1 pays nothing for this core.
#if USE_NES_CORE

/**
 * This is a placeholder. The implementation is currently in map004.c
 */


#endif  // USE_NES_CORE
