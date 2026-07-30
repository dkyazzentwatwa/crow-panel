# Pokedex Panel Expo Pass Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Turn `projects/15-pokedex-panel` from a serial-driven developer demo into an expo-ready Pokedex: real sprites from SD, a sprite-grid browse screen with jump rail and filters, named detail tabs, and audio.

**Architecture:** A pure-C++ `PokedexIndex` builds a 40-byte-per-row index of `index.csv` into PSRAM once at boot, replacing a linear SD rescan that ran on every page turn. A pure-C++ BMP decoder feeds `PokedexSprites`, which caches upscaled RGB565 tiles in PSRAM. `PokedexDashboard` gains a grid browse mode; `PokedexAudio` mirrors the hardware-verified I2S path from project 20. The index and decoder are free of `Arduino.h`/`String`/`SD_MMC` so the shipping translation units also compile under `g++` for host tests.

**Tech Stack:** Arduino-CLI + `esp32:esp32@3.3.8`, ESP32-P4, Arduino_GFX (no LVGL), `SD_MMC`, ESP-IDF `driver/i2s_std.h`, `g++ -std=c++17` for host tests.

**Spec:** `docs/superpowers/specs/2026-07-29-pokedex-expo-pass-design.md`

---

## Critical repo constraints

Read these before writing any code. Violating them produces builds that are
green and broken.

1. **`CTAGS_WORKAROUND=1` is mandatory on this machine.** It disables Arduino
   prototype generation, so **every function must be defined before use** in
   `.ino` files. Preserve existing ordering.
2. **Never wrap a feature-flagged library include in `__has_include`.** It
   silently disables the feature and still builds green. Include `<SD_MMC.h>`
   directly under the flag, as `PokedexCatalog.cpp:9` already does.
3. **`compiler.cpp.extra_flags` does not reach `.c` files.** Not an issue here —
   this project has no flag-gated C.
4. **New flags need three things:** an `AppConfig.h` default, a `ProjectConfig.h`
   override if the project wants it on, and a row in `check-flag-matrix.sh`.
   A combination is only "supported" once it has a green row there.
5. **Read pins from `HardwareProfile`. Never hardcode a GPIO.** Audio amp enable
   IO30 is **ACTIVE-LOW** — write `digitalWrite(a.control, a.controlActiveHigh ? HIGH : LOW)`.
6. **Honesty contract.** Everything here lands as `compile-ready`. Do not touch
   `docs/full-port-proof-matrix.md` beyond that without observed hardware
   evidence.
7. **Mock-first.** The project must still boot and demo with every flag off.
   `PokedexCatalog`'s mock path and public API stay intact.

## File structure

| Path | Status | Responsibility |
|---|---|---|
| `projects/15-pokedex-panel/src/PokedexIndex.h` | create | Pure C++: record layout, variant/type tables, `ByteSource`, `Index` queries |
| `projects/15-pokedex-panel/src/PokedexIndex.cpp` | create | Pure C++: CSV parse, name-order sort, paging, jumps |
| `projects/15-pokedex-panel/src/PokedexBmp.h` | create | Pure C++: BMP header validation + decode/upscale API |
| `projects/15-pokedex-panel/src/PokedexBmp.cpp` | create | Pure C++: 24bpp bottom-up → RGB565 top-down, integer upscale |
| `projects/15-pokedex-panel/src/PokedexSdSource.h` | create | Arduino: `ByteSource` over `SD_MMC` `File` |
| `projects/15-pokedex-panel/src/PokedexSprites.h/.cpp` | create | Arduino: sprite load + PSRAM LRU cache |
| `projects/15-pokedex-panel/src/PokedexAudio.h/.cpp` | create | Arduino: I2S BGM loop + SFX tones |
| `projects/15-pokedex-panel/src/PokedexCatalog.h/.cpp` | modify | Delegate to `Index` when SD is live; keep mock fallback + API |
| `projects/15-pokedex-panel/src/PokedexDashboard.h/.cpp` | modify | Grid browse, jump rail, new footer, detail tabs |
| `projects/15-pokedex-panel/src/PokedexTypes.h` | modify | Re-export pure enums; add grid UI event types |
| `projects/15-pokedex-panel/test/host_main.cpp` | create | Host harness (never compiled into firmware) |
| `projects/15-pokedex-panel/15-pokedex-panel.ino` | modify | New serial commands, wire grid state |
| `projects/15-pokedex-panel/config/ProjectConfig.h` | modify | Turn new flags on for this project |
| `shared/CrowPanelShared/AppConfig.h` | modify | `USE_POKEDEX_SPRITES`, `USE_POKEDEX_AUDIO` defaults |
| `scripts/test-pokedex.sh` | create | g++ host test runner |
| `scripts/check-flag-matrix.sh` | modify | 3 new rows |

`arduino-cli` compiles only the sketch root and `src/`, so `test/` never reaches
the firmware. This mirrors `projects/10-litego-touch-coach`.

---

## Stage 1 — PokedexIndex

### Task 1: Host test harness skeleton

**Files:**
- Create: `projects/15-pokedex-panel/src/PokedexIndex.h`
- Create: `projects/15-pokedex-panel/src/PokedexIndex.cpp`
- Create: `projects/15-pokedex-panel/test/host_main.cpp`
- Create: `scripts/test-pokedex.sh`

- [ ] **Step 1: Write the failing test**

Create `projects/15-pokedex-panel/test/host_main.cpp`:

```cpp
// Host-side test harness for the pure Pokedex index and BMP decoder.
//
// Lives outside src/ on purpose: arduino-cli only compiles the sketch root and
// src/, so nothing in this folder ever reaches the firmware. Build and run it
// with scripts/test-pokedex.sh.
#include <stdio.h>
#include <string.h>

#include "../src/PokedexIndex.h"

using namespace pokedex;

namespace {

uint16_t gFailures = 0;

void expect(const char *name, bool ok) {
  printf("[host] %-52s %s\n", name, ok ? "PASS" : "FAIL");
  if (!ok) {
    gFailures++;
  }
}

}  // namespace

int main() {
  expect("Record is exactly 40 bytes", sizeof(Record) == 40);
  printf("\n%u failure(s)\n", gFailures);
  return gFailures == 0 ? 0 : 1;
}
```

Create `projects/15-pokedex-panel/src/PokedexIndex.h`:

```cpp
#ifndef POKEDEX_INDEX_H
#define POKEDEX_INDEX_H

// Pure C++ catalog index for the Pokedex panel. Deliberately free of Arduino.h,
// String, and SD_MMC so the identical translation unit compiles for the
// ESP32-P4 firmware and for the host harness in ../test (scripts/test-pokedex.sh).
#include <stdint.h>

namespace pokedex {

constexpr uint8_t kTypeCount = 18;
constexpr uint8_t kTypeAny = 0xFF;
constexpr uint8_t kGridPageSize = 18;
constexpr uint8_t kNameLength = 32;

struct Record {
  uint32_t offset;
  uint16_t dex;
  uint8_t flags;
  uint8_t type1;
  char name[kNameLength];
};

}  // namespace pokedex

#endif
```

Create `projects/15-pokedex-panel/src/PokedexIndex.cpp`:

```cpp
#include "PokedexIndex.h"

namespace pokedex {
}  // namespace pokedex
```

Create `scripts/test-pokedex.sh`:

```bash
#!/usr/bin/env bash
set -euo pipefail

# Host-side tests for the Pokedex index and BMP decoder.
#
# PokedexIndex and PokedexBmp are deliberately free of Arduino.h, String, and
# SD_MMC, so the exact translation units that ship in the firmware also build
# with a plain g++ here. That makes CSV paging and sprite decode a one-second
# loop instead of a flash-and-squint cycle.

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROJECT="$ROOT/projects/15-pokedex-panel"
OUT="${BUILD_ROOT:-$ROOT/_arduino-build}/pokedex-host"

CXX="${CXX:-g++}"
if ! command -v "$CXX" >/dev/null 2>&1; then
  echo "$CXX is required to run the Pokedex host tests." >&2
  exit 1
fi

mkdir -p "$OUT"

SOURCES=("$PROJECT/src/PokedexIndex.cpp")
if [ -f "$PROJECT/src/PokedexBmp.cpp" ]; then
  SOURCES+=("$PROJECT/src/PokedexBmp.cpp")
fi

echo "Building Pokedex host tests with $CXX"
"$CXX" -std=c++17 -O2 -Wall -Wextra -Werror \
  -DPOKEDEX_HOST_BUILD=1 \
  "${SOURCES[@]}" \
  "$PROJECT/test/host_main.cpp" \
  -o "$OUT/pokedex-tests"

echo
"$OUT/pokedex-tests" "$@"
```

- [ ] **Step 2: Run test to verify it fails**

```bash
chmod +x scripts/test-pokedex.sh && ./scripts/test-pokedex.sh
```

Expected: PASS on the one assertion. If `sizeof(Record)` is not 40, the struct
has padding — fix field order before continuing. This step establishes the
harness; the assertion is a real invariant guard, not a placeholder.

- [ ] **Step 3: Commit**

```bash
git add projects/15-pokedex-panel/src/PokedexIndex.h projects/15-pokedex-panel/src/PokedexIndex.cpp projects/15-pokedex-panel/test/host_main.cpp scripts/test-pokedex.sh
git commit -m "test: add Pokedex host harness and index record layout"
```

---

### Task 2: Type name table

**Files:**
- Modify: `projects/15-pokedex-panel/src/PokedexIndex.h`
- Modify: `projects/15-pokedex-panel/src/PokedexIndex.cpp`
- Modify: `projects/15-pokedex-panel/test/host_main.cpp`

- [ ] **Step 1: Write the failing test**

Add to `host_main.cpp` inside `main()`, before the summary `printf`:

```cpp
  expect("typeIdFromName maps Bug to 0", typeIdFromName("Bug") == 0);
  expect("typeIdFromName maps Water to 17", typeIdFromName("Water") == 17);
  expect("typeIdFromName maps Grass to 9", typeIdFromName("Grass") == 9);
  expect("typeIdFromName rejects unknown", typeIdFromName("Cosmic") == kTypeAny);
  expect("typeIdFromName rejects empty", typeIdFromName("") == kTypeAny);
  expect("typeNameFromId round-trips Grass",
         strcmp(typeNameFromId(typeIdFromName("Grass")), "Grass") == 0);
  expect("typeNameFromId guards out of range", typeNameFromId(99) == nullptr);
```

- [ ] **Step 2: Run test to verify it fails**

```bash
./scripts/test-pokedex.sh
```

Expected: FAIL to compile with `'typeIdFromName' was not declared in this scope`.

- [ ] **Step 3: Write minimal implementation**

Add to `PokedexIndex.h` inside `namespace pokedex`, after the `Record` struct:

```cpp
// Maps an index.csv type name ("Grass") to 0..17, or kTypeAny when unknown.
uint8_t typeIdFromName(const char *name);

// Reverse of typeIdFromName. Returns nullptr when id is out of range.
const char *typeNameFromId(uint8_t id);
```

Replace the body of `PokedexIndex.cpp`:

```cpp
#include "PokedexIndex.h"

namespace pokedex {
namespace {

// The 18 type names that appear in index.csv, in the order the file uses them
// alphabetically. Index into this table is what Record::type1 stores.
const char *const kTypeNames[kTypeCount] = {
    "Bug",    "Dark",   "Dragon", "Electric", "Fairy",  "Fighting",
    "Fire",   "Flying", "Ghost",  "Grass",    "Ground", "Ice",
    "Normal", "Poison", "Psychic", "Rock",    "Steel",  "Water",
};

bool sameString(const char *a, const char *b) {
  while (*a != '\0' && *b != '\0') {
    if (*a != *b) return false;
    a++;
    b++;
  }
  return *a == *b;
}

}  // namespace

uint8_t typeIdFromName(const char *name) {
  if (name == nullptr || name[0] == '\0') return kTypeAny;
  for (uint8_t i = 0; i < kTypeCount; i++) {
    if (sameString(name, kTypeNames[i])) return i;
  }
  return kTypeAny;
}

const char *typeNameFromId(uint8_t id) {
  if (id >= kTypeCount) return nullptr;
  return kTypeNames[id];
}

}  // namespace pokedex
```

- [ ] **Step 4: Run test to verify it passes**

```bash
./scripts/test-pokedex.sh
```

Expected: all assertions PASS, `0 failure(s)`.

- [ ] **Step 5: Commit**

```bash
git add projects/15-pokedex-panel/src/PokedexIndex.h projects/15-pokedex-panel/src/PokedexIndex.cpp projects/15-pokedex-panel/test/host_main.cpp
git commit -m "feat: add Pokedex type name table"
```

---

### Task 3: Variant classification

Variants **compose** — 43 entries carry two or more markers. This is why `flags`
is a bitmask and the classifier must test every marker instead of returning on
the first hit.

**Files:**
- Modify: `projects/15-pokedex-panel/src/PokedexIndex.h`
- Modify: `projects/15-pokedex-panel/src/PokedexIndex.cpp`
- Modify: `projects/15-pokedex-panel/test/host_main.cpp`

- [ ] **Step 1: Write the failing test**

Add to `host_main.cpp` inside `main()`:

```cpp
  expect("classifyVariant plain name is base",
         classifyVariant("bulbasaur") == kVariantBase);
  expect("classifyVariant shadow", classifyVariant("bulbasaur_shadow") == kVariantShadow);
  expect("classifyVariant mega", classifyVariant("venusaur_mega") == kVariantMega);
  expect("classifyVariant mega x keeps mega bit",
         (classifyVariant("charizard_mega_x") & kVariantMega) != 0);
  expect("classifyVariant alolan is regional",
         classifyVariant("rattata_alolan") == kVariantRegional);
  expect("classifyVariant galarian is regional",
         classifyVariant("zigzagoon_galarian") == kVariantRegional);
  expect("classifyVariant hisuian is regional",
         classifyVariant("growlithe_hisuian") == kVariantRegional);
  expect("classifyVariant alolan shadow composes both",
         classifyVariant("rattata_alolan_shadow") ==
             (uint8_t)(kVariantRegional | kVariantShadow));
  expect("classifyVariant costume form is other",
         classifyVariant("pikachu_pop_star") == kVariantOther);
  expect("classifyVariant anniversary form is other",
         classifyVariant("pikachu_5th_anniversary") == kVariantOther);
  expect("classifyVariant null is base", classifyVariant(nullptr) == kVariantBase);
```

- [ ] **Step 2: Run test to verify it fails**

```bash
./scripts/test-pokedex.sh
```

Expected: FAIL to compile with `'classifyVariant' was not declared in this scope`.

- [ ] **Step 3: Write minimal implementation**

Add to `PokedexIndex.h` inside `namespace pokedex`, before `struct Record`:

```cpp
// Variant markers parsed out of an entry_id. These COMPOSE — rattata_alolan_shadow
// is both regional and shadow — so this is a bitmask, never an enum value.
enum Variant : uint8_t {
  kVariantBase = 0x01,
  kVariantShadow = 0x02,
  kVariantMega = 0x04,
  kVariantRegional = 0x08,
  kVariantOther = 0x10,
};
```

Add to `PokedexIndex.h` after the `typeNameFromId` declaration:

```cpp
// Classifies an entry_id into a Variant bitmask. Every marker is tested; a form
// carrying two markers gets both bits.
uint8_t classifyVariant(const char *entryId);
```

Add to the anonymous namespace in `PokedexIndex.cpp`:

```cpp
bool containsToken(const char *haystack, const char *needle) {
  if (haystack == nullptr || needle == nullptr) return false;
  for (const char *h = haystack; *h != '\0'; h++) {
    const char *a = h;
    const char *b = needle;
    while (*a != '\0' && *b != '\0' && *a == *b) {
      a++;
      b++;
    }
    if (*b == '\0') return true;
  }
  return false;
}

bool hasUnderscore(const char *text) { return containsToken(text, "_"); }
```

Add to `PokedexIndex.cpp` after `typeNameFromId`:

```cpp
uint8_t classifyVariant(const char *entryId) {
  if (entryId == nullptr || entryId[0] == '\0') return kVariantBase;

  uint8_t flags = 0;
  if (containsToken(entryId, "_shadow")) flags |= kVariantShadow;
  if (containsToken(entryId, "_mega")) flags |= kVariantMega;
  if (containsToken(entryId, "_galarian") || containsToken(entryId, "_alolan") ||
      containsToken(entryId, "_hisuian") || containsToken(entryId, "_paldean")) {
    flags |= kVariantRegional;
  }
  if (flags != 0) return flags;
  return hasUnderscore(entryId) ? kVariantOther : kVariantBase;
}
```

`_paldean` is included defensively; it may not appear in the current
`index.csv`, and an absent marker costs nothing.

- [ ] **Step 4: Run test to verify it passes**

```bash
./scripts/test-pokedex.sh
```

Expected: all assertions PASS, `0 failure(s)`.

- [ ] **Step 5: Commit**

```bash
git add projects/15-pokedex-panel/src/PokedexIndex.h projects/15-pokedex-panel/src/PokedexIndex.cpp projects/15-pokedex-panel/test/host_main.cpp
git commit -m "feat: classify Pokedex entry variants as a composing bitmask"
```

---

### Task 4: ByteSource and CSV parse

**Files:**
- Modify: `projects/15-pokedex-panel/src/PokedexIndex.h`
- Modify: `projects/15-pokedex-panel/src/PokedexIndex.cpp`
- Modify: `projects/15-pokedex-panel/test/host_main.cpp`

- [ ] **Step 1: Write the failing test**

Add to `host_main.cpp`, in the anonymous namespace above `main()`:

```cpp
// A ByteSource over a fixed string, standing in for the SD file in host tests.
class MemorySource : public ByteSource {
 public:
  MemorySource(const char *text) : text_(text), len_((uint32_t)strlen(text)) {}
  bool seek(uint32_t offset) override {
    if (offset > len_) return false;
    pos_ = offset;
    return true;
  }
  uint32_t read(uint8_t *dest, uint32_t length) override {
    uint32_t n = len_ - pos_;
    if (n > length) n = length;
    memcpy(dest, text_ + pos_, n);
    pos_ += n;
    return n;
  }
  uint32_t size() const override { return len_; }

 private:
  const char *text_;
  uint32_t len_;
  uint32_t pos_ = 0;
};

// Mirrors the real index.csv shape: header, dex order, empty type2 allowed,
// composing variants, and the longest observed 28-character name.
const char *const kFixtureCsv =
    "dex,entry_id,name,type1,type2,file\n"
    "1,bulbasaur,Bulbasaur,Grass,Poison,bulbasaur.json\n"
    "1,bulbasaur_shadow,Bulbasaur Shadow,Grass,Poison,bulbasaur_shadow.json\n"
    "4,charmander,Charmander,Fire,,charmander.json\n"
    "6,charizard_mega_x,Charizard Mega X,Fire,Dragon,charizard_mega_x.json\n"
    "19,rattata_alolan_shadow,Rattata Alolan Shadow,Dark,Normal,rattata_alolan_shadow.json\n"
    "25,pikachu,Pikachu,Electric,,pikachu.json\n"
    "43,oddish,Oddish,Grass,Poison,oddish.json\n"
    "641,thundurus_incarnate_shadow,Thundurus (Incarnate) Shadow,Electric,Flying,x.json\n";

}  // namespace
```

Note the fixture's closing `}  // namespace` replaces the existing one — keep
`expect` and `gFailures` above `MemorySource`.

Add to `main()`:

```cpp
  {
    Record records[16];
    uint16_t nameOrder[16];
    Index index;
    index.attach(records, 16, nameOrder);
    MemorySource source(kFixtureCsv);
    uint16_t parsed = index.build(source);

    expect("build parses 8 data rows, skipping the header", parsed == 8);
    expect("build reports rowCount", index.rowCount() == 8);
    expect("first record dex", index.record(0).dex == 1);
    expect("first record name", strcmp(index.record(0).name, "Bulbasaur") == 0);
    expect("first record type1 is Grass", index.record(0).type1 == typeIdFromName("Grass"));
    expect("first record is base", index.record(0).flags == kVariantBase);
    expect("second record is shadow", index.record(1).flags == kVariantShadow);
    expect("empty type2 does not break parse",
           strcmp(index.record(2).name, "Charmander") == 0);
    expect("composing variant survives parse",
           index.record(4).flags == (uint8_t)(kVariantRegional | kVariantShadow));
    expect("28-char name is not truncated",
           strcmp(index.record(7).name, "Thundurus (Incarnate) Shadow") == 0);
    expect("nameOrder availability reported", index.nameOrderAvailable());

    // Offsets must point at the start of each row so resolve() can seek to it.
    expect("row 0 offset points past the header",
           index.record(0).offset == (uint32_t)(strlen("dex,entry_id,name,type1,type2,file\n")));
    const char *at = kFixtureCsv + index.record(3).offset;
    expect("row 3 offset lands on its own line", strncmp(at, "6,charizard_mega_x", 18) == 0);
  }

  {
    // Capacity smaller than the file must stop cleanly, not overrun.
    Record records[3];
    Index index;
    index.attach(records, 3, nullptr);
    MemorySource source(kFixtureCsv);
    expect("build respects capacity", index.build(source) == 3);
    expect("no name order without a buffer", !index.nameOrderAvailable());
  }

  {
    // A file with only a header yields zero rows rather than a bogus row.
    Record records[4];
    Index index;
    index.attach(records, 4, nullptr);
    MemorySource header("dex,entry_id,name,type1,type2,file\n");
    expect("header-only file parses to zero rows", index.build(header) == 0);
  }
```

- [ ] **Step 2: Run test to verify it fails**

```bash
./scripts/test-pokedex.sh
```

Expected: FAIL to compile with `'ByteSource' has not been declared` and
`'Index' was not declared in this scope`.

- [ ] **Step 3: Write minimal implementation**

Add to `PokedexIndex.h` inside `namespace pokedex`, after `classifyVariant`:

```cpp
// Injected byte reader. Firmware implements this over an SD_MMC File; host tests
// implement it over a memory buffer. Keeps SD_MMC out of this translation unit.
class ByteSource {
 public:
  virtual ~ByteSource() {}
  virtual bool seek(uint32_t offset) = 0;
  virtual uint32_t read(uint8_t *dest, uint32_t length) = 0;
  virtual uint32_t size() const = 0;
};

class Index {
 public:
  // records/capacity and nameOrder are caller-owned. Passing nullptr for
  // nameOrder leaves name ordering unavailable, which is the documented
  // fallback when the PSRAM allocation for it fails.
  void attach(Record *records, uint16_t capacity, uint16_t *nameOrder);

  // Single pass over the CSV. Returns the number of rows parsed.
  uint16_t build(ByteSource &source);

  uint16_t rowCount() const { return rowCount_; }
  bool nameOrderAvailable() const { return nameOrder_ != nullptr; }
  const Record &record(uint16_t rowIndex) const { return records_[rowIndex]; }

 private:
  Record *records_ = nullptr;
  uint16_t capacity_ = 0;
  uint16_t *nameOrder_ = nullptr;
  uint16_t rowCount_ = 0;

  void buildNameOrder();
};
```

Add to the anonymous namespace in `PokedexIndex.cpp`:

```cpp
// Copies field `index` (0-based, comma separated) out of `line` into `out`.
// index.csv has no quoted fields and no embedded commas, so a plain splitter is
// correct here.
bool fieldAt(const char *line, uint8_t index, char *out, uint16_t outSize) {
  uint8_t field = 0;
  const char *start = line;
  for (const char *p = line;; p++) {
    if (*p != ',' && *p != '\0') continue;
    if (field == index) {
      uint16_t len = (uint16_t)(p - start);
      if (len > outSize - 1) len = outSize - 1;
      for (uint16_t i = 0; i < len; i++) out[i] = start[i];
      out[len] = '\0';
      return true;
    }
    if (*p == '\0') return false;
    field++;
    start = p + 1;
  }
}

uint16_t parseDex(const char *text) {
  uint32_t value = 0;
  for (const char *p = text; *p >= '0' && *p <= '9'; p++) {
    value = value * 10 + (uint32_t)(*p - '0');
    if (value > 65535) return 0;
  }
  return (uint16_t)value;
}

// Returns false for the header row, which parses to dex 0 and is thus rejected
// without a special case.
bool parseLine(const char *line, uint32_t offset, Record &out) {
  char field[72];
  if (!fieldAt(line, 0, field, sizeof(field))) return false;
  uint16_t dex = parseDex(field);
  if (dex == 0) return false;
  out.dex = dex;
  out.offset = offset;
  if (!fieldAt(line, 1, field, sizeof(field))) return false;
  out.flags = classifyVariant(field);
  if (!fieldAt(line, 2, out.name, kNameLength)) return false;
  if (!fieldAt(line, 3, field, sizeof(field))) return false;
  out.type1 = typeIdFromName(field);
  return true;
}
```

Add to `PokedexIndex.cpp` after `classifyVariant`:

```cpp
void Index::attach(Record *records, uint16_t capacity, uint16_t *nameOrder) {
  records_ = records;
  capacity_ = capacity;
  nameOrder_ = nameOrder;
  rowCount_ = 0;
}

uint16_t Index::build(ByteSource &source) {
  rowCount_ = 0;
  if (records_ == nullptr || capacity_ == 0) return 0;
  if (!source.seek(0)) return 0;

  uint8_t chunk[512];
  char line[256];
  uint32_t chunkLen = 0;
  uint32_t chunkPos = 0;
  uint32_t absolute = 0;
  uint32_t lineStart = 0;
  uint16_t lineLen = 0;

  for (;;) {
    if (chunkPos >= chunkLen) {
      chunkLen = source.read(chunk, sizeof(chunk));
      chunkPos = 0;
      if (chunkLen == 0) break;
    }
    char c = (char)chunk[chunkPos++];
    absolute++;
    if (c == '\r') continue;
    if (c != '\n') {
      if (lineLen < sizeof(line) - 1) line[lineLen++] = c;
      continue;
    }
    line[lineLen] = '\0';
    if (lineLen > 0 && rowCount_ < capacity_ &&
        parseLine(line, lineStart, records_[rowCount_])) {
      rowCount_++;
    }
    lineLen = 0;
    lineStart = absolute;
    if (rowCount_ >= capacity_) break;
  }

  // A final row with no trailing newline still counts.
  if (lineLen > 0 && rowCount_ < capacity_) {
    line[lineLen] = '\0';
    if (parseLine(line, lineStart, records_[rowCount_])) rowCount_++;
  }

  buildNameOrder();
  return rowCount_;
}

void Index::buildNameOrder() {
  if (nameOrder_ == nullptr) return;
  for (uint16_t i = 0; i < rowCount_; i++) nameOrder_[i] = i;
}
```

`buildNameOrder` only seeds identity ordering here; Task 6 sorts it.

- [ ] **Step 4: Run test to verify it passes**

```bash
./scripts/test-pokedex.sh
```

Expected: all assertions PASS, `0 failure(s)`.

- [ ] **Step 5: Commit**

```bash
git add projects/15-pokedex-panel/src/PokedexIndex.h projects/15-pokedex-panel/src/PokedexIndex.cpp projects/15-pokedex-panel/test/host_main.cpp
git commit -m "feat: parse index.csv into the Pokedex record index"
```

---

### Task 5: Filtering and dex-order paging

**Files:**
- Modify: `projects/15-pokedex-panel/src/PokedexIndex.h`
- Modify: `projects/15-pokedex-panel/src/PokedexIndex.cpp`
- Modify: `projects/15-pokedex-panel/test/host_main.cpp`

- [ ] **Step 1: Write the failing test**

Add to `host_main.cpp` in `main()`:

```cpp
  {
    Record records[16];
    uint16_t nameOrder[16];
    Index index;
    index.attach(records, 16, nameOrder);
    MemorySource source(kFixtureCsv);
    index.build(source);

    Filter all;
    all.showShadows = true;
    expect("countMatching with shadows shown", index.countMatching(all) == 8);

    Filter noShadow;
    noShadow.showShadows = false;
    expect("countMatching hides 3 shadow rows", index.countMatching(noShadow) == 5);

    Filter grass;
    grass.showShadows = true;
    grass.type1 = typeIdFromName("Grass");
    expect("type filter counts Grass rows", index.countMatching(grass) == 3);

    Filter grassNoShadow;
    grassNoShadow.type1 = typeIdFromName("Grass");
    expect("type and shadow filters compose", index.countMatching(grassNoShadow) == 2);

    Filter none;
    none.type1 = typeIdFromName("Steel");
    expect("filter matching nothing counts zero", index.countMatching(none) == 0);
    expect("pageCount is at least 1 when empty", index.pageCount(none) == 1);

    uint16_t rows[kGridPageSize];
    expect("empty filter yields an empty page",
           index.pageAt(0, kOrderDex, none, rows) == 0);

    uint8_t got = index.pageAt(0, kOrderDex, all, rows);
    expect("dex page returns all 8 rows", got == 8);
    expect("dex order preserves file order", rows[0] == 0 && rows[1] == 1 && rows[7] == 7);

    got = index.pageAt(0, kOrderDex, noShadow, rows);
    expect("filtered page skips shadows", got == 5);
    expect("filtered page starts at bulbasaur", rows[0] == 0);
    expect("filtered page second row is charmander", rows[1] == 2);

    expect("page past the end is empty",
           index.pageAt(9, kOrderDex, all, rows) == 0);
    expect("pageCount for 8 rows at 18 per page is 1", index.pageCount(all) == 1);
  }
```

- [ ] **Step 2: Run test to verify it fails**

```bash
./scripts/test-pokedex.sh
```

Expected: FAIL to compile with `'Filter' was not declared in this scope`.

- [ ] **Step 3: Write minimal implementation**

Add to `PokedexIndex.h` inside `namespace pokedex`, after the `Variant` enum:

```cpp
enum Order : uint8_t { kOrderDex = 0, kOrderName = 1 };

struct Filter {
  bool showShadows = false;
  uint8_t type1 = kTypeAny;
};
```

Add to the `Index` public section in `PokedexIndex.h`:

```cpp
  bool matches(uint16_t rowIndex, const Filter &filter) const;
  uint16_t countMatching(const Filter &filter) const;

  // Writes up to kGridPageSize row indices for `page` into outRows, which the
  // caller must size to at least kGridPageSize. Returns how many were written.
  uint8_t pageAt(uint16_t page, Order order, const Filter &filter,
                 uint16_t *outRows) const;

  // Always at least 1 so the UI can render "Page 1/1" for an empty filter.
  uint16_t pageCount(const Filter &filter) const;
```

Add to the `Index` private section:

```cpp
  uint16_t rowAtOrdinal(uint16_t ordinal, Order order) const;
```

Add to `PokedexIndex.cpp` after `buildNameOrder`:

```cpp
uint16_t Index::rowAtOrdinal(uint16_t ordinal, Order order) const {
  if (order == kOrderName && nameOrder_ != nullptr) return nameOrder_[ordinal];
  return ordinal;  // index.csv is already in dex order.
}

bool Index::matches(uint16_t rowIndex, const Filter &filter) const {
  if (rowIndex >= rowCount_) return false;
  const Record &row = records_[rowIndex];
  if (!filter.showShadows && (row.flags & kVariantShadow) != 0) return false;
  if (filter.type1 != kTypeAny && row.type1 != filter.type1) return false;
  return true;
}

uint16_t Index::countMatching(const Filter &filter) const {
  uint16_t total = 0;
  for (uint16_t i = 0; i < rowCount_; i++) {
    if (matches(i, filter)) total++;
  }
  return total;
}

uint8_t Index::pageAt(uint16_t page, Order order, const Filter &filter,
                      uint16_t *outRows) const {
  const uint32_t skip = (uint32_t)page * kGridPageSize;
  uint32_t seen = 0;
  uint8_t written = 0;
  for (uint16_t i = 0; i < rowCount_ && written < kGridPageSize; i++) {
    const uint16_t row = rowAtOrdinal(i, order);
    if (!matches(row, filter)) continue;
    if (seen++ < skip) continue;
    outRows[written++] = row;
  }
  return written;
}

uint16_t Index::pageCount(const Filter &filter) const {
  const uint16_t total = countMatching(filter);
  if (total == 0) return 1;
  return (uint16_t)((total + kGridPageSize - 1) / kGridPageSize);
}
```

- [ ] **Step 4: Run test to verify it passes**

```bash
./scripts/test-pokedex.sh
```

Expected: all assertions PASS, `0 failure(s)`.

- [ ] **Step 5: Commit**

```bash
git add projects/15-pokedex-panel/src/PokedexIndex.h projects/15-pokedex-panel/src/PokedexIndex.cpp projects/15-pokedex-panel/test/host_main.cpp
git commit -m "feat: add Pokedex filtering and dex-order paging"
```

---

### Task 6: Name ordering and jumps

**Files:**
- Modify: `projects/15-pokedex-panel/src/PokedexIndex.h`
- Modify: `projects/15-pokedex-panel/src/PokedexIndex.cpp`
- Modify: `projects/15-pokedex-panel/test/host_main.cpp`

- [ ] **Step 1: Write the failing test**

Add to `host_main.cpp` in `main()`:

```cpp
  {
    Record records[16];
    uint16_t nameOrder[16];
    Index index;
    index.attach(records, 16, nameOrder);
    MemorySource source(kFixtureCsv);
    index.build(source);

    Filter all;
    all.showShadows = true;

    uint16_t rows[kGridPageSize];
    index.pageAt(0, kOrderName, all, rows);
    // Alphabetical: Bulbasaur, Bulbasaur Shadow, Charizard Mega X, Charmander,
    // Oddish, Pikachu, Rattata Alolan Shadow, Thundurus (Incarnate) Shadow.
    expect("name order starts at Bulbasaur",
           strcmp(index.record(rows[0]).name, "Bulbasaur") == 0);
    expect("name order sorts Charizard before Charmander",
           strcmp(index.record(rows[2]).name, "Charizard Mega X") == 0 &&
               strcmp(index.record(rows[3]).name, "Charmander") == 0);
    expect("name order ends at Thundurus",
           strcmp(index.record(rows[7]).name, "Thundurus (Incarnate) Shadow") == 0);

    expect("hasLetter finds B", index.hasLetter('B', all));
    expect("hasLetter finds lowercase b", index.hasLetter('b', all));
    expect("hasLetter rejects Z", !index.hasLetter('Z', all));
    expect("jumpToLetter B is page 0", index.jumpToLetter('B', all) == 0);
    expect("jumpToLetter P is page 0 for a short fixture",
           index.jumpToLetter('P', all) == 0);

    Filter noShadow;
    expect("hasLetter respects the shadow filter", !index.hasLetter('R', noShadow));
    expect("hasLetter finds R with shadows shown", index.hasLetter('R', all));

    Filter grass;
    grass.showShadows = true;
    grass.type1 = typeIdFromName("Grass");
    expect("hasLetter respects the type filter", !index.hasLetter('P', grass));
    expect("hasLetter finds O under the Grass filter", index.hasLetter('O', grass));

    expect("hasDexAtLeast finds 600+", index.hasDexAtLeast(600, all));
    expect("hasDexAtLeast rejects beyond the max", !index.hasDexAtLeast(2000, all));
    expect("jumpToDex 0 is page 0", index.jumpToDex(0, all) == 0);
  }

  {
    // With no name-order buffer, name order must degrade to dex order rather
    // than read a null pointer.
    Record records[16];
    Index index;
    index.attach(records, 16, nullptr);
    MemorySource source(kFixtureCsv);
    index.build(source);

    Filter all;
    all.showShadows = true;
    uint16_t rows[kGridPageSize];
    index.pageAt(0, kOrderName, all, rows);
    expect("name order falls back to file order without a buffer", rows[0] == 0);
    expect("jumpToLetter is page 0 without a name order",
           index.jumpToLetter('T', all) == 0);
    expect("hasLetter is false without a name order", !index.hasLetter('B', all));
  }
```

- [ ] **Step 2: Run test to verify it fails**

```bash
./scripts/test-pokedex.sh
```

Expected: FAIL to compile with `'class pokedex::Index' has no member named 'hasLetter'`.

- [ ] **Step 3: Write minimal implementation**

Add to the `Index` public section in `PokedexIndex.h`:

```cpp
  // Name-order jumps. All return 0 / false when no name-order buffer was
  // attached, which is the PSRAM-failure fallback.
  uint16_t jumpToLetter(char letter, const Filter &filter) const;
  bool hasLetter(char letter, const Filter &filter) const;

  // Dex-order jumps. Valid in either order buffer state.
  uint16_t jumpToDex(uint16_t dex, const Filter &filter) const;
  bool hasDexAtLeast(uint16_t dex, const Filter &filter) const;
```

Add to the anonymous namespace in `PokedexIndex.cpp`:

```cpp
char upperChar(char c) {
  if (c >= 'a' && c <= 'z') return (char)(c - 'a' + 'A');
  return c;
}

// Case-insensitive compare so the A-Z rail groups names the way a reader
// expects regardless of how index.csv capitalises them.
int compareNames(const char *a, const char *b) {
  for (;; a++, b++) {
    const char ca = upperChar(*a);
    const char cb = upperChar(*b);
    if (ca != cb) return ca < cb ? -1 : 1;
    if (ca == '\0') return 0;
  }
}
```

Replace `Index::buildNameOrder` in `PokedexIndex.cpp`:

```cpp
void Index::buildNameOrder() {
  if (nameOrder_ == nullptr) return;
  for (uint16_t i = 0; i < rowCount_; i++) nameOrder_[i] = i;

  // Binary insertion sort. Deliberately dependency-free and stable; boot-time
  // cost on 1573 rows must be measured on hardware, not assumed.
  for (uint16_t i = 1; i < rowCount_; i++) {
    const uint16_t value = nameOrder_[i];
    uint16_t low = 0;
    uint16_t high = i;
    while (low < high) {
      const uint16_t mid = (uint16_t)(low + (high - low) / 2);
      const int cmp = compareNames(records_[value].name, records_[nameOrder_[mid]].name);
      if (cmp < 0 || (cmp == 0 && records_[value].dex < records_[nameOrder_[mid]].dex)) {
        high = mid;
      } else {
        low = (uint16_t)(mid + 1);
      }
    }
    for (uint16_t j = i; j > low; j--) nameOrder_[j] = nameOrder_[j - 1];
    nameOrder_[low] = value;
  }
}
```

Add to `PokedexIndex.cpp` after `pageCount`:

```cpp
uint16_t Index::jumpToLetter(char letter, const Filter &filter) const {
  if (nameOrder_ == nullptr) return 0;
  const char want = upperChar(letter);
  uint32_t ordinal = 0;
  for (uint16_t i = 0; i < rowCount_; i++) {
    const uint16_t row = nameOrder_[i];
    if (!matches(row, filter)) continue;
    if (upperChar(records_[row].name[0]) >= want) {
      return (uint16_t)(ordinal / kGridPageSize);
    }
    ordinal++;
  }
  if (ordinal == 0) return 0;
  return (uint16_t)((ordinal - 1) / kGridPageSize);
}

bool Index::hasLetter(char letter, const Filter &filter) const {
  if (nameOrder_ == nullptr) return false;
  const char want = upperChar(letter);
  for (uint16_t i = 0; i < rowCount_; i++) {
    const uint16_t row = nameOrder_[i];
    if (!matches(row, filter)) continue;
    if (upperChar(records_[row].name[0]) == want) return true;
  }
  return false;
}

uint16_t Index::jumpToDex(uint16_t dex, const Filter &filter) const {
  uint32_t ordinal = 0;
  for (uint16_t i = 0; i < rowCount_; i++) {
    if (!matches(i, filter)) continue;
    if (records_[i].dex >= dex) return (uint16_t)(ordinal / kGridPageSize);
    ordinal++;
  }
  if (ordinal == 0) return 0;
  return (uint16_t)((ordinal - 1) / kGridPageSize);
}

bool Index::hasDexAtLeast(uint16_t dex, const Filter &filter) const {
  for (uint16_t i = 0; i < rowCount_; i++) {
    if (matches(i, filter) && records_[i].dex >= dex) return true;
  }
  return false;
}
```

- [ ] **Step 4: Run test to verify it passes**

```bash
./scripts/test-pokedex.sh
```

Expected: all assertions PASS, `0 failure(s)`.

- [ ] **Step 5: Commit**

```bash
git add projects/15-pokedex-panel/src/PokedexIndex.h projects/15-pokedex-panel/src/PokedexIndex.cpp projects/15-pokedex-panel/test/host_main.cpp
git commit -m "feat: add Pokedex name ordering with letter and dex jumps"
```

---

### Task 7: SD ByteSource and PokedexCatalog delegation

Wires the index into the firmware. `PokedexCatalog`'s public API and mock
fallback do not change.

**Files:**
- Create: `projects/15-pokedex-panel/src/PokedexSdSource.h`
- Modify: `projects/15-pokedex-panel/src/PokedexCatalog.h`
- Modify: `projects/15-pokedex-panel/src/PokedexCatalog.cpp`

- [ ] **Step 1: Create the SD ByteSource**

Create `projects/15-pokedex-panel/src/PokedexSdSource.h`:

```cpp
#ifndef POKEDEX_SD_SOURCE_H
#define POKEDEX_SD_SOURCE_H

#include "../config/ProjectConfig.h"
#include "PokedexIndex.h"

#if USE_SD_POKEDEX
// Include SD_MMC.h directly under the flag - do NOT wrap it in __has_include.
// SD_MMC is on the include path, so a __has_include guard evaluates false at the
// wrong moment and silently disables the feature while still building green.
#include <SD_MMC.h>

// Adapts an SD_MMC File to the pure-C++ ByteSource the index consumes. This is
// the only place SD_MMC and PokedexIndex meet.
class PokedexSdSource : public pokedex::ByteSource {
 public:
  explicit PokedexSdSource(File &file) : file_(file) {}
  bool seek(uint32_t offset) override { return file_.seek(offset); }
  uint32_t read(uint8_t *dest, uint32_t length) override {
    return (uint32_t)file_.read(dest, length);
  }
  uint32_t size() const override { return (uint32_t)file_.size(); }

 private:
  File &file_;
};
#endif

#endif
```

- [ ] **Step 2: Add index storage to PokedexCatalog**

Add to `PokedexCatalog.h` includes:

```cpp
#include "PokedexIndex.h"
```

Add to the `PokedexCatalog` public section:

```cpp
  // Index-backed queries. When the index is unavailable these fall back to the
  // pre-existing linear scan so the project stays usable.
  //
  // Browse is OFFSET-based, not page-based: a jump sets the offset so the target
  // lands at grid slot 0. A page-aligned window cannot do that — measured on the
  // real catalog, tapping "B" left Bagon at slot 14 behind fourteen A-names.
  bool indexActive() const { return indexRows_ > 0; }
  uint16_t indexRowCount() const { return indexRows_; }
  bool nameOrderActive() const;
  uint16_t countMatching(const pokedex::Filter &filter) const;
  uint8_t window(uint32_t startOrdinal, pokedex::Order order,
                 const pokedex::Filter &filter, PokedexRow *outRows, uint8_t maxRows);
  uint32_t ordinalOfLetter(char letter, const pokedex::Filter &filter) const;
  bool hasLetter(char letter, const pokedex::Filter &filter) const;
  uint32_t ordinalOfDex(uint16_t dex, const pokedex::Filter &filter) const;
  bool hasDexAtLeast(uint16_t dex, const pokedex::Filter &filter) const;
  uint32_t clampOrdinal(uint32_t ordinal, const pokedex::Filter &filter) const;
```

Add to the `PokedexCatalog` private section:

```cpp
  pokedex::Index index_;
  pokedex::Record *indexRecords_ = nullptr;
  uint16_t *indexNameOrder_ = nullptr;
  uint16_t indexRows_ = 0;

  void buildIndex();
  void releaseIndex();
  bool resolveRow(uint16_t rowIndex, PokedexRow &out);
```

- [ ] **Step 3: Implement index build in PokedexCatalog.cpp**

Add these includes at the top of `PokedexCatalog.cpp`, after the existing ones:

```cpp
#include "PokedexSdSource.h"
```

Add to `PokedexCatalog.cpp`:

```cpp
void PokedexCatalog::releaseIndex() {
  if (indexRecords_ != nullptr) {
    free(indexRecords_);
    indexRecords_ = nullptr;
  }
  if (indexNameOrder_ != nullptr) {
    free(indexNameOrder_);
    indexNameOrder_ = nullptr;
  }
  indexRows_ = 0;
}

void PokedexCatalog::buildIndex() {
#if USE_SD_POKEDEX && POKEDEX_HAS_SD_MMC
  releaseIndex();
  if (!sdReady_ || !indexReady_) return;

  File file = SD_MMC.open(POKEDEX_INDEX_PATH, FILE_READ);
  if (!file) {
    Logger::warn("index", "cannot open index for indexing; using linear scan");
    return;
  }

  // Cap at the observed catalog size with headroom. 40 bytes per record.
  const uint16_t capacity = 2048;
  indexRecords_ = (pokedex::Record *)ps_malloc(sizeof(pokedex::Record) * capacity);
  if (indexRecords_ == nullptr) {
    file.close();
    Logger::warn("index", "PSRAM index alloc failed; using linear scan");
    return;
  }

  // The name-order array is optional: without it, A-Z ordering is unavailable
  // but dex ordering still works.
  indexNameOrder_ = (uint16_t *)ps_malloc(sizeof(uint16_t) * capacity);
  if (indexNameOrder_ == nullptr) {
    Logger::warn("index", "PSRAM name-order alloc failed; dex order only");
  }

  index_.attach(indexRecords_, capacity, indexNameOrder_);
  PokedexSdSource source(file);
  indexRows_ = index_.build(source);
  file.close();

  if (indexRows_ == 0) {
    releaseIndex();
    Logger::warn("index", "index build produced no rows; using linear scan");
    return;
  }
  Logger::info("index", String("indexed ") + indexRows_ + " rows");
#endif
}

bool PokedexCatalog::nameOrderActive() const {
  return indexRows_ > 0 && index_.nameOrderAvailable();
}

uint16_t PokedexCatalog::countMatching(const pokedex::Filter &filter) const {
  if (indexRows_ == 0) return totalRows_;
  return index_.countMatching(filter);
}

uint32_t PokedexCatalog::ordinalOfLetter(char letter, const pokedex::Filter &filter) const {
  if (indexRows_ == 0) return 0;
  return index_.ordinalOfLetter(letter, filter);
}

bool PokedexCatalog::hasLetter(char letter, const pokedex::Filter &filter) const {
  if (indexRows_ == 0) return false;
  return index_.hasLetter(letter, filter);
}

uint32_t PokedexCatalog::ordinalOfDex(uint16_t dex, const pokedex::Filter &filter) const {
  if (indexRows_ == 0) return 0;
  return index_.ordinalOfDex(dex, filter);
}

uint32_t PokedexCatalog::clampOrdinal(uint32_t ordinal, const pokedex::Filter &filter) const {
  if (indexRows_ == 0) return 0;
  return index_.clampOrdinal(ordinal, filter);
}

bool PokedexCatalog::hasDexAtLeast(uint16_t dex, const pokedex::Filter &filter) const {
  if (indexRows_ == 0) return false;
  return index_.hasDexAtLeast(dex, filter);
}
```

- [ ] **Step 4: Implement row resolution and paging**

Add to `PokedexCatalog.cpp`:

```cpp
bool PokedexCatalog::resolveRow(uint16_t rowIndex, PokedexRow &out) {
#if USE_SD_POKEDEX && POKEDEX_HAS_SD_MMC
  if (indexRows_ == 0 || rowIndex >= indexRows_) return false;
  File file = SD_MMC.open(POKEDEX_INDEX_PATH, FILE_READ);
  if (!file) return false;
  // One seek to the row's recorded byte offset, instead of rescanning the file.
  if (!file.seek(index_.record(rowIndex).offset)) {
    file.close();
    return false;
  }
  String line = file.readStringUntil('\n');
  file.close();
  return parseIndexLine(line, out);
#else
  (void)rowIndex;
  (void)out;
  return false;
#endif
}

uint8_t PokedexCatalog::window(uint32_t startOrdinal, pokedex::Order order,
                               const pokedex::Filter &filter, PokedexRow *outRows,
                               uint8_t maxRows) {
  if (indexRows_ == 0) {
    // Fallback: the pre-existing linear browse. It only understands a row start,
    // which is exactly what an ordinal is when nothing is filtered out.
    return browse((uint16_t)startOrdinal, outRows, maxRows);
  }
  uint16_t rowIndices[pokedex::kGridPageSize];
  const uint8_t found = index_.pageAtOrdinal(startOrdinal, order, filter, rowIndices);
  uint8_t written = 0;
  for (uint8_t i = 0; i < found && written < maxRows; i++) {
    if (resolveRow(rowIndices[i], outRows[written])) written++;
  }
  return written;
}
```

- [ ] **Step 5: Call buildIndex from begin()**

In `PokedexCatalog::begin()`, immediately after the line that sets
`status_ = "SD catalog // " + String(totalRows_) + " entries";`, add:

```cpp
  buildIndex();
```

- [ ] **Step 6: Raise POKEDEX_MAX_RESULTS to the grid page size**

In `projects/15-pokedex-panel/src/PokedexTypes.h`, change:

```cpp
constexpr uint8_t POKEDEX_MAX_RESULTS = 8;
```

to:

```cpp
// Matches pokedex::kGridPageSize so one grid page fits in one result buffer.
constexpr uint8_t POKEDEX_MAX_RESULTS = 18;
```

- [ ] **Step 7: Compile for the target**

`upload-project.sh` always flashes — it has no compile-only mode. To compile
without a board attached, use `compile-all.sh` (which honours `EXTRA_FLAGS`):

```bash
CTAGS_WORKAROUND=1 EXTRA_FLAGS="-DUSE_DISPLAY=1 -DUSE_SD_POKEDEX=1" ./scripts/compile-all.sh
```

Expected: compiles clean. Verify SD linkage by confirming the build actually
included `SD_MMC`:

```bash
ls _arduino-build/15-pokedex-panel/libraries/
```

Expected: an `SD_MMC` directory is present. A green compile alone does not prove
linkage.

- [ ] **Step 8: Run host tests to confirm nothing regressed**

```bash
./scripts/test-pokedex.sh
```

Expected: `0 failure(s)`.

- [ ] **Step 9: Commit**

```bash
git add projects/15-pokedex-panel/src/
git commit -m "feat: back Pokedex browse with the in-PSRAM index"
```

---

### Task 8: Hardware checkpoint — stage 1

- [ ] **Step 1: Flash and observe**

```bash
arduino-cli board list
```

Then, with the reported port:

```bash
CTAGS_WORKAROUND=1 EXTRA_FLAGS="-DUSE_DISPLAY=1 -DUSE_SD_POKEDEX=1" ./scripts/upload-project.sh projects/15-pokedex-panel /dev/cu.usbmodem1101
```

- [ ] **Step 2: Verify on serial at 115200 baud, Newline endings**

Run `status`. Expected: the index row count appears and reports 1573 rows.
Run `browse 900`. Expected: it returns promptly rather than after a visible
pause, because paging is now RAM-side.

Record the observed boot time and any `index` warnings in the session log. If
`PSRAM index alloc failed` appears, the fallback is working but the feature is
not — investigate before continuing to stage 2.

- [ ] **Step 3: Commit any fixes, then update docs**

Only if hardware behaviour was observed, note it in
`docs/full-port-proof-matrix.md`. Do not advance the row past what was actually
seen.

---

## Stage 2 — Sprites

### Task 9: BMP decode

**Files:**
- Create: `projects/15-pokedex-panel/src/PokedexBmp.h`
- Create: `projects/15-pokedex-panel/src/PokedexBmp.cpp`
- Modify: `projects/15-pokedex-panel/test/host_main.cpp`

- [ ] **Step 1: Write the failing test**

Add to `host_main.cpp` includes:

```cpp
#include "../src/PokedexBmp.h"
```

Add to the anonymous namespace in `host_main.cpp`:

```cpp
// Builds a valid 24bpp Windows 3.x BMP of the given size, bottom-up, with a
// deterministic per-pixel colour so decode output can be checked exactly.
// Real sprites are 40x40, where a row is 120 bytes and needs no padding.
void writeLe32(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)(v & 0xFF);
  p[1] = (uint8_t)((v >> 8) & 0xFF);
  p[2] = (uint8_t)((v >> 16) & 0xFF);
  p[3] = (uint8_t)((v >> 24) & 0xFF);
}

void writeLe16(uint8_t *p, uint16_t v) {
  p[0] = (uint8_t)(v & 0xFF);
  p[1] = (uint8_t)((v >> 8) & 0xFF);
}

uint32_t makeBmp(uint8_t *out, uint32_t outSize, int32_t w, int32_t h, uint16_t bpp) {
  const uint32_t stride = (uint32_t)(((w * (bpp / 8)) + 3) & ~3);
  const uint32_t pixels = stride * (uint32_t)(h < 0 ? -h : h);
  const uint32_t total = 54 + pixels;
  if (total > outSize) return 0;
  memset(out, 0, total);
  out[0] = 'B';
  out[1] = 'M';
  writeLe32(out + 2, total);
  writeLe32(out + 10, 54);
  writeLe32(out + 14, 40);
  writeLe32(out + 18, (uint32_t)w);
  writeLe32(out + 22, (uint32_t)h);
  writeLe16(out + 26, 1);
  writeLe16(out + 28, bpp);
  writeLe32(out + 34, pixels);
  // Bottom-up rows: file row 0 is image row h-1. Paint B=x, G=y, R=0 so the
  // decoder's row flip is observable.
  for (int32_t y = 0; y < (h < 0 ? -h : h); y++) {
    uint8_t *row = out + 54 + stride * (uint32_t)y;
    const int32_t imageY = (h > 0) ? ((h - 1) - y) : y;
    for (int32_t x = 0; x < w; x++) {
      row[x * 3 + 0] = (uint8_t)x;
      row[x * 3 + 1] = (uint8_t)imageY;
      row[x * 3 + 2] = 0;
    }
  }
  return total;
}
```

Add to `main()`:

```cpp
  {
    static uint8_t bmp[8192];
    static uint16_t out[40 * 40];

    const uint32_t len = makeBmp(bmp, sizeof(bmp), 40, 40, 24);
    expect("fixture BMP is the real 4854-byte size", len == 4854);

    BmpInfo info;
    expect("readBmpInfo accepts a 40x40 24bpp BMP", readBmpInfo(bmp, len, info));
    expect("readBmpInfo reports width", info.width == 40);
    expect("readBmpInfo reports height", info.height == 40);
    expect("readBmpInfo reports pixel offset", info.pixelOffset == 54);
    expect("readBmpInfo reports no padding for 40px", info.stride == 120);

    expect("decodeBmp fills the target", decodeBmp(bmp, len, out, 40 * 40));
    // Row flip: image row 0 must carry G=0, and the last row G=39.
    const uint16_t topLeft = out[0];
    const uint16_t bottomLeft = out[39 * 40];
    expect("decode flips bottom-up rows", topLeft != bottomLeft);
    // RGB565 green field of image row 0 is 0; of row 39 is nonzero.
    expect("top row green channel is zero", ((topLeft >> 5) & 0x3F) == 0);
    expect("bottom row green channel is nonzero", ((bottomLeft >> 5) & 0x3F) != 0);

    expect("decodeBmp rejects a small target", !decodeBmp(bmp, len, out, 10));

    // Truncated file must be rejected without reading past the buffer.
    expect("readBmpInfo rejects truncation", !readBmpInfo(bmp, 53, info));
    expect("decodeBmp rejects truncated pixels", !decodeBmp(bmp, 100, out, 40 * 40));

    // Wrong bit depth must be rejected.
    const uint32_t len8 = makeBmp(bmp, sizeof(bmp), 40, 40, 8);
    expect("readBmpInfo rejects 8bpp", !readBmpInfo(bmp, len8, info));

    // Not a BMP at all.
    bmp[0] = 'X';
    expect("readBmpInfo rejects a bad magic", !readBmpInfo(bmp, len, info));
  }
```

- [ ] **Step 2: Run test to verify it fails**

```bash
./scripts/test-pokedex.sh
```

Expected: FAIL to compile with `PokedexBmp.h: No such file or directory`.

- [ ] **Step 3: Write minimal implementation**

Create `projects/15-pokedex-panel/src/PokedexBmp.h`:

```cpp
#ifndef POKEDEX_BMP_H
#define POKEDEX_BMP_H

// Pure C++ 24bpp BMP decoder for the SD sprite pack. Free of Arduino.h so the
// shipping translation unit also builds under g++ (scripts/test-pokedex.sh).
//
// The sprite pack is uniformly Windows 3.x, 54-byte header, 24bpp, bottom-up,
// 40x40. At 40px a row is 120 bytes, already a multiple of 4, so real sprites
// carry no row padding - but stride is computed properly anyway.
#include <stdint.h>

namespace pokedex {

struct BmpInfo {
  int32_t width = 0;
  int32_t height = 0;      // Always positive; topDown records the original sign.
  uint32_t pixelOffset = 0;
  uint32_t stride = 0;
  bool topDown = false;
};

// Validates the header and fills info. Returns false for anything that is not a
// 24bpp uncompressed BMP that fits inside `length`.
bool readBmpInfo(const uint8_t *bytes, uint32_t length, BmpInfo &info);

// Decodes to RGB565, top-down, into `out` which must hold width*height pixels.
bool decodeBmp(const uint8_t *bytes, uint32_t length, uint16_t *out, uint32_t outPixels);

// Nearest-neighbour integer upscale. `scale` must be >= 1. `out` must hold
// (srcW*scale)*(srcH*scale) pixels.
bool upscaleNearest(const uint16_t *src, int32_t srcW, int32_t srcH, uint8_t scale,
                    uint16_t *out, uint32_t outPixels);

}  // namespace pokedex

#endif
```

Create `projects/15-pokedex-panel/src/PokedexBmp.cpp`:

```cpp
#include "PokedexBmp.h"

namespace pokedex {
namespace {

uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

uint32_t le32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}

uint16_t toRgb565(uint8_t r, uint8_t g, uint8_t b) {
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

}  // namespace

bool readBmpInfo(const uint8_t *bytes, uint32_t length, BmpInfo &info) {
  if (bytes == nullptr || length < 54) return false;
  if (bytes[0] != 'B' || bytes[1] != 'M') return false;

  const uint16_t bpp = le16(bytes + 28);
  if (bpp != 24) return false;
  if (le32(bytes + 30) != 0) return false;  // BI_RGB only.

  const int32_t width = (int32_t)le32(bytes + 18);
  const int32_t rawHeight = (int32_t)le32(bytes + 22);
  if (width <= 0 || rawHeight == 0) return false;

  info.width = width;
  info.topDown = rawHeight < 0;
  info.height = info.topDown ? -rawHeight : rawHeight;
  info.pixelOffset = le32(bytes + 10);
  info.stride = (uint32_t)((width * 3 + 3) & ~3);

  if (info.pixelOffset < 54 || info.pixelOffset > length) return false;
  const uint32_t needed = info.stride * (uint32_t)info.height;
  if (needed > length - info.pixelOffset) return false;
  return true;
}

bool decodeBmp(const uint8_t *bytes, uint32_t length, uint16_t *out, uint32_t outPixels) {
  BmpInfo info;
  if (!readBmpInfo(bytes, length, info)) return false;
  if (out == nullptr) return false;
  const uint32_t pixels = (uint32_t)info.width * (uint32_t)info.height;
  if (outPixels < pixels) return false;

  for (int32_t y = 0; y < info.height; y++) {
    // BMP rows are bottom-up unless the height was negative.
    const int32_t fileRow = info.topDown ? y : (info.height - 1 - y);
    const uint8_t *row = bytes + info.pixelOffset + info.stride * (uint32_t)fileRow;
    uint16_t *dest = out + (uint32_t)y * (uint32_t)info.width;
    for (int32_t x = 0; x < info.width; x++) {
      // 24bpp BMP stores BGR.
      dest[x] = toRgb565(row[x * 3 + 2], row[x * 3 + 1], row[x * 3 + 0]);
    }
  }
  return true;
}

bool upscaleNearest(const uint16_t *src, int32_t srcW, int32_t srcH, uint8_t scale,
                    uint16_t *out, uint32_t outPixels) {
  if (src == nullptr || out == nullptr || scale == 0) return false;
  if (srcW <= 0 || srcH <= 0) return false;
  const uint32_t dstW = (uint32_t)srcW * scale;
  const uint32_t dstH = (uint32_t)srcH * scale;
  if (outPixels < dstW * dstH) return false;

  for (uint32_t y = 0; y < dstH; y++) {
    const uint16_t *srcRow = src + (uint32_t)srcW * (y / scale);
    uint16_t *dstRow = out + dstW * y;
    for (uint32_t x = 0; x < dstW; x++) {
      dstRow[x] = srcRow[x / scale];
    }
  }
  return true;
}

}  // namespace pokedex
```

- [ ] **Step 4: Run test to verify it passes**

```bash
./scripts/test-pokedex.sh
```

Expected: all assertions PASS, `0 failure(s)`.

- [ ] **Step 5: Commit**

```bash
git add projects/15-pokedex-panel/src/PokedexBmp.h projects/15-pokedex-panel/src/PokedexBmp.cpp projects/15-pokedex-panel/test/host_main.cpp
git commit -m "feat: add pure C++ BMP decoder for Pokedex sprites"
```

---

### Task 10: Upscale tests

**Files:**
- Modify: `projects/15-pokedex-panel/test/host_main.cpp`

- [ ] **Step 1: Write the failing test**

Add to `main()`:

```cpp
  {
    // 2x2 source, scale 2 -> 4x4 where each source pixel becomes a 2x2 block.
    const uint16_t src[4] = {0x0001, 0x0002, 0x0003, 0x0004};
    uint16_t dst[16];
    expect("upscaleNearest 2x on a 2x2 source",
           upscaleNearest(src, 2, 2, 2, dst, 16));
    expect("upscale top-left block", dst[0] == 1 && dst[1] == 1 && dst[4] == 1 && dst[5] == 1);
    expect("upscale top-right block", dst[2] == 2 && dst[3] == 2 && dst[6] == 2 && dst[7] == 2);
    expect("upscale bottom-left block",
           dst[8] == 3 && dst[9] == 3 && dst[12] == 3 && dst[13] == 3);
    expect("upscale bottom-right block",
           dst[10] == 4 && dst[11] == 4 && dst[14] == 4 && dst[15] == 4);

    expect("scale 1 is a copy", upscaleNearest(src, 2, 2, 1, dst, 4));
    expect("scale 1 preserves pixels", dst[0] == 1 && dst[3] == 4);

    expect("upscale rejects a small target", !upscaleNearest(src, 2, 2, 2, dst, 15));
    expect("upscale rejects scale 0", !upscaleNearest(src, 2, 2, 0, dst, 16));

    // The two real target sizes must fit their documented buffers exactly.
    expect("40x40 at 2x needs 6400 pixels", (40 * 2) * (40 * 2) == 6400);
    expect("40x40 at 8x needs 102400 pixels", (40 * 8) * (40 * 8) == 102400);
  }
```

- [ ] **Step 2: Run test to verify it passes**

```bash
./scripts/test-pokedex.sh
```

Expected: all assertions PASS. `upscaleNearest` was implemented in Task 9, so
this task adds the coverage that pins its contract. If any assertion fails, fix
`upscaleNearest` — do not adjust the test to match the bug.

- [ ] **Step 3: Commit**

```bash
git add projects/15-pokedex-panel/test/host_main.cpp
git commit -m "test: pin nearest-neighbour upscale behaviour"
```

---

### Task 11: PokedexSprites cache

**Files:**
- Modify: `shared/CrowPanelShared/AppConfig.h`
- Modify: `projects/15-pokedex-panel/config/ProjectConfig.h`
- Create: `projects/15-pokedex-panel/src/PokedexSprites.h`
- Create: `projects/15-pokedex-panel/src/PokedexSprites.cpp`

- [ ] **Step 1: Add the flag default**

In `shared/CrowPanelShared/AppConfig.h`, alongside the other flag blocks, add:

```cpp
#ifndef USE_POKEDEX_SPRITES
#define USE_POKEDEX_SPRITES 0
#endif

#ifndef USE_POKEDEX_AUDIO
#define USE_POKEDEX_AUDIO 0
#endif
```

- [ ] **Step 2: Turn sprites on for this project**

In `projects/15-pokedex-panel/config/ProjectConfig.h`, after the
`POKEDEX_META_PATH` block, add:

```cpp
// Sprites live beside the catalog on the same card. 40x40 24-bit BMP each.
#ifndef USE_POKEDEX_SPRITES
#define USE_POKEDEX_SPRITES 1
#endif

#ifndef POKEDEX_SPRITE_DIR
#define POKEDEX_SPRITE_DIR "/pokemon/sprites/"
#endif

// Grid tiles are 40x40 at 2x; the detail hero is 40x40 at 8x.
#ifndef POKEDEX_SPRITE_TILE_SCALE
#define POKEDEX_SPRITE_TILE_SCALE 2
#endif

#ifndef POKEDEX_SPRITE_HERO_SCALE
#define POKEDEX_SPRITE_HERO_SCALE 8
#endif

// One page of tiles, so a full grid repaint never evicts a tile it still needs.
#ifndef POKEDEX_SPRITE_CACHE_SLOTS
#define POKEDEX_SPRITE_CACHE_SLOTS 18
#endif
```

- [ ] **Step 3: Create the sprite cache**

Create `projects/15-pokedex-panel/src/PokedexSprites.h`:

```cpp
#ifndef POKEDEX_SPRITES_H
#define POKEDEX_SPRITES_H

#include "../config/ProjectConfig.h"
#include <Arduino.h>
#include "PokedexBmp.h"

// Loads sprite BMPs off SD, upscales them, and keeps one grid page of tiles plus
// one hero image in PSRAM. All buffers are PSRAM; a failed allocation degrades to
// "no sprite" rather than aborting, and the UI falls back to the drawn pokeball.
class PokedexSprites {
 public:
  void begin();

  // Returns a tile-scaled RGB565 buffer for entryId, or nullptr when the sprite
  // is missing, malformed, or could not be cached.
  const uint16_t *tile(const char *entryId);
  int16_t tileSize() const { return tileSize_; }

  // Returns the hero-scaled buffer for entryId, or nullptr. Only one hero is
  // resident at a time; requesting a different entry replaces it.
  const uint16_t *hero(const char *entryId);
  int16_t heroSize() const { return heroSize_; }

  bool ready() const { return ready_; }
  uint32_t hits() const { return hits_; }
  uint32_t misses() const { return misses_; }
  uint32_t failures() const { return failures_; }
  uint32_t lastDecodeMicros() const { return lastDecodeMicros_; }
  void printDiagnostics(Print &out) const;

 private:
  struct Slot {
    char entryId[48] = "";
    uint16_t *pixels = nullptr;
    uint32_t lastUsed = 0;
  };

  bool ready_ = false;
  int16_t tileSize_ = 0;
  int16_t heroSize_ = 0;
  uint32_t clock_ = 0;
  uint32_t hits_ = 0;
  uint32_t misses_ = 0;
  uint32_t failures_ = 0;
  uint32_t lastDecodeMicros_ = 0;

  Slot slots_[POKEDEX_SPRITE_CACHE_SLOTS];
  char heroEntry_[48] = "";
  uint16_t *heroPixels_ = nullptr;
  uint8_t *fileBuffer_ = nullptr;
  uint16_t *decodeBuffer_ = nullptr;

  bool loadScaled(const char *entryId, uint8_t scale, uint16_t *dest, uint32_t destPixels);
  int8_t findSlot(const char *entryId) const;
  int8_t evictSlot();
};

#endif
```

Create `projects/15-pokedex-panel/src/PokedexSprites.cpp`:

```cpp
#include "PokedexSprites.h"

#include <CrowPanelShared.h>

#if USE_POKEDEX_SPRITES && USE_SD_POKEDEX
// Include SD_MMC.h directly under the flag - never behind __has_include, which
// evaluates false here and silently disables sprites while still building green.
#include <SD_MMC.h>
#define POKEDEX_SPRITES_HAVE_SD 1
#else
#define POKEDEX_SPRITES_HAVE_SD 0
#endif

namespace {
// Sprites are a uniform 40x40 in the shipped pack. The decoder still reads the
// real dimensions; these only size the fixed buffers.
constexpr int16_t kNativeSize = 40;
constexpr uint32_t kNativePixels = (uint32_t)kNativeSize * kNativeSize;
constexpr uint32_t kFileBufferBytes = 8192;  // 4854 for a 40x40, with headroom.
}  // namespace

void PokedexSprites::begin() {
#if POKEDEX_SPRITES_HAVE_SD
  tileSize_ = kNativeSize * POKEDEX_SPRITE_TILE_SCALE;
  heroSize_ = kNativeSize * POKEDEX_SPRITE_HERO_SCALE;

  const uint32_t tilePixels = (uint32_t)tileSize_ * tileSize_;
  const uint32_t heroPixels = (uint32_t)heroSize_ * heroSize_;

  fileBuffer_ = (uint8_t *)ps_malloc(kFileBufferBytes);
  decodeBuffer_ = (uint16_t *)ps_malloc(kNativePixels * sizeof(uint16_t));
  heroPixels_ = (uint16_t *)ps_malloc(heroPixels * sizeof(uint16_t));
  if (fileBuffer_ == nullptr || decodeBuffer_ == nullptr || heroPixels_ == nullptr) {
    Logger::warn("sprites", "PSRAM alloc failed; sprites disabled");
    ready_ = false;
    return;
  }

  for (uint8_t i = 0; i < POKEDEX_SPRITE_CACHE_SLOTS; i++) {
    slots_[i].pixels = (uint16_t *)ps_malloc(tilePixels * sizeof(uint16_t));
    if (slots_[i].pixels == nullptr) {
      Logger::warn("sprites", String("tile slot ") + i + " alloc failed");
    }
  }
  ready_ = true;
  Logger::info("sprites", String("cache ready: ") + tileSize_ + "px tiles, " +
                              heroSize_ + "px hero");
#else
  ready_ = false;
#endif
}

int8_t PokedexSprites::findSlot(const char *entryId) const {
  for (uint8_t i = 0; i < POKEDEX_SPRITE_CACHE_SLOTS; i++) {
    if (slots_[i].pixels != nullptr && slots_[i].entryId[0] != '\0' &&
        strcmp(slots_[i].entryId, entryId) == 0) {
      return (int8_t)i;
    }
  }
  return -1;
}

int8_t PokedexSprites::evictSlot() {
  int8_t best = -1;
  uint32_t oldest = 0xFFFFFFFF;
  for (uint8_t i = 0; i < POKEDEX_SPRITE_CACHE_SLOTS; i++) {
    if (slots_[i].pixels == nullptr) continue;
    if (slots_[i].entryId[0] == '\0') return (int8_t)i;  // Free slot wins.
    if (slots_[i].lastUsed < oldest) {
      oldest = slots_[i].lastUsed;
      best = (int8_t)i;
    }
  }
  return best;
}

bool PokedexSprites::loadScaled(const char *entryId, uint8_t scale, uint16_t *dest,
                                uint32_t destPixels) {
#if POKEDEX_SPRITES_HAVE_SD
  if (!ready_ || entryId == nullptr || entryId[0] == '\0') return false;

  const uint32_t started = micros();
  String path = String(POKEDEX_SPRITE_DIR) + entryId + ".bmp";
  File file = SD_MMC.open(path.c_str(), FILE_READ);
  if (!file) {
    failures_++;
    return false;
  }
  const uint32_t size = (uint32_t)file.size();
  if (size == 0 || size > kFileBufferBytes) {
    file.close();
    failures_++;
    return false;
  }
  const uint32_t read = (uint32_t)file.read(fileBuffer_, size);
  file.close();
  if (read != size) {
    failures_++;
    return false;
  }

  pokedex::BmpInfo info;
  if (!pokedex::readBmpInfo(fileBuffer_, size, info)) {
    Logger::warn("sprites", String("bad BMP header: ") + entryId);
    failures_++;
    return false;
  }
  if (info.width != kNativeSize || info.height != kNativeSize) {
    Logger::warn("sprites", String("unexpected sprite size: ") + entryId);
    failures_++;
    return false;
  }
  if (!pokedex::decodeBmp(fileBuffer_, size, decodeBuffer_, kNativePixels)) {
    failures_++;
    return false;
  }
  if (!pokedex::upscaleNearest(decodeBuffer_, info.width, info.height, scale, dest,
                               destPixels)) {
    failures_++;
    return false;
  }
  lastDecodeMicros_ = micros() - started;
  return true;
#else
  (void)entryId;
  (void)scale;
  (void)dest;
  (void)destPixels;
  return false;
#endif
}

const uint16_t *PokedexSprites::tile(const char *entryId) {
  if (!ready_ || entryId == nullptr || entryId[0] == '\0') return nullptr;

  const int8_t found = findSlot(entryId);
  if (found >= 0) {
    hits_++;
    slots_[found].lastUsed = ++clock_;
    return slots_[found].pixels;
  }

  const int8_t slot = evictSlot();
  if (slot < 0) return nullptr;

  const uint32_t tilePixels = (uint32_t)tileSize_ * tileSize_;
  if (!loadScaled(entryId, POKEDEX_SPRITE_TILE_SCALE, slots_[slot].pixels, tilePixels)) {
    slots_[slot].entryId[0] = '\0';
    return nullptr;
  }
  misses_++;
  snprintf(slots_[slot].entryId, sizeof(slots_[slot].entryId), "%s", entryId);
  slots_[slot].lastUsed = ++clock_;
  return slots_[slot].pixels;
}

const uint16_t *PokedexSprites::hero(const char *entryId) {
  if (!ready_ || heroPixels_ == nullptr || entryId == nullptr || entryId[0] == '\0') {
    return nullptr;
  }
  if (strcmp(heroEntry_, entryId) == 0) {
    hits_++;
    return heroPixels_;
  }
  const uint32_t heroPixelCount = (uint32_t)heroSize_ * heroSize_;
  if (!loadScaled(entryId, POKEDEX_SPRITE_HERO_SCALE, heroPixels_, heroPixelCount)) {
    heroEntry_[0] = '\0';
    return nullptr;
  }
  misses_++;
  snprintf(heroEntry_, sizeof(heroEntry_), "%s", entryId);
  return heroPixels_;
}

void PokedexSprites::printDiagnostics(Print &out) const {
  out.print(F("sprites ready="));
  out.print(ready_ ? F("yes") : F("no"));
  out.print(F(" hits="));
  out.print(hits_);
  out.print(F(" miss="));
  out.print(misses_);
  out.print(F(" fail="));
  out.print(failures_);
  out.print(F(" lastUs="));
  out.println(lastDecodeMicros_);
}
```

- [ ] **Step 4: Compile with sprites on**

```bash
CTAGS_WORKAROUND=1 EXTRA_FLAGS="-DUSE_DISPLAY=1 -DUSE_SD_POKEDEX=1 -DUSE_POKEDEX_SPRITES=1" ./scripts/compile-all.sh
```

Expected: clean compile. Confirm no `__has_include` crept in:

```bash
grep -rn "__has_include" projects/15-pokedex-panel/src/
```

Expected: no matches.

- [ ] **Step 5: Commit**

```bash
git add shared/CrowPanelShared/AppConfig.h projects/15-pokedex-panel/config/ProjectConfig.h projects/15-pokedex-panel/src/PokedexSprites.h projects/15-pokedex-panel/src/PokedexSprites.cpp
git commit -m "feat: add PSRAM sprite cache for the Pokedex panel"
```

---

## Stage 3 — Grid UI

### Task 12: Grid and rail layout constants

**Files:**
- Modify: `projects/15-pokedex-panel/src/PokedexTypes.h`
- Modify: `projects/15-pokedex-panel/src/PokedexDashboard.h`
- Modify: `projects/15-pokedex-panel/src/PokedexDashboard.cpp`

- [ ] **Step 1: Add the UI event types**

In `projects/15-pokedex-panel/src/PokedexTypes.h`, add to `PokedexUiEventType`
before the closing brace:

```cpp
  kPokedexUiJumpTop,
  kPokedexUiJumpLetter,
  kPokedexUiJumpDex,
  kPokedexUiToggleSort,
  kPokedexUiToggleShadows,
  kPokedexUiCycleType,
  kPokedexUiSelectTab
```

Add to `struct PokedexUiEvent`:

```cpp
  char letter = '\0';   // kPokedexUiJumpLetter
  uint16_t dex = 0;     // kPokedexUiJumpDex
  uint8_t tab = 0;      // kPokedexUiSelectTab
```

- [ ] **Step 2: Add the layout constants**

In `projects/15-pokedex-panel/src/PokedexDashboard.cpp`, after the existing
`kRowH` constant, add:

```cpp
// Grid browse geometry. The rail takes a fixed strip on the left; the remaining
// width divides into 6 columns and 3 rows of 18 tiles total.
constexpr int16_t kRailX = 24;
constexpr int16_t kRailY = 104;
constexpr int16_t kRailW = 40;
constexpr int16_t kRailH = 420;
constexpr uint8_t kRailSlots = 26;  // A-Z, or 11 dex buckets using the first 11.
constexpr uint8_t kDexBuckets = 11;  // 0,100,...,1000

constexpr int16_t kGridX = 76;
constexpr int16_t kGridY = 104;
constexpr int16_t kGridW = 924;
constexpr int16_t kGridH = 420;
constexpr uint8_t kGridCols = 6;
constexpr uint8_t kGridRows = 3;
constexpr int16_t kGridCellW = kGridW / kGridCols;   // 154
constexpr int16_t kGridCellH = kGridH / kGridRows;   // 140

static_assert(kGridCols * kGridRows == POKEDEX_MAX_RESULTS,
              "grid must hold exactly one result page");
```

- [ ] **Step 3: Add the dashboard state and draw declarations**

Browse is **offset-based**, not page-based (see Task 7's `PokedexCatalog::window`):
a jump sets a row offset so the target lands at grid slot 0, rather than landing
on the page containing it. Measured on the real catalog, page-based jumps put
`Bagon` at slot 14 behind fourteen A-names — offsets fix that. The footer
therefore shows a **range** (`51-68 of 1114`), not a page number.

Add to the `PokedexDashboard` public section in `PokedexDashboard.h`:

```cpp
  // Grid browse state, owned by the sketch and pushed in with each repaint.
  // startOrdinal is the offset of slot 0 in the filtered, ordered sequence.
  void showGrid(const PokedexRow *rows, uint8_t count, uint8_t selected,
                uint32_t startOrdinal, uint32_t totalMatching,
                pokedex::Order order, const pokedex::Filter &filter,
                const bool *railEnabled, const String &source, const String &status);
  void attachSprites(class PokedexSprites *sprites) { sprites_ = sprites; }
```

Add to the private section:

```cpp
  uint32_t startOrdinal_ = 0;
  uint32_t totalMatching_ = 0;
  pokedex::Order order_ = pokedex::kOrderDex;
  pokedex::Filter filter_;
  bool railEnabled_[kRailSlotsMax] = {false};
  uint8_t railCursor_ = 0;
  class PokedexSprites *sprites_ = nullptr;
```

Add near the top of `PokedexDashboard.h`, after the includes:

```cpp
#include "PokedexIndex.h"

// Upper bound on jump-rail entries so the enabled-state array is fixed size.
constexpr uint8_t kRailSlotsMax = 26;
```

Add to the display-gated private block:

```cpp
  void drawGrid(class Arduino_GFX *g);
  void drawRail(class Arduino_GFX *g);
  void drawTile(class Arduino_GFX *g, uint8_t slot, const PokedexRow &row, bool active);
```

- [ ] **Step 4: Compile**

```bash
CTAGS_WORKAROUND=1 EXTRA_FLAGS="-DUSE_DISPLAY=1 -DUSE_SD_POKEDEX=1 -DUSE_POKEDEX_SPRITES=1" ./scripts/compile-all.sh
```

Expected: clean compile. The new draw functions are declared but unused, which
is fine until Task 13.

- [ ] **Step 5: Commit**

```bash
git add projects/15-pokedex-panel/src/
git commit -m "feat: add Pokedex grid layout constants and UI event types"
```

---

### Task 13: Draw the grid and rail

**Files:**
- Modify: `projects/15-pokedex-panel/src/PokedexDashboard.cpp`

- [ ] **Step 1: Implement showGrid**

Add to `PokedexDashboard.cpp`, next to `showList`:

```cpp
void PokedexDashboard::showGrid(const PokedexRow *rows, uint8_t count, uint8_t selected,
                                uint32_t startOrdinal, uint32_t totalMatching,
                                pokedex::Order order, const pokedex::Filter &filter,
                                const bool *railEnabled, const String &source,
                                const String &status) {
  rowCount_ = min(count, (uint8_t)POKEDEX_MAX_RESULTS);
  for (uint8_t i = 0; i < rowCount_; i++) rows_[i] = rows[i];
  selected_ = (rowCount_ == 0) ? 0 : min(selected, (uint8_t)(rowCount_ - 1));
  startOrdinal_ = startOrdinal;
  totalMatching_ = totalMatching;
  order_ = order;
  filter_ = filter;
  for (uint8_t i = 0; i < kRailSlotsMax; i++) {
    railEnabled_[i] = railEnabled != nullptr && railEnabled[i];
  }
  source_ = source;
  status_ = status;
  mode_ = kPokedexUiList;
  dirty_ = true;
```

Leave the `railCursor_` block (added below in Step 2) inside this function,
before the closing brace.

- [ ] **Step 1b: Keep showList working as a grid**

`showList` is still called for **search results** (`cmdSearch` / `cmdOpen` paths),
which are not a filtered catalog window and have no meaningful offset. Without
this it would render a grid over stale browse state. Replace the body of
`showList` so it forwards to the grid with neutral chrome:

```cpp
void PokedexDashboard::showList(const PokedexRow *rows, uint8_t count, uint8_t selected,
                                uint16_t browseStart, uint16_t totalRows,
                                const String &query, const String &source,
                                const String &status) {
  // Search results are a one-off set, not a catalog window: offset 0, and leave
  // the rail disabled so nothing suggests a position in the catalog.
  (void)browseStart;
  query_ = query;
  pokedex::Filter neutral;
  neutral.showShadows = true;
  showGrid(rows, count, selected, 0, totalRows, order_, neutral, nullptr, source,
           status);
}
```

Passing `nullptr` for `railEnabled` dims every rail slot, which `showGrid`
already handles.

- [ ] **Step 2: Implement drawRail**

Add to the display-gated section of `PokedexDashboard.cpp`:

```cpp
void PokedexDashboard::drawRail(Arduino_GFX *g) {
  Widgets::panel(g, kRailX, kRailY, kRailW, kRailH, 6, kCard, 1, kLine);
  const uint8_t slots = (order_ == pokedex::kOrderName) ? kRailSlots : kDexBuckets;
  const int16_t slotH = kRailH / slots;

  for (uint8_t i = 0; i < slots; i++) {
    const int16_t y = kRailY + i * slotH;
    char label[6];
    if (order_ == pokedex::kOrderName) {
      label[0] = (char)('A' + i);
      label[1] = '\0';
    } else {
      snprintf(label, sizeof(label), "%u", (unsigned)(i * 100));
    }
    // A letter or bucket with no rows under the active filter is dimmed and
    // ignored by the touch handler.
    const bool enabled = railEnabled_[i];
    const uint16_t colour = enabled ? kWhite : kLine;
    if (enabled && i == railCursor_) {
      Widgets::panel(g, kRailX + 3, y + 1, kRailW - 6, slotH - 2, 4, kCardHi, 0, kCardHi);
    }
    PokedexText::draw(g, kRailX + kRailW / 2, y + (slotH - 12) / 2, label,
                      PokedexText::fontS(), enabled && i == railCursor_ ? kGold : colour,
                      PokedexText::kCenter);
  }
}
```

`railCursor_` was declared in Task 12. Add this at the end of `showGrid`, still
inside the function body, then close the function with `}`:

```cpp
  railCursor_ = 0;
  if (rowCount_ > 0) {
    if (order_ == pokedex::kOrderName) {
      const char first = rows_[0].name[0];
      const char upper = (first >= 'a' && first <= 'z') ? (char)(first - 'a' + 'A') : first;
      if (upper >= 'A' && upper <= 'Z') railCursor_ = (uint8_t)(upper - 'A');
    } else {
      const uint16_t dex = (uint16_t)atoi(rows_[0].dex);
      const uint8_t bucket = (uint8_t)(dex / 100);
      railCursor_ = bucket < kDexBuckets ? bucket : (uint8_t)(kDexBuckets - 1);
    }
  }
}
```

- [ ] **Step 3: Implement drawTile and drawGrid**

Add to the display-gated section:

```cpp
void PokedexDashboard::drawTile(Arduino_GFX *g, uint8_t slot, const PokedexRow &row,
                                bool active) {
  const int16_t col = slot % kGridCols;
  const int16_t rowIndex = slot / kGridCols;
  const int16_t x = kGridX + col * kGridCellW;
  const int16_t y = kGridY + rowIndex * kGridCellH;
  const uint16_t fill = active ? Widgets::rgb(0x27, 0x38, 0x4C) : Widgets::rgb(0x10, 0x17, 0x21);
  const uint16_t border = active ? typeColor(row.type1) : kLine;

  Widgets::panel(g, x + 4, y + 4, kGridCellW - 8, kGridCellH - 8, 6, fill,
                 active ? 2 : 1, border);

  // Sprite, centred. Sprite BMPs have no alpha, so the well MUST be filled with
  // literal RGB565 0x0000 (not kInk or any other "black-ish" colour) and the
  // blit keys out the sprite's own sampled background colour.
  //
  // Why the well has to be exactly 0x0000: a black-background sprite's interior
  // outline/eye pixels are ALSO literal 0x0000 (1318 of 1573 sprites have such
  // pixels), and the transparent-colour blit cannot distinguish "background
  // black" from "outline black" - both get skipped identically. That only reads
  // correctly if the pixels underneath are the same 0x0000, so the skipped
  // outline pixels still show as black rather than as whatever tile colour was
  // there. Any other well colour would punch the outlines out of those sprites.
  const int16_t artCx = x + kGridCellW / 2;
  const int16_t artCy = y + 12 + 40;
  uint16_t key = 0;
  const uint16_t *pixels =
      (sprites_ != nullptr) ? sprites_->tile(row.entryId, &key) : nullptr;
  if (pixels != nullptr) {
    const int16_t size = sprites_->tileSize();
    g->fillRect(artCx - size / 2, y + 12, size, size, 0x0000);
    g->draw16bitRGBBitmapWithTranColor(artCx - size / 2, y + 12, (uint16_t *)pixels,
                                       key, size, size);
  } else {
    drawPokeball(g, artCx, artCy, 56, typeColor(row.type1));
  }

  String name = fitText(g, row.name, kGridCellW - 20, PokedexText::fontS());
  PokedexText::draw(g, artCx, y + kGridCellH - 44, name.c_str(), PokedexText::fontS(),
                    active ? kWhite : kMuted, PokedexText::kCenter);
  char dex[12];
  snprintf(dex, sizeof(dex), "#%s", row.dex);
  PokedexText::draw(g, artCx, y + kGridCellH - 24, dex, PokedexText::fontS(),
                    typeColor(row.type1), PokedexText::kCenter);
}

void PokedexDashboard::drawGrid(Arduino_GFX *g) {
  drawHeader(g, "POKEDEX");
  drawRail(g);

  if (rowCount_ == 0) {
    Widgets::panel(g, kGridX, kGridY, kGridW, kGridH, 8, kCard, 1, kLine);
    PokedexText::draw(g, kGridX + kGridW / 2, kGridY + 180, "No matches",
                      PokedexText::fontL(), kWhite, PokedexText::kCenter);
    PokedexText::draw(g, kGridX + kGridW / 2, kGridY + 226,
                      "Clear the type filter or enable shadows.", PokedexText::fontS(),
                      kMuted, PokedexText::kCenter);
  } else {
    for (uint8_t i = 0; i < rowCount_; i++) {
      drawTile(g, i, rows_[i], i == selected_);
    }
  }
  drawFooter(g);
  drawTouchDot(g);
}
```

- [ ] **Step 4: Route list mode to the grid and delete drawList**

In `PokedexDashboard::draw()`, replace the `drawList(g)` call for
`kPokedexUiList` with `drawGrid(g)`.

Then **delete `drawList` entirely** — its body and its declaration in
`PokedexDashboard.h`. It is the function that renders the `PORT NOTES` panel and
the `Use search pikachu, search mega, browse 24, or open mewtwo.` hint, both of
which the spec removes. Nothing else calls it: the serial `rows` command prints
via `catalog.printRows(Serial, …)`, not through the renderer. Leaving it behind
would keep the deleted developer copy alive in the binary.

Also delete the now-unused `kListX` / `kListY` / `kListW` / `kListH` / `kSideX` /
`kSideY` / `kSideW` / `kSideH` / `kRowH` constants if the compiler reports them
unused. Keep `kHeroX` / `kHeroY` / `kHeroW` / `kHeroH` — `drawDetail` still uses
them.

- [ ] **Step 5: Update the footer for grid mode**

Replace the `mode_ == kPokedexUiList` branch of `drawFooter`:

```cpp
  if (mode_ == kPokedexUiList) {
    drawButton(g, 24, kFooterY, 96, "TOP");
    drawButton(g, 128, kFooterY, 104, "PREV");
    drawButton(g, 240, kFooterY, 104, "NEXT");
    drawButton(g, 352, kFooterY, 118, "SEARCH", kDexRedDark);
    drawButton(g, 478, kFooterY, 104,
               order_ == pokedex::kOrderName ? "A-Z" : "DEX");
    drawButton(g, 590, kFooterY, 130,
               filter_.type1 == pokedex::kTypeAny
                   ? "TYPE: ALL"
                   : pokedex::typeNameFromId(filter_.type1));
    drawButton(g, 728, kFooterY, 140,
               filter_.showShadows ? "SHADOWS ON" : "SHADOWS OFF");
    // A range, not a page number: the window is offset-based and need not be
    // page-aligned, so "Page n/m" would be meaningless after a rail jump.
    char range[32];
    if (rowCount_ == 0) {
      snprintf(range, sizeof(range), "0 of %lu", (unsigned long)totalMatching_);
    } else {
      snprintf(range, sizeof(range), "%lu-%lu of %lu",
               (unsigned long)(startOrdinal_ + 1),
               (unsigned long)(startOrdinal_ + rowCount_),
               (unsigned long)totalMatching_);
    }
    PokedexText::draw(g, 1000, kFooterY + 8, range, PokedexText::fontS(), kMuted,
                      PokedexText::kRight);
  } else if (mode_ == kPokedexUiDetail) {
```

- [ ] **Step 6: Compile**

```bash
CTAGS_WORKAROUND=1 EXTRA_FLAGS="-DUSE_DISPLAY=1 -DUSE_SD_POKEDEX=1 -DUSE_POKEDEX_SPRITES=1" ./scripts/compile-all.sh
```

Expected: clean compile.

- [ ] **Step 7: Commit**

```bash
git add projects/15-pokedex-panel/src/
git commit -m "feat: draw the Pokedex sprite grid and jump rail"
```

---

### Task 14: Grid and rail touch handling

**Files:**
- Modify: `projects/15-pokedex-panel/src/PokedexDashboard.cpp`

- [ ] **Step 1: Add the hit regions**

In `handleTouchMapped`, inside the `mode_ == kPokedexUiList` branch, replace the
three existing footer regions with:

```cpp
    // Footer buttons. Regions are deliberately taller than the drawn button so
    // a slightly low tap still lands.
    if (inside(x, y, 16, kFooterY - 20, 112, 76)) {
      event.type = kPokedexUiJumpTop;
      return true;
    }
    if (inside(x, y, 120, kFooterY - 20, 120, 76)) {
      event.type = kPokedexUiBrowsePrev;
      return true;
    }
    if (inside(x, y, 232, kFooterY - 20, 120, 76)) {
      event.type = kPokedexUiBrowseNext;
      return true;
    }
    if (inside(x, y, 344, kFooterY - 20, 134, 76)) {
      event.type = kPokedexUiOpenSearch;
      return true;
    }
    if (inside(x, y, 470, kFooterY - 20, 120, 76)) {
      event.type = kPokedexUiToggleSort;
      return true;
    }
    if (inside(x, y, 582, kFooterY - 20, 146, 76)) {
      event.type = kPokedexUiCycleType;
      return true;
    }
    if (inside(x, y, 720, kFooterY - 20, 156, 76)) {
      event.type = kPokedexUiToggleShadows;
      return true;
    }

    // Jump rail. Dimmed slots are not tappable.
    if (inside(x, y, kRailX, kRailY, kRailW, kRailH)) {
      const uint8_t slots = (order_ == pokedex::kOrderName) ? kRailSlots : kDexBuckets;
      const int16_t slotH = kRailH / slots;
      const int16_t hit = (y - kRailY) / slotH;
      if (hit >= 0 && hit < (int16_t)slots && railEnabled_[hit]) {
        if (order_ == pokedex::kOrderName) {
          event.type = kPokedexUiJumpLetter;
          event.letter = (char)('A' + hit);
        } else {
          event.type = kPokedexUiJumpDex;
          event.dex = (uint16_t)(hit * 100);
        }
        return true;
      }
      return false;
    }

    // Grid tiles. The whole ~154x140 cell is the target, not just the sprite.
    if (inside(x, y, kGridX, kGridY, kGridW, kGridH)) {
      const int16_t col = (x - kGridX) / kGridCellW;
      const int16_t rowIndex = (y - kGridY) / kGridCellH;
      if (col >= 0 && col < kGridCols && rowIndex >= 0 && rowIndex < kGridRows) {
        const uint8_t slot = (uint8_t)(rowIndex * kGridCols + col);
        if (slot < rowCount_) {
          event.type = kPokedexUiSelectRow;
          event.row = slot;
          return true;
        }
      }
      return false;
    }
```

- [ ] **Step 2: Compile**

```bash
CTAGS_WORKAROUND=1 EXTRA_FLAGS="-DUSE_DISPLAY=1 -DUSE_SD_POKEDEX=1 -DUSE_POKEDEX_SPRITES=1" ./scripts/compile-all.sh
```

Expected: clean compile.

- [ ] **Step 3: Commit**

```bash
git add projects/15-pokedex-panel/src/PokedexDashboard.cpp
git commit -m "feat: handle Pokedex grid and rail touch targets"
```

---

### Task 15: Wire grid state into the sketch

**Files:**
- Modify: `projects/15-pokedex-panel/15-pokedex-panel.ino`

Every function must be **defined before use** — `CTAGS_WORKAROUND=1` disables
prototype generation. Insert each function above its first caller.

- [ ] **Step 1: Add the state globals**

Browse tracks an **ordinal**, not a page number — see Task 7/12's offset-based
`PokedexCatalog::window`. Near the existing `uint16_t browseStart = 0;`, add:

```cpp
PokedexSprites sprites;
pokedex::Order browseOrder = pokedex::kOrderDex;
pokedex::Filter browseFilter;
uint32_t browseOrdinal = 0;
bool railEnabled[kRailSlotsMax] = {false};
```

Add the include beside the other project includes:

```cpp
#include "src/PokedexSprites.h"
```

- [ ] **Step 2: Add the rail-state helper and the window loader**

Insert above the first function that calls it:

```cpp
// Recomputes which rail slots have rows under the active order and filter, so
// the UI can dim the ones that would jump nowhere.
void refreshRailState() {
  for (uint8_t i = 0; i < kRailSlotsMax; i++) railEnabled[i] = false;
  if (browseOrder == pokedex::kOrderName) {
    for (uint8_t i = 0; i < 26; i++) {
      railEnabled[i] = catalog.hasLetter((char)('A' + i), browseFilter);
    }
  } else {
    for (uint8_t i = 0; i < 11; i++) {
      railEnabled[i] = catalog.hasDexAtLeast((uint16_t)(i * 100), browseFilter);
    }
  }
}

// Loads the kGridPageSize-row window starting at `ordinal` in the current order
// and filter, clamped so it never runs off either end.
void loadGridWindow(uint32_t ordinal) {
  browseOrdinal = catalog.clampOrdinal(ordinal, browseFilter);
  rowCount = catalog.window(browseOrdinal, browseOrder, browseFilter, rows,
                            POKEDEX_MAX_RESULTS);
  selectedRow = 0;
  refreshRailState();
  dashboard.showGrid(rows, rowCount, selectedRow, browseOrdinal,
                     catalog.countMatching(browseFilter), browseOrder, browseFilter,
                     railEnabled, catalog.sourceLabel(), catalog.status());
}
```

- [ ] **Step 3: Handle the new UI events**

In the UI event switch in `loop()`, add:

```cpp
      case kPokedexUiJumpTop:
        loadGridWindow(0);
        break;
      case kPokedexUiJumpLetter:
        loadGridWindow(catalog.ordinalOfLetter(event.letter, browseFilter));
        break;
      case kPokedexUiJumpDex:
        loadGridWindow(catalog.ordinalOfDex(event.dex, browseFilter));
        break;
      case kPokedexUiToggleSort:
        // A-Z is only meaningful when the name-order buffer was allocated.
        if (browseOrder == pokedex::kOrderDex && catalog.nameOrderActive()) {
          browseOrder = pokedex::kOrderName;
        } else {
          browseOrder = pokedex::kOrderDex;
        }
        loadGridWindow(0);
        break;
      case kPokedexUiToggleShadows:
        browseFilter.showShadows = !browseFilter.showShadows;
        loadGridWindow(0);
        break;
      case kPokedexUiCycleType:
        if (browseFilter.type1 == pokedex::kTypeAny) {
          browseFilter.type1 = 0;
        } else if (browseFilter.type1 + 1 >= pokedex::kTypeCount) {
          browseFilter.type1 = pokedex::kTypeAny;
        } else {
          browseFilter.type1++;
        }
        loadGridWindow(0);
        break;
      case kPokedexUiBrowsePrev:
        // browseOrdinal is unsigned: guard against underflow rather than
        // subtracting past 0.
        loadGridWindow(browseOrdinal < pokedex::kGridPageSize
                           ? 0
                           : browseOrdinal - pokedex::kGridPageSize);
        break;
      case kPokedexUiBrowseNext:
        loadGridWindow(browseOrdinal + pokedex::kGridPageSize);
        break;
```

Remove any pre-existing `kPokedexUiBrowsePrev` / `kPokedexUiBrowseNext` cases so
they are not handled twice.

- [ ] **Step 4: Attach sprites and load the first window in setup()**

In `setup()`, after `dashboard.begin();` and after `catalog.begin();`, add:

```cpp
  sprites.begin();
  dashboard.attachSprites(&sprites);
  loadGridWindow(0);
```

- [ ] **Step 5: Add the serial commands**

Add these command handlers above `setup()`, then register them:

```cpp
void cmdGrid(const String &args) {
  const uint32_t ordinal = args.length() ? (uint32_t)args.toInt() : browseOrdinal;
  loadGridWindow(ordinal);
  Serial.print(F("rows "));
  Serial.print(browseOrdinal + 1);
  Serial.print('-');
  Serial.print(browseOrdinal + rowCount);
  Serial.print(F(" of "));
  Serial.println(catalog.countMatching(browseFilter));
  catalog.printRows(Serial, rows, rowCount);
}

void cmdLetter(const String &args) {
  if (args.length() == 0) {
    Serial.println(F("usage: letter <a-z>"));
    return;
  }
  if (!catalog.nameOrderActive()) {
    Serial.println(F("name order unavailable; dex order only"));
    return;
  }
  browseOrder = pokedex::kOrderName;
  loadGridWindow(catalog.ordinalOfLetter(args[0], browseFilter));
  catalog.printRows(Serial, rows, rowCount);
}

void cmdSort(const String &args) {
  if (args == "name") {
    if (!catalog.nameOrderActive()) {
      Serial.println(F("name order unavailable; staying in dex order"));
      return;
    }
    browseOrder = pokedex::kOrderName;
  } else if (args == "dex") {
    browseOrder = pokedex::kOrderDex;
  } else {
    Serial.println(F("usage: sort [dex|name]"));
    return;
  }
  loadGridWindow(0);
  Serial.print(F("order="));
  Serial.println(browseOrder == pokedex::kOrderName ? F("name") : F("dex"));
}

void cmdFilter(const String &args) {
  if (args.length() == 0 || args == "none") {
    browseFilter.type1 = pokedex::kTypeAny;
  } else {
    char wanted[16];
    snprintf(wanted, sizeof(wanted), "%s", args.c_str());
    if (wanted[0] >= 'a' && wanted[0] <= 'z') wanted[0] = (char)(wanted[0] - 'a' + 'A');
    const uint8_t id = pokedex::typeIdFromName(wanted);
    if (id == pokedex::kTypeAny) {
      Serial.println(F("unknown type"));
      return;
    }
    browseFilter.type1 = id;
  }
  loadGridWindow(0);
  Serial.print(F("matching="));
  Serial.println(catalog.countMatching(browseFilter));
}

void cmdShadows(const String &args) {
  if (args == "on") {
    browseFilter.showShadows = true;
  } else if (args == "off") {
    browseFilter.showShadows = false;
  } else {
    browseFilter.showShadows = !browseFilter.showShadows;
  }
  loadGridWindow(0);
  Serial.print(F("shadows="));
  Serial.println(browseFilter.showShadows ? F("on") : F("off"));
}

void cmdSprite(const String &args) {
  if (args.length() == 0) {
    sprites.printDiagnostics(Serial);
    return;
  }
  uint16_t key = 0;
  const uint16_t *pixels = sprites.tile(args.c_str(), &key);
  Serial.print(args);
  Serial.print(pixels != nullptr ? F(" decoded in ") : F(" failed after "));
  Serial.print(sprites.lastDecodeMicros());
  Serial.print(F(" us key=0x"));
  Serial.println(key, HEX);
}
```

Register them in `setup()` beside the existing `router.on` calls:

```cpp
  router.on("grid", "grid [ordinal] - show a grid window", cmdGrid);
  router.on("letter", "letter <a-z> - jump in A-Z order", cmdLetter);
  router.on("sort", "sort [dex|name] - set browse order", cmdSort);
  router.on("filter", "filter [type|none] - set the type filter", cmdFilter);
  router.on("shadows", "shadows [on|off] - toggle shadow forms", cmdShadows);
  router.on("sprite", "sprite [entry_id] - decode report", cmdSprite);
```

Keep every help string short enough that the router's 95-character line limit is
not hit.

- [ ] **Step 6: Extend cmdStatus**

In `cmdStatus`, add:

```cpp
  Serial.print(F("index rows="));
  Serial.print(catalog.indexRowCount());
  Serial.print(F(" nameOrder="));
  Serial.print(catalog.nameOrderActive() ? F("yes") : F("no"));
  Serial.print(F(" order="));
  Serial.print(browseOrder == pokedex::kOrderName ? F("name") : F("dex"));
  Serial.print(F(" matching="));
  Serial.println(catalog.countMatching(browseFilter));
  sprites.printDiagnostics(Serial);
```

Audio state is added to `status` in Task 19, once `PokedexAudio` exists. Keep
each line under the router's 95-character limit.

- [ ] **Step 7: Compile**

```bash
CTAGS_WORKAROUND=1 EXTRA_FLAGS="-DUSE_DISPLAY=1 -DUSE_SD_POKEDEX=1 -DUSE_POKEDEX_SPRITES=1" ./scripts/compile-all.sh
```

Expected: clean compile. If you see `'cmdGrid' was not declared in this scope`,
a function is defined below its caller — move it up.

- [ ] **Step 8: Commit**

```bash
git add projects/15-pokedex-panel/15-pokedex-panel.ino
git commit -m "feat: wire Pokedex grid state and serial commands"
```

---

### Task 16: Detail tabs

**Files:**
- Modify: `projects/15-pokedex-panel/src/PokedexDashboard.cpp`

- [ ] **Step 1: Replace the detail footer with tabs**

In `drawFooter`, replace the `mode_ == kPokedexUiDetail` branch:

```cpp
  } else if (mode_ == kPokedexUiDetail) {
    drawButton(g, 24, kFooterY, 112, "LIST");
    static const char *const kTabs[POKEDEX_DETAIL_PAGE_COUNT] = {
        "ENTRY", "STATS", "MOVES", "MATCHUPS", "EVO"};
    for (uint8_t i = 0; i < POKEDEX_DETAIL_PAGE_COUNT; i++) {
      const int16_t x = 168 + i * 168;
      drawButton(g, x, kFooterY, 158, kTabs[i],
                 i == detailPage_ ? kDexRedDark : kCardHi);
    }
  }
```

- [ ] **Step 2: Replace the detail touch regions**

In `handleTouchMapped`, replace the `mode_ == kPokedexUiDetail` branch:

```cpp
    if (inside(x, y, 16, kFooterY - 20, 128, 76)) {
      event.type = kPokedexUiBackToList;
      return true;
    }
    for (uint8_t i = 0; i < POKEDEX_DETAIL_PAGE_COUNT; i++) {
      const int16_t x0 = 160 + i * 168;
      if (inside(x, y, x0, kFooterY - 20, 174, 76)) {
        event.type = kPokedexUiSelectTab;
        event.tab = i;
        return true;
      }
    }
```

- [ ] **Step 3: Handle kPokedexUiSelectTab in the sketch**

In `15-pokedex-panel.ino`, add to the UI event switch:

```cpp
      case kPokedexUiSelectTab:
        detailPage = event.tab % POKEDEX_DETAIL_PAGE_COUNT;
        dashboard.showDetail(detail, detailPage, catalog.sourceLabel(), catalog.status());
        break;
```

- [ ] **Step 4: Use the hero sprite on the detail card**

In `drawDetail`, replace the `drawPokeball(...)` call with:

```cpp
  // Same colour-keying rationale as drawTile in the grid (Task 13): the well
  // must be literal 0x0000, not any other "black-ish" colour, because a
  // black-background sprite's interior outline pixels are also 0x0000 and are
  // keyed identically to the background.
  uint16_t heroKey = 0;
  const uint16_t *heroPixels =
      (sprites_ != nullptr) ? sprites_->hero(detail_.entryId, &heroKey) : nullptr;
  if (heroPixels != nullptr) {
    const int16_t size = sprites_->heroSize();
    const int16_t hx = kHeroX + (kHeroW - size) / 2;
    const int16_t hy = kHeroY + 40;
    g->fillRect(hx, hy, size, size, 0x0000);
    g->draw16bitRGBBitmapWithTranColor(hx, hy, (uint16_t *)heroPixels, heroKey,
                                       size, size);
  } else {
    drawPokeball(g, kHeroX + kHeroW / 2, kHeroY + 190, 112, kDexRed);
  }
```

- [ ] **Step 5: Compile**

```bash
CTAGS_WORKAROUND=1 EXTRA_FLAGS="-DUSE_DISPLAY=1 -DUSE_SD_POKEDEX=1 -DUSE_POKEDEX_SPRITES=1" ./scripts/compile-all.sh
```

Expected: clean compile.

- [ ] **Step 6: Commit**

```bash
git add projects/15-pokedex-panel/
git commit -m "feat: replace Pokedex detail paging with named tabs"
```

---

### Task 17: Hardware checkpoint — stages 2 and 3

- [ ] **Step 1: Flash**

```bash
CTAGS_WORKAROUND=1 EXTRA_FLAGS="-DUSE_DISPLAY=1 -DUSE_SD_POKEDEX=1 -DUSE_POKEDEX_SPRITES=1" ./scripts/upload-project.sh projects/15-pokedex-panel /dev/cu.usbmodem1101
```

- [ ] **Step 2: Verify on the panel**

Confirm each of these and record what you actually see:

- Grid shows 18 real sprites, not pokeballs.
- Tapping a tile opens the detail card with the 320px hero sprite.
- Detail tabs switch pages in one tap.
- `SORT` flips the rail between A-Z and dex hundreds.
- `SHADOWS OFF` reduces the header count to 1114.
- `TYPE` cycles and the count follows.
- Dimmed rail slots do not respond.
- `↑ TOP` returns to page 1 from deep in the list.

- [ ] **Step 3: Measure**

Run `sprite pikachu` and `status`. Record the decode time and cache hit/miss
counts. Time a page turn by eye and note it.

- [ ] **Step 4: Report honestly**

If touch targets miss, run `touch` and use the reported raw/mapped coordinates to
fix the regions — do not leave `POKEDEX_TOUCH_AUTO_REMAP` masking a wrong
mapping. Update `docs/full-port-proof-matrix.md` only to the stage actually
observed.

---

## Stage 4 — Audio

### Task 18: Convert the BGM asset

**Files:**
- Creates a converted WAV for the SD card (not committed — it is an SD asset)

- [ ] **Step 1: Convert**

The source is 11025 Hz mono unsigned 8-bit; the panel's I2S path is 16-bit.
Use the repo's audio conversion skill:

```
Use the convert-crowpanel-audio skill on ~/Documents/GitHub/esp32-pokedex/sd/pokemon/audio/bgm/pokedex_loop.wav
```

- [ ] **Step 2: Verify the output format**

```bash
file <converted>.wav
```

Expected: `RIFF (little-endian) data, WAVE audio, Microsoft PCM, 16 bit, mono 16000 Hz`.

- [ ] **Step 3: Copy to the card**

Place it at `/pokemon/audio/bgm/pokedex_loop.wav` on the SD card, replacing the
8-bit original. Do not commit WAV assets to the repo.

- [ ] **Step 4: Confirm the credits file still applies**

`audio/credits.txt` states the loop is a project-local original chiptune, safe to
redistribute. Leave it in place. Do not add ripped OST audio.

---

### Task 19: PokedexAudio

**Files:**
- Modify: `projects/15-pokedex-panel/config/ProjectConfig.h`
- Create: `projects/15-pokedex-panel/src/PokedexAudio.h`
- Create: `projects/15-pokedex-panel/src/PokedexAudio.cpp`
- Modify: `projects/15-pokedex-panel/15-pokedex-panel.ino`

- [ ] **Step 1: Add the config**

In `ProjectConfig.h`:

```cpp
#ifndef USE_POKEDEX_AUDIO
#define USE_POKEDEX_AUDIO 1
#endif

#ifndef POKEDEX_BGM_PATH
#define POKEDEX_BGM_PATH "/pokemon/audio/bgm/pokedex_loop.wav"
#endif

#ifndef POKEDEX_AUDIO_SAMPLE_RATE
#define POKEDEX_AUDIO_SAMPLE_RATE 16000
#endif
```

- [ ] **Step 2: Read the reference implementation first**

Read `projects/20-pipboy-terminal/src/PipBoyMedia.cpp` before writing this. It
is the hardware-verified I2S + SD WAV path on this panel. Copy its structure:
`i2s_std` channel setup, `I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG` at 16-bit, the
WAV header parse, and critically its **amp sequence**.

- [ ] **Step 3: Write the header**

Create `projects/15-pokedex-panel/src/PokedexAudio.h`:

```cpp
#ifndef POKEDEX_AUDIO_H
#define POKEDEX_AUDIO_H

#include "../config/ProjectConfig.h"
#include <Arduino.h>

// BGM loop plus UI blips for the Pokedex panel. Mirrors the hardware-verified
// I2S path in projects/20-pipboy-terminal/src/PipBoyMedia.cpp.
//
// The amp enable (IO30) is ACTIVE-LOW on this panel. Driving it HIGH mutes the
// speaker while I2S keeps streaming, which looks exactly like working audio.
// Always resolve polarity from HardwareProfile, never hardcode it.
class PokedexAudio {
 public:
  void begin();
  void tick();  // Feeds the I2S ring; call every loop().

  void startBgm();
  void stopBgm();
  bool bgmPlaying() const { return bgmPlaying_; }

  enum Sfx : uint8_t { kSfxSelect, kSfxBack, kSfxPage, kSfxError };
  void play(Sfx sfx);

  bool ready() const { return ready_; }
  const String &status() const { return status_; }

 private:
  bool ready_ = false;
  bool bgmPlaying_ = false;
  String status_ = "audio off";

  bool openBgm();
  void closeBgm();
  void setAmpEnabled(bool enabled);
};

#endif
```

- [ ] **Step 4: Implement, following PipBoyMedia**

Create `projects/15-pokedex-panel/src/PokedexAudio.cpp`. The amp control must be
exactly this shape — copy the polarity handling verbatim from `PipBoyMedia.cpp`:

```cpp
void PokedexAudio::setAmpEnabled(bool enabled) {
  const HardwareProfile &profile = activeHardwareProfile();
  const auto &audio = profile.audio;
  pinMode(audio.control, OUTPUT);
  // IO30 is ACTIVE-LOW on this panel: controlActiveHigh is false, so "enabled"
  // means driving the pin LOW. Never hardcode HIGH/LOW here.
  const bool level = enabled ? audio.controlActiveHigh : !audio.controlActiveHigh;
  digitalWrite(audio.control, level ? HIGH : LOW);
}
```

Guard the ESP-IDF header the same way project 20 does — `driver/i2s_std.h` is an
IDF core header, not an Arduino library, so `__has_include` is legitimate **here**
(the ban applies to feature-flagged *library* includes like `SD_MMC.h`):

```cpp
#if USE_POKEDEX_AUDIO && __has_include(<driver/i2s_std.h>)
#include <driver/i2s_std.h>
#define POKEDEX_HAS_I2S 1
#else
#define POKEDEX_HAS_I2S 0
#endif
```

In `begin()`, park the amp off **before** touching I2S, configure the channel,
push silence, then enable the amp. This ordering is what avoids the turn-on pop.
This is the verified sequence from `PipBoyMedia::begin()` — the NS4168 needs no
codec or I2C init, and no MCLK:

```cpp
#if POKEDEX_HAS_I2S
  const AudioPins &audio = activeHardwareProfile().audio;
  setAmpEnabled(false);  // Park the amp before the bus exists.

  i2s_chan_config_t chanCfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
  chanCfg.dma_desc_num = kDmaDesc;
  chanCfg.dma_frame_num = kBlockFrames;
  chanCfg.auto_clear = true;  // Emit silence on underrun, not stale audio.
  i2s_chan_handle_t tx = nullptr;
  if (i2s_new_channel(&chanCfg, &tx, nullptr) == ESP_OK) {
    i2s_std_config_t stdCfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG((uint32_t)POKEDEX_AUDIO_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                        I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,  // NS4168 needs no MCLK.
            .bclk = (gpio_num_t)audio.bclk,
            .ws = (gpio_num_t)audio.lrclk,
            .dout = (gpio_num_t)audio.sdata,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {false, false, false},
        },
    };
    if (i2s_channel_init_std_mode(tx, &stdCfg) == ESP_OK &&
        i2s_channel_enable(tx) == ESP_OK) {
      int16_t silence[kBlockFrames * 2] = {};
      for (int i = 0; i < 4; ++i) {
        size_t written = 0;
        i2s_channel_write(tx, silence, sizeof(silence), &written, pdMS_TO_TICKS(50));
      }
      setAmpEnabled(true);  // Only now, onto a clean bus.
      txChan_ = tx;
      ready_ = true;
    } else {
      i2s_del_channel(tx);
    }
  }
  status_ = ready_ ? "speaker ready" : "speaker init failed; silent";
#endif
```

Declare the matching members and constants:

```cpp
namespace {
constexpr uint32_t kDmaDesc = 6;
constexpr uint32_t kBlockFrames = 256;
}  // namespace
```

and in the header's private section, gated the same way:

```cpp
#if POKEDEX_HAS_I2S
  i2s_chan_handle_t txChan_ = nullptr;
#endif
```

Because the handle type only exists under the guard, declare it in the `.cpp` as
a file-scope static instead if the header would need the IDF include — project 20
keeps `gWav` at file scope in the `.cpp` for exactly this reason.

The mono 16 kHz source must be duplicated to stereo frames on write, since the
slot config is `I2S_SLOT_MODE_STEREO`: for each sample, write it twice.

- [ ] **Step 5: Wire into the sketch**

Add `#include "src/PokedexAudio.h"`, a `PokedexAudio audio;` global,
`audio.begin();` in `setup()`, `audio.tick();` in `loop()`, and
`audio.play(PokedexAudio::kSfxSelect)` on `kPokedexUiSelectRow`,
`kSfxBack` on `kPokedexUiBackToList`, `kSfxPage` on the page and jump events.

Add the serial command, defined above its registration:

```cpp
void cmdAudio(const String &args) {
  if (args == "on") {
    audio.startBgm();
  } else if (args == "off") {
    audio.stopBgm();
  }
  Serial.print(F("audio="));
  Serial.print(audio.bgmPlaying() ? F("playing") : F("stopped"));
  Serial.print(' ');
  Serial.println(audio.status());
}
```

```cpp
  router.on("audio", "audio [on|off] - BGM control", cmdAudio);
```

Add audio state to `cmdStatus`, completing the `status` line-up from Task 15:

```cpp
  Serial.print(F("audio="));
  Serial.print(audio.bgmPlaying() ? F("playing") : F("stopped"));
  Serial.print(' ');
  Serial.println(audio.status());
```

- [ ] **Step 6: Compile with audio only, then with everything**

```bash
CTAGS_WORKAROUND=1 EXTRA_FLAGS="-DUSE_SD_POKEDEX=1 -DUSE_POKEDEX_AUDIO=1" ./scripts/compile-all.sh
CTAGS_WORKAROUND=1 EXTRA_FLAGS="-DUSE_DISPLAY=1 -DUSE_SD_POKEDEX=1 -DUSE_POKEDEX_SPRITES=1 -DUSE_POKEDEX_AUDIO=1" ./scripts/compile-all.sh
```

Expected: both clean. The audio-only build proves audio does not depend on the
display or sprites.

- [ ] **Step 7: Commit**

```bash
git add projects/15-pokedex-panel/
git commit -m "feat: add Pokedex BGM and UI sound effects"
```

---

### Task 20: Flag matrix rows

**Files:**
- Modify: `scripts/check-flag-matrix.sh`

- [ ] **Step 1: Add the rows**

After the existing `$P15` rows (near line 137), add:

```bash
  "$P15|pokedex-sprites|-DUSE_DISPLAY=1 -DUSE_SD_POKEDEX=1 -DUSE_POKEDEX_SPRITES=1|ArduinoJson,GFX Library for Arduino,SensorLib,U8g2"
  "$P15|pokedex-audio|-DUSE_SD_POKEDEX=1 -DUSE_POKEDEX_AUDIO=1|ArduinoJson"
  "$P15|pokedex-expo-full|-DUSE_DISPLAY=1 -DUSE_SD_POKEDEX=1 -DUSE_POKEDEX_SPRITES=1 -DUSE_POKEDEX_AUDIO=1|ArduinoJson,GFX Library for Arduino,SensorLib,U8g2"
```

- [ ] **Step 2: Run the full matrix**

```bash
CTAGS_WORKAROUND=1 ./scripts/check-flag-matrix.sh
```

Expected: every row green, including the pre-existing `sd-pokedex` and
`display-sd-pokedex` rows, which prove the new flags default off cleanly.

- [ ] **Step 3: Run host tests**

```bash
./scripts/test-pokedex.sh
```

Expected: `0 failure(s)`.

- [ ] **Step 4: Commit**

```bash
git add scripts/check-flag-matrix.sh
git commit -m "test: add Pokedex sprite and audio flag matrix rows"
```

---

### Task 21: Documentation

**Files:**
- Modify: `projects/15-pokedex-panel/README.md`
- Modify: `projects/15-pokedex-panel/TECHNICAL.md`
- Modify: `README.md`
- Modify: `AGENTS.md` (only if build commands or flags changed for other projects)

- [ ] **Step 1: Update the project README**

Replace the demo walkthrough. The old one narrates a row list and tells the
reader to type `search pikachu`; both are gone. Cover, in this order:

1. Boot state: grid of 18 sprites, dex order, shadows hidden, 1114 of 1573.
2. Tap a tile → detail card with the large sprite; tap a tab name to jump.
3. `SORT` → rail becomes A–Z; tap a letter to jump.
4. `TYPE` → cycles the 18 types; the header count follows.
5. `SHADOWS` → 1114 becomes 1573.
6. `↑ TOP` → back to page 1 from anywhere.

Then a serial-command table with the new commands and one example each:

| Command | Example | Effect |
|---|---|---|
| `grid [page]` | `grid 12` | jump to a grid page and print it |
| `letter <a-z>` | `letter p` | switch to A–Z order and jump |
| `sort [dex\|name]` | `sort name` | set browse order |
| `filter [type\|none]` | `filter grass` | set the type filter |
| `shadows [on\|off]` | `shadows on` | show or hide shadow forms |
| `sprite [entry_id]` | `sprite pikachu` | decode report and timing |
| `audio [on\|off]` | `audio on` | BGM control |

State the SD card requirement plainly: without `/pokemon/sprites/` the grid
still works but every tile is a drawn pokeball.

- [ ] **Step 2: Update TECHNICAL.md**

Document the SD layout the project now depends on:

```
/pokemon/index.csv
/pokemon/catalog_meta.json
/pokemon/data/*.json
/pokemon/sprites/*.bmp              40x40 24-bit Windows 3.x BMP
/pokemon/audio/bgm/pokedex_loop.wav 16-bit 16 kHz mono
```

Record the index memory cost (1573 x 40 B plus a 3 KB order array), the sprite
cache budget, and the measured decode and page-turn timings from Task 17.

- [ ] **Step 3: Update the root README flag table**

Add `USE_POKEDEX_SPRITES` and `USE_POKEDEX_AUDIO` to project 15's row.

- [ ] **Step 4: Update the proof matrix**

In `docs/full-port-proof-matrix.md`, set project 15's stage to exactly what the
Task 17 and Task 19 hardware checks showed. **Never upgrade a row past the
evidence in the session log.** If sprites were seen working but audio was not
tested, say so.

- [ ] **Step 5: Commit**

```bash
git add projects/15-pokedex-panel/README.md projects/15-pokedex-panel/TECHNICAL.md README.md docs/full-port-proof-matrix.md
git commit -m "docs: document the Pokedex expo pass"
```

---

## Verification checklist

Before claiming the work complete, run all of these and paste the real output:

```bash
./scripts/test-pokedex.sh
```

```bash
CTAGS_WORKAROUND=1 ./scripts/check-flag-matrix.sh
```

```bash
grep -rn "__has_include" projects/15-pokedex-panel/src/
```

Expected: no matches for the last one.

Confirm `_arduino-build/*/libraries/` contains `SD_MMC` and `GFX Library for
Arduino` for the sprite build — a green compile does not prove linkage.

Do not claim any stage is hardware-verified without the exact FQBN, the upload,
and the observed runtime behaviour recorded.
