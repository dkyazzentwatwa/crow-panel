#ifndef CREATOROPS_PROJECT_CONFIG_H
#define CREATOROPS_PROJECT_CONFIG_H

// Per-project overrides go here, BEFORE AppConfig.h fills default flags.
//
// USE_CREATOROPS_API switches the dashboard from the compiled static mock
// snapshot to a compiled read-only API-cache snapshot. It does not enable
// network access and does not add publish/schedule/delete behavior.
#ifndef USE_CREATOROPS_API
#define USE_CREATOROPS_API 0
#endif

// Optional local cache override. This file should contain only exported,
// non-secret counts/labels and should never contain account credentials.
#if __has_include("CreatorOpsCache.h")
#include "CreatorOpsCache.h"
#endif

#ifndef CREATOROPS_API_CACHE_LABEL
#define CREATOROPS_API_CACHE_LABEL "local exported API cache"
#endif

#ifndef CREATOROPS_API_CACHE_DETAIL
#define CREATOROPS_API_CACHE_DETAIL "Compile-time CreatorOps cache; read-only on device"
#endif

#ifndef CREATOROPS_API_CACHE_IDEAS
#define CREATOROPS_API_CACHE_IDEAS 18
#endif

#ifndef CREATOROPS_API_CACHE_DRAFTS
#define CREATOROPS_API_CACHE_DRAFTS 7
#endif

#ifndef CREATOROPS_API_CACHE_FILMING
#define CREATOROPS_API_CACHE_FILMING 3
#endif

#ifndef CREATOROPS_API_CACHE_SCHEDULED
#define CREATOROPS_API_CACHE_SCHEDULED 5
#endif

#ifndef CREATOROPS_API_CACHE_PUBLISHED
#define CREATOROPS_API_CACHE_PUBLISHED 888
#endif

#ifndef CREATOROPS_API_CACHE_TASKS
#define CREATOROPS_API_CACHE_TASKS 12
#endif

#ifndef CREATOROPS_API_CACHE_CHANNELS
#define CREATOROPS_API_CACHE_CHANNELS 4
#endif

#include <AppConfig.h>

#endif
