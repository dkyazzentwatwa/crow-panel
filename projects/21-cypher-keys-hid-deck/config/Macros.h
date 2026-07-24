#ifndef CYPHER_KEYS_HID_DECK_MACROS_H
#define CYPHER_KEYS_HID_DECK_MACROS_H

#include "../src/HidTypes.h"

// ===========================================================================
// EDIT ME. This is the macro pad. Each preset is a page of up to
// CYPHER_KEYS_MACRO_SLOTS (12 = 3 rows x 4 cols) tiles. Change labels, add
// presets, or point slots at different shortcuts and text snippets. The panel
// shows one tab per preset and remembers the last one you used.
//
// Slot helpers (from src/HidTypes.h):
//   MACRO_EMPTY                         - blank tile
//   MACRO_COMBO("Copy", kModCmd, 'c')   - modifiers + one key (ASCII or kKey*)
//   MACRO_MEDIA("Vol +", kCcVolumeUp)   - a consumer-control usage
//   MACRO_TEXT("Fix", "Fix the bug: ")  - types a canned string
//
// Modifiers: kModCmd (Command), kModShift, kModOpt (Option), kModCtrl.
// Special keys: kKeyReturn, kKeyEsc, kKeyTab, kKeyUpArrow, kKeyLeftArrow, ...
// Media usages: kCcVolumeUp/Down, kCcMute, kCcPlayPause, kCcBrightnessUp/Down.
// ===========================================================================

static const MacroPreset kCypherKeysPresets[] = {
    // -------- Preset 1: macOS essentials --------
    {"Mac",
     {
         MACRO_COMBO("Copy", kModCmd, 'c'),
         MACRO_COMBO("Cut", kModCmd, 'x'),
         MACRO_COMBO("Paste", kModCmd, 'v'),
         MACRO_COMBO("Undo", kModCmd, 'z'),

         MACRO_COMBO("Redo", kModCmd | kModShift, 'z'),
         MACRO_COMBO("App Switch", kModCmd, kKeyTab),
         MACRO_COMBO("Spotlight", kModCmd, ' '),
         MACRO_COMBO("Mission", kModCtrl, kKeyUpArrow),

         MACRO_COMBO("Screenshot", kModCmd | kModShift, '4'),
         MACRO_MEDIA("Vol -", kCcVolumeDown),
         MACRO_MEDIA("Vol +", kCcVolumeUp),
         MACRO_MEDIA("Mute", kCcMute),
     }},

    // -------- Preset 2: ChatGPT / Codex micro keypad --------
    // A mix of app shortcuts and canned prompt/command snippets. Codex CLI
    // reads the text ones as typed input; edit them to taste.
    {"ChatGPT / Codex",
     {
         MACRO_COMBO("New Chat", kModCmd, 'n'),
         MACRO_COMBO("Summon", kModOpt, ' '),   // ChatGPT Mac app global hotkey
         MACRO_COMBO("Copy", kModCmd, 'c'),
         MACRO_COMBO("Interrupt", kModNone, kKeyEsc),

         MACRO_TEXT("/model", "/model "),
         MACRO_TEXT("/approvals", "/approvals "),
         MACRO_TEXT("/new", "/new "),
         MACRO_TEXT("/diff", "/diff "),

         MACRO_TEXT("Explain", "Explain what this code does: "),
         MACRO_TEXT("Fix bug", "Find and fix the bug in this: "),
         MACRO_TEXT("Add tests", "Write tests for this: "),
         MACRO_TEXT("Commit msg", "Write a commit message for these changes: "),
     }},

    // -------- Preset 3: Media & display --------
    {"Media",
     {
         MACRO_MEDIA("Play/Pause", kCcPlayPause),
         MACRO_MEDIA("Vol +", kCcVolumeUp),
         MACRO_MEDIA("Vol -", kCcVolumeDown),
         MACRO_MEDIA("Mute", kCcMute),

         MACRO_MEDIA("Bright +", kCcBrightnessUp),
         MACRO_MEDIA("Bright -", kCcBrightnessDown),
         MACRO_EMPTY,
         MACRO_EMPTY,

         MACRO_EMPTY,
         MACRO_EMPTY,
         MACRO_EMPTY,
         MACRO_EMPTY,
     }},
};

static const uint8_t kCypherKeysPresetCount =
    sizeof(kCypherKeysPresets) / sizeof(kCypherKeysPresets[0]);

#endif
