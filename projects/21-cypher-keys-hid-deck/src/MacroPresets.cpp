#include "MacroPresets.h"

#include <Preferences.h>

#include "../config/Macros.h"
#include "KeysLayout.h"  // tab bar + macro grid geometry (draw and hit-test)

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
#include <CrowPanelShared.h>
#include <Arduino_GFX_Library.h>
#endif

void MacroPresets::begin() {
  count_ = kCypherKeysPresetCount;
  Preferences prefs;
  if (prefs.begin(CYPHER_KEYS_NVS_NAMESPACE, true)) {
    uint32_t stored = prefs.getUInt(CYPHER_KEYS_NVS_PRESET_KEY, 0);
    prefs.end();
    if (stored < count_) active_ = (uint8_t)stored;
  }
}

uint8_t MacroPresets::presetCount() const { return count_; }

const MacroPreset &MacroPresets::preset(uint8_t index) const {
  if (index >= count_) index = 0;
  return kCypherKeysPresets[index];
}

const MacroPreset &MacroPresets::active() const { return preset(active_); }

const char *MacroPresets::activeName() const { return active().name; }

const MacroSlot &MacroPresets::slot(uint8_t index) const {
  if (index >= CYPHER_KEYS_MACRO_SLOTS) index = 0;
  return active().slots[index];
}

void MacroPresets::persist() const {
  Preferences prefs;
  if (prefs.begin(CYPHER_KEYS_NVS_NAMESPACE, false)) {
    prefs.putUInt(CYPHER_KEYS_NVS_PRESET_KEY, active_);
    prefs.end();
  }
}

void MacroPresets::setActive(uint8_t index) {
  if (count_ == 0) return;
  if (index >= count_) index = count_ - 1;
  if (index == active_) return;
  active_ = index;
  persist();
}

void MacroPresets::next() {
  if (count_ == 0) return;
  active_ = (active_ + 1) % count_;
  persist();
}

bool MacroPresets::selectByName(const String &name) {
  String want = name;
  want.trim();
  want.toLowerCase();
  if (want.length() == 0) return false;
  for (uint8_t i = 0; i < count_; ++i) {
    String have = kCypherKeysPresets[i].name;
    have.toLowerCase();
    if (have.startsWith(want)) {
      setActive(i);
      return true;
    }
  }
  return false;
}

int8_t MacroPresets::hitTab(int16_t x, int16_t y) const {
  return KeysLayout::hitTab(x, y, count_);
}

int8_t MacroPresets::hitSlot(int16_t x, int16_t y) const {
  return KeysLayout::hitSlot(x, y);
}

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
namespace {
uint16_t slotAccent(const MacroSlot &s, const DeckTheme &theme) {
  switch (s.kind) {
    case kMacroCombo: return theme.accent2;
    case kMacroConsumer: return theme.warn;
    case kMacroText: return theme.good;
    case kMacroApp: return theme.accent;
    default: return theme.line;
  }
}
}  // namespace

void MacroPresets::draw(Arduino_GFX *g, const DeckTheme &theme) const {
  if (g == nullptr) return;

  // Tabs.
  for (uint8_t i = 0; i < count_; ++i) {
    int16_t x, y, w, h;
    KeysLayout::tabBounds(i, count_, x, y, w, h);
    bool on = (i == active_);
    Widgets::panel(g, x, y, w, h, 9, on ? theme.accent : theme.surface, 1,
                   on ? theme.accent : theme.line);
    Widgets::text(g, x + w / 2, y + 12, kCypherKeysPresets[i].name,
                  Widgets::fontS(), on ? theme.onAccent : theme.muted,
                  Widgets::kCenter);
  }

  // Macro grid.
  for (uint8_t i = 0; i < CYPHER_KEYS_MACRO_SLOTS; ++i) {
    const MacroSlot &s = active().slots[i];
    int16_t x, y, w, h;
    KeysLayout::slotBounds(i, x, y, w, h);
    bool empty = (s.kind == kMacroNone);
    uint16_t fill = empty ? theme.surface : theme.surfaceHi;
    uint16_t accent = slotAccent(s, theme);
    Widgets::panel(g, x, y, w, h, 12, fill, empty ? 1 : 2,
                   empty ? theme.line : accent);
    if (empty) continue;
    // Accent tab on the left edge to signal the slot kind.
    g->fillRoundRect(x + 6, y + 8, 5, h - 16, 2, accent);
    Widgets::text(g, x + w / 2, y + h / 2 - 6, s.label, Widgets::fontM(),
                  theme.ink, Widgets::kCenter);
  }
}
#endif
