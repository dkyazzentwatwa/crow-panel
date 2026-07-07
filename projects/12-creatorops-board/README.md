# CrowPanel CreatorOps Board

Content operations board inspired by `techtiff-brain`.

V1 uses a project-local data model for ideas, drafts, filming, scheduled,
published, tasks, and channel status. The default source is a static mock
snapshot compiled into the sketch. It is a dashboard, not a publisher.

## Data Sources

Default mode is static/local:

- `USE_CREATOROPS_API=0`: compiled mock snapshot in `src/CreatorOpsDataSource.cpp`.
- No Wi-Fi, credentials, posting, scheduling, deleting, or account mutation.

Optional API-cache mode is read-only and still local to the build:

- `USE_CREATOROPS_API=1`: switches to the compiled API-cache snapshot.
- Override cache values with compiler defines or a local `config/CreatorOpsCache.h`.
- Cache files must contain only exported counts/labels, never tokens, cookies, or account credentials.

`config/CreatorOpsCache.h` format:

```cpp
#define CREATOROPS_API_CACHE_LABEL "2026-07-06 local export"
#define CREATOROPS_API_CACHE_DETAIL "Exported from local content index"
#define CREATOROPS_API_CACHE_IDEAS 18
#define CREATOROPS_API_CACHE_DRAFTS 7
#define CREATOROPS_API_CACHE_FILMING 3
#define CREATOROPS_API_CACHE_SCHEDULED 5
#define CREATOROPS_API_CACHE_PUBLISHED 888
#define CREATOROPS_API_CACHE_TASKS 12
#define CREATOROPS_API_CACHE_CHANNELS 4
```

## Flags

- `USE_DISPLAY=0`: Serial-first dashboard, default compile path.
- `USE_DISPLAY=1`: mirrors the dashboard to the CrowPanel display path.
- `USE_CREATOROPS_API=0`: static mock source, default.
- `USE_CREATOROPS_API=1`: read-only API-cache source. This does not enable network calls.

## Serial Commands

- `help` / `status` / `history`
- `pipeline`
- `ideas`
- `drafts`
- `filming`
- `scheduled`
- `published`
- `tasks`
- `channels`
- `source`
- `refresh`

Smoke sequence after upload:

```text
help
status
source
pipeline
ideas
drafts
filming
scheduled
published
tasks
channels
refresh
history
```

## Proof States

- `compile-ready local mock`: default mode compiled successfully.
- `compile-ready api-cache`: `USE_CREATOROPS_API=1` compiled successfully.
- `uploaded`: sketch was flashed to a connected CrowPanel.
- `field-proven`: Serial/display behavior was observed on the real board.

No posting, scheduling, sending, or external account mutation happens in v1.
