#pragma once

#ifdef RETRO_GO
#include <rg_system.h>
#define LOG_PRINTF(level, x...) rg_system_log(RG_LOG_PRINTF, NULL, x)
#define CRC32(a, b, c) rg_crc32(a, b, c)
#else
#include <stdio.h>
#define LOG_PRINTF(level, x...) printf(x)
// CrowPanel patch: guard it - esp_attr.h defines IRAM_ATTR too, and any C++
// TU including both Arduino.h and nes.h would hit a macro redefinition.
#ifndef IRAM_ATTR
#define IRAM_ATTR
#endif
// CrowPanel patch: upstream stubs this to 0 outside RETRO_GO, which makes
// rom.checksum 0 so database.h (4942 entries) can NEVER match - we would pay
// ~49 KB of rodata for a table searched against a constant. esp_rom_crc32_le
// is the same reflected-0xEDB88320 CRC retro-go uses via rg_crc32.
#include "esp_rom_crc.h"
#define CRC32(a, b, c) esp_rom_crc32_le((a), (b), (c))
#endif

#define MESSAGE_ERROR(x...) LOG_PRINTF(1, "!! " x)
#define MESSAGE_WARN(x...)  LOG_PRINTF(2, " ! " x)
#define MESSAGE_INFO(x...)  LOG_PRINTF(3, x)
#ifdef NOFRENDO_DEBUG
#define MESSAGE_DEBUG(x, ...) LOG_PRINTF(4, "> %s: " x, __func__, ## __VA_ARGS__)
#else
#define MESSAGE_DEBUG(x...)
#endif
#define MESSAGE_TRACE(x, ...) LOG_PRINTF(4, "~ %s: " x, __func__, ## __VA_ARGS__)

#undef MIN
#define MIN(a, b) ({__typeof__(a) _a = (a); __typeof__(b) _b = (b);_a < _b ? _a : _b; })
#undef MAX
#define MAX(a, b) ({__typeof__(a) _a = (a); __typeof__(b) _b = (b);_a > _b ? _a : _b; })

#define ASSERT(expr) while (!(expr)) { LOG_PRINTF(1, "ASSERTION FAILED IN %s: " #expr "\n", __func__); abort(); }
#define UNUSED(x) (void)x
