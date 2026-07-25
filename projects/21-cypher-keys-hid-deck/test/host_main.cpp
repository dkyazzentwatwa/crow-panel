// Host-side tests for the Cypher Keys touch model: the HidKeyboard chord /
// sticky / repeat state machine and the KeysTouch multi-contact tracker.
//
// Lives outside src/ on purpose: arduino-cli only compiles the sketch root and
// src/, so nothing in this folder ever reaches the firmware. The two units
// under test are the exact translation units that ship - they are compiled here
// against test/shim/Arduino.h and test/shim/CrowPanelShared.h, which supply a
// test-driven millis() and a scripted GT911 sample. Build and run with
// scripts/test-cypher-keys.sh.
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <CrowPanelShared.h>  // the test shim: CrowDisplay::touchPoints

#include "../src/HidKeyboard.h"
#include "../src/KeySoundPacks.h"
#include "../src/KeysLayout.h"
#include "../src/KeysTouch.h"

// Where the converted sound packs live on this machine. Only used by the
// real-WAV tests, which SKIP (never fail) when the tree is not present, so the
// harness stays runnable on a fresh checkout.
#ifndef CYPHER_KEYS_TEST_SOUND_DIR
#define CYPHER_KEYS_TEST_SOUND_DIR ""
#endif

// ---- test plumbing ----------------------------------------------------------

uint32_t gHostMillis = 0;

namespace {

uint16_t gFailures = 0;

void expect(const char *name, bool ok) {
  printf("[host] %-58s %s\n", name, ok ? "PASS" : "FAIL");
  if (!ok) gFailures++;
}

// Scripted GT911 sample the shim hands to KeysTouch.
CrowDisplay::TouchPointData gRaw[KeysTouch::kMaxContacts];
uint8_t gRawCount = 0;

void rawNone() { gRawCount = 0; }

void rawOne(int16_t x, int16_t y, uint8_t id) {
  gRaw[0].x = x;
  gRaw[0].y = y;
  gRaw[0].id = id;
  gRawCount = 1;
}

void rawTwo(int16_t x0, int16_t y0, uint8_t id0, int16_t x1, int16_t y1,
            uint8_t id1) {
  rawOne(x0, y0, id0);
  gRaw[1].x = x1;
  gRaw[1].y = y1;
  gRaw[1].id = id1;
  gRawCount = 2;
}

void pollAfter(KeysTouch &touch, uint32_t ms) {
  gHostMillis += ms;
  touch.tick();
}

}  // namespace

uint8_t CrowDisplay::touchPoints(CrowDisplay::TouchPointData *out,
                                 uint8_t maxPoints) {
  uint8_t n = gRawCount < maxPoints ? gRawCount : maxPoints;
  for (uint8_t i = 0; i < n; i++) out[i] = gRaw[i];
  return n;
}

// ---- key geometry mirror ----------------------------------------------------
// The key TABLES live in an anonymous namespace inside HidKeyboard.cpp, so the
// tests mirror just the per-row weights to compute a key's center. The mirror is
// self-checking: press() asserts the id HidKeyboard returns is the row/index it
// aimed at, which only holds if these weights match the shipped ones.
namespace {

struct Cell {
  uint8_t weight;
};

const Cell kLetters0[] = {{10}, {10}, {10}, {10}, {10},
                          {10}, {10}, {10}, {10}, {10}};
const Cell kLetters1[] = {{10}, {10}, {10}, {10}, {10}, {10}, {10}, {10}, {10}};
const Cell kLetters2[] = {{15}, {10}, {10}, {10}, {10}, {10}, {10}, {10}, {15}};
const Cell kLetters3[] = {{14}, {12}, {12}, {12}, {34}, {12}, {12}, {18}};

const Cell kSymbols0[] = {{10}, {10}, {10}, {10}, {10},
                          {10}, {10}, {10}, {10}, {10}};
const Cell kSymbols1[] = {{10}, {10}, {10}, {10}, {10},
                          {10}, {10}, {10}, {10}, {10}};
const Cell kSymbols2[] = {{15}, {10}, {10}, {10}, {10}, {10}, {10}, {10}, {15}};
const Cell kSymbols3[] = {{14}, {12}, {34}, {12}, {12}, {12}, {18}};

struct Row {
  const Cell *cells;
  uint8_t count;
};

Row rowAt(bool symbols, uint8_t row) {
  if (!symbols) {
    if (row == 0) return {kLetters0, 10};
    if (row == 1) return {kLetters1, 9};
    if (row == 2) return {kLetters2, 9};
    return {kLetters3, 8};
  }
  if (row == 0) return {kSymbols0, 10};
  if (row == 1) return {kSymbols1, 10};
  if (row == 2) return {kSymbols2, 9};
  return {kSymbols3, 7};
}

struct KeyRef {
  uint8_t row;
  uint8_t idx;
};

// Letters layer.
const KeyRef kKeyQ = {0, 0};
const KeyRef kKeyA = {1, 0};
const KeyRef kKeyShift = {2, 0};
const KeyRef kKeyC = {2, 3};
const KeyRef kKeyBack = {2, 8};
const KeyRef kKey123 = {3, 0};
const KeyRef kKeyCtrl = {3, 1};
const KeyRef kKeyOpt = {3, 2};
const KeyRef kKeyCmd = {3, 3};
const KeyRef kKeySpace = {3, 4};
const KeyRef kKeyLeft = {3, 5};
const KeyRef kKeyRet = {3, 7};
// Symbols layer.
const KeyRef kKeyOne = {0, 0};
const KeyRef kKeyAbc = {3, 0};

void centerOf(bool symbols, KeyRef k, int16_t &cx, int16_t &cy) {
  Row row = rowAt(symbols, k.row);
  int16_t x = 0, y = 0, w = 0, h = 0;
  KeysLayout::keyBounds(row.cells, row.count, k.row, k.idx, x, y, w, h);
  cx = (int16_t)(x + w / 2);
  cy = (int16_t)(y + h / 2);
}

int16_t expectedId(KeyRef k) { return (int16_t)(k.row * 16 + k.idx); }

struct Press {
  HidKeyEvent ev;
  int16_t id;
};

// Press a key by name, hit-testing its center in whatever layer is up. Verifies
// the geometry mirror on the way through.
Press press(HidKeyboard &kb, KeyRef k, const char *what) {
  int16_t cx = 0, cy = 0;
  centerOf(kb.symbols(), k, cx, cy);
  Press p;
  p.id = HidKeyboard::kNoKey;
  p.ev = kb.pressAt(cx, cy, p.id);
  if (p.id != expectedId(k)) {
    char name[96];
    snprintf(name, sizeof(name), "%s: press resolves to row %u key %u", what,
             k.row, k.idx);
    expect(name, false);
  }
  return p;
}

// ---- HidKeyboard: chording, sticky one-shots, repeat -----------------------

void testHoldCmdTapC() {
  HidKeyboard kb;
  Press cmd = press(kb, kKeyCmd, "hold-CMD");
  expect("hold CMD: press sends nothing, only redraws",
         !cmd.ev.send && cmd.ev.redraw);
  expect("hold CMD: modifier is live while held",
         kb.heldMods() == kModCmd && kb.effectiveMods() == kModCmd);

  Press c = press(kb, kKeyC, "chord-C");
  expect("hold CMD + tap C: sends 'c' with Cmd",
         c.ev.send && c.ev.key == 'c' && c.ev.mods == kModCmd);

  kb.releaseKey(c.id);
  expect("chord: Cmd still held after C lifts", kb.heldMods() == kModCmd);

  HidKeyEvent up = kb.releaseKey(cmd.id);
  expect("chord: releasing a USED CMD arms nothing sticky",
         kb.heldMods() == 0 && kb.stickyMods() == 0 && up.redraw);

  Press plain = press(kb, kKeyA, "post-chord");
  expect("chord: the next key is unmodified",
         plain.ev.send && plain.ev.key == 'a' && plain.ev.mods == 0);
}

void testTapCmdThenC() {
  HidKeyboard kb;
  Press cmd = press(kb, kKeyCmd, "tap-CMD");
  HidKeyEvent up = kb.releaseKey(cmd.id);
  expect("tap CMD: an unused hold becomes a sticky one-shot",
         kb.stickyMods() == kModCmd && kb.heldMods() == 0 && up.redraw);

  Press c = press(kb, kKeyC, "sticky-C");
  expect("tap CMD then C: sends 'c' with Cmd",
         c.ev.send && c.ev.key == 'c' && c.ev.mods == kModCmd);
  kb.releaseKey(c.id);
  expect("sticky Cmd is consumed by that one key", kb.stickyMods() == 0);

  Press again = press(kb, kKeyC, "post-sticky");
  expect("the key after a sticky mod is unmodified",
         again.ev.send && again.ev.key == 'c' && again.ev.mods == 0);
}

void testDoubleTapCmdClearsSticky() {
  HidKeyboard kb;
  Press first = press(kb, kKeyCmd, "tap-CMD-1");
  kb.releaseKey(first.id);
  expect("double tap CMD: first tap arms sticky", kb.stickyMods() == kModCmd);
  Press second = press(kb, kKeyCmd, "tap-CMD-2");
  expect("double tap CMD: still armed while held down",
         kb.effectiveMods() == kModCmd);
  kb.releaseKey(second.id);
  expect("double tap CMD: second tap disarms it",
         kb.stickyMods() == 0 && kb.heldMods() == 0);
}

void testHoldShiftTapA() {
  HidKeyboard kb;
  Press shift = press(kb, kKeyShift, "hold-SHIFT");
  expect("hold SHIFT: press sends nothing, only redraws",
         !shift.ev.send && shift.ev.redraw);
  expect("hold SHIFT: shift is live while held",
         kb.heldShift() && !kb.shifted());

  Press a = press(kb, kKeyA, "shift-A");
  expect("hold SHIFT + tap a: sends 'A' with no modifier bits",
         a.ev.send && a.ev.key == 'A' && a.ev.mods == 0);
  kb.releaseKey(a.id);

  kb.releaseKey(shift.id);
  expect("hold SHIFT: releasing a USED shift arms nothing",
         !kb.heldShift() && !kb.shifted());
  Press lower = press(kb, kKeyA, "post-shift");
  expect("hold SHIFT: the next key is lowercase again",
         lower.ev.send && lower.ev.key == 'a');
}

void testTapShiftThenA() {
  HidKeyboard kb;
  Press shift = press(kb, kKeyShift, "tap-SHIFT");
  HidKeyEvent up = kb.releaseKey(shift.id);
  expect("tap SHIFT: an unused hold becomes a one-shot shift",
         kb.shifted() && !kb.heldShift() && up.redraw);

  Press a = press(kb, kKeyA, "sticky-A");
  expect("tap SHIFT then a: sends 'A'",
         a.ev.send && a.ev.key == 'A' && a.ev.mods == 0);
  kb.releaseKey(a.id);
  expect("one-shot shift is consumed by that one key", !kb.shifted());

  Press again = press(kb, kKeyA, "post-sticky-shift");
  expect("the key after a one-shot shift is lowercase",
         again.ev.send && again.ev.key == 'a');
}

void testTwoModsHeldAtOnce() {
  HidKeyboard kb;
  Press cmd = press(kb, kKeyCmd, "hold-CMD");
  Press opt = press(kb, kKeyOpt, "hold-OPT");
  expect("two mods held: both are live",
         kb.heldMods() == (kModCmd | kModOpt));
  expect("holding a second mod does not consume the first",
         !opt.ev.send && kb.effectiveMods() == (kModCmd | kModOpt));

  Press c = press(kb, kKeyC, "chord-C");
  expect("hold CMD+OPT, tap C: sends 'c' with both mods",
         c.ev.send && c.ev.key == 'c' && c.ev.mods == (kModCmd | kModOpt));
  kb.releaseKey(c.id);
  kb.releaseKey(cmd.id);
  kb.releaseKey(opt.id);
  expect("two mods held: neither is left sticky after a used chord",
         kb.stickyMods() == 0 && kb.heldMods() == 0);

  // Three-finger chord with shift folded in: shift rides as ASCII case.
  HidKeyboard kb2;
  Press cmd2 = press(kb2, kKeyCmd, "hold-CMD");
  Press sh2 = press(kb2, kKeyShift, "hold-SHIFT");
  Press a2 = press(kb2, kKeyA, "chord-A");
  expect("hold CMD+SHIFT, tap a: sends 'A' with Cmd",
         a2.ev.send && a2.ev.key == 'A' && a2.ev.mods == kModCmd);
  kb2.releaseKey(a2.id);
  kb2.releaseKey(sh2.id);
  kb2.releaseKey(cmd2.id);
  expect("hold CMD+SHIFT: nothing sticky survives a used chord",
         kb2.stickyMods() == 0 && !kb2.shifted());
}

void testStickyPlusHeldCompose() {
  HidKeyboard kb;
  Press ctrl = press(kb, kKeyCtrl, "tap-CTRL");
  kb.releaseKey(ctrl.id);  // unused -> sticky Ctrl
  Press cmd = press(kb, kKeyCmd, "hold-CMD");
  expect("sticky Ctrl + held Cmd compose",
         kb.effectiveMods() == (kModCtrl | kModCmd));
  Press c = press(kb, kKeyC, "chord-C");
  expect("sticky Ctrl + held Cmd, tap C: sends both",
         c.ev.send && c.ev.mods == (kModCtrl | kModCmd));
  kb.releaseKey(c.id);
  expect("only the sticky half is consumed",
         kb.stickyMods() == 0 && kb.heldMods() == kModCmd);
  kb.releaseKey(cmd.id);
  expect("the held half leaves nothing behind", kb.effectiveMods() == 0);
}

void testSpecialKeysAndOneShots() {
  HidKeyboard kb;
  Press cmd = press(kb, kKeyCmd, "hold-CMD");
  Press ret = press(kb, kKeyRet, "chord-RETURN");
  expect("hold CMD + tap RETURN: sends Return with Cmd",
         ret.ev.send && ret.ev.key == kKeyReturn && ret.ev.mods == kModCmd);
  kb.releaseKey(ret.id);
  kb.releaseKey(cmd.id);
  expect("special-key chord leaves nothing sticky", kb.effectiveMods() == 0);

  // A special key consumes the one-shots too.
  HidKeyboard kb2;
  Press sh = press(kb2, kKeyShift, "tap-SHIFT");
  kb2.releaseKey(sh.id);
  Press back = press(kb2, kKeyBack, "BACK");
  expect("a special key consumes the armed one-shot shift",
         back.ev.send && back.ev.key == kKeyBackspace && !kb2.shifted());

  // Releasing a text or special key never transmits anything.
  HidKeyboard kb3;
  Press space = press(kb3, kKeySpace, "SPACE");
  HidKeyEvent up = kb3.releaseKey(space.id);
  expect("releasing a text key sends nothing and needs no repaint",
         space.ev.send && space.ev.key == ' ' && !up.send && !up.redraw);
}

void testRepeat() {
  HidKeyboard kb;
  Press back = press(kb, kKeyBack, "BACK");
  expect("BACK press sends Backspace once",
         back.ev.send && back.ev.key == kKeyBackspace);
  expect("BACK auto-repeats while held", kb.repeats(back.id));
  HidKeyEvent r = kb.repeatKey(back.id);
  expect("BACK repeat re-sends Backspace with no mods",
         r.send && r.key == kKeyBackspace && r.mods == 0 && !r.redraw);
  kb.releaseKey(back.id);

  Press left = press(kb, kKeyLeft, "LEFT");
  expect("the arrows auto-repeat while held", kb.repeats(left.id));
  expect("LEFT press sends the left arrow",
         left.ev.send && left.ev.key == kKeyLeftArrow);
  kb.releaseKey(left.id);

  Press c = press(kb, kKeyC, "C");
  expect("letters never auto-repeat", !kb.repeats(c.id));
  expect("repeatKey on a non-repeating key sends nothing",
         !kb.repeatKey(c.id).send);
  kb.releaseKey(c.id);
  Press ret = press(kb, kKeyRet, "RETURN");
  expect("RETURN never auto-repeats", !kb.repeats(ret.id));
  kb.releaseKey(ret.id);
  Press q = press(kb, kKeyQ, "Q");
  expect("repeats() is false for a plain letter id", !kb.repeats(q.id));
  kb.releaseKey(q.id);
  expect("repeats() is false for 'no key'", !kb.repeats(HidKeyboard::kNoKey));
}

void testRepeatCarriesHeldButNotStickyMods() {
  // A held modifier rides every repeat: hold OPT, hold LEFT -> Opt+Left xN.
  HidKeyboard kb;
  Press opt = press(kb, kKeyOpt, "hold-OPT");
  Press left = press(kb, kKeyLeft, "hold-LEFT");
  expect("hold OPT + hold LEFT: first send carries Opt",
         left.ev.send && left.ev.mods == kModOpt);
  HidKeyEvent r1 = kb.repeatKey(left.id);
  HidKeyEvent r2 = kb.repeatKey(left.id);
  expect("every repeat still carries the held Opt",
         r1.send && r1.mods == kModOpt && r2.send && r2.mods == kModOpt);
  kb.releaseKey(left.id);
  kb.releaseKey(opt.id);

  // A sticky one-shot is consumed by the first send and must NOT repeat.
  HidKeyboard kb2;
  Press cmd = press(kb2, kKeyCmd, "tap-CMD");
  kb2.releaseKey(cmd.id);
  Press back = press(kb2, kKeyBack, "BACK");
  expect("sticky Cmd rides the first Backspace",
         back.ev.send && back.ev.mods == kModCmd);
  HidKeyEvent r = kb2.repeatKey(back.id);
  expect("sticky Cmd does NOT ride the repeats", r.send && r.mods == 0);
}

void testPressOwnership() {
  // Ownership: the press is resolved once, at the DOWN point, and the release is
  // driven by that id - never by where the finger ended up.
  HidKeyboard kb;
  int16_t cx = 0, cy = 0;
  centerOf(false, kKeyCmd, cx, cy);
  int16_t id = HidKeyboard::kNoKey;
  kb.pressAt(cx, cy, id);
  expect("pressAt reports the key id under the DOWN point",
         id == expectedId(kKeyCmd));

  // Release by id from a completely different part of the screen: still CMD.
  kb.releaseKey(id);
  expect("releaseKey(id) unwinds the key that was pressed, not one under the "
         "finger",
         kb.heldMods() == 0 && kb.stickyMods() == kModCmd);

  // Off-keyboard points bind to nothing.
  HidKeyboard kb2;
  int16_t none = 0;
  HidKeyEvent ev = kb2.pressAt(500, 100, none);  // macro band, above row 0
  expect("a press above the keyboard binds no key and does nothing",
         none == HidKeyboard::kNoKey && !ev.send && !ev.redraw);
  expect("releaseKey(kNoKey) is inert",
         !kb2.releaseKey(HidKeyboard::kNoKey).send);
}

void testSymbolsLayer() {
  HidKeyboard kb;
  Press sym = press(kb, kKey123, "123");
  expect("123 raises the symbols layer and repaints",
         kb.symbols() && sym.ev.redraw && !sym.ev.send);
  Press one = press(kb, kKeyOne, "symbol-1");
  expect("symbols layer types '1' where Q used to be",
         one.ev.send && one.ev.key == '1');
  kb.releaseKey(one.id);
  Press abc = press(kb, kKeyAbc, "ABC");
  expect("ABC drops back to letters", !kb.symbols() && abc.ev.redraw);

  // A layer flip cannot leave a modifier stuck down: the key ids it was bound
  // to no longer mean the same key, so the chord is dropped instead.
  HidKeyboard kb2;
  press(kb2, kKeyCmd, "hold-CMD");
  press(kb2, kKey123, "123");
  expect("a layer flip drops held modifiers instead of stranding them",
         kb2.symbols() && kb2.heldMods() == 0 && kb2.stickyMods() == 0);

  // reset() clears every kind of state.
  HidKeyboard kb3;
  press(kb3, kKeyCmd, "hold-CMD");
  press(kb3, kKeyShift, "hold-SHIFT");
  kb3.reset();
  expect("reset() clears held, sticky, shift and layer",
         kb3.heldMods() == 0 && kb3.stickyMods() == 0 && !kb3.heldShift() &&
             !kb3.shifted() && !kb3.symbols());
}

// ---- KeysTouch: cadence, debounce, multi-contact ---------------------------

void testTouchPressAndDebounce() {
  KeysTouch touch;
  gHostMillis = 1000;
  rawOne(300, 400, 0);
  touch.tick();  // the very first tick always polls
  expect("touch: first contact opens slot 0 with a press edge",
         touch.activeCount() == 1 && touch.contact(0).pressedEdge &&
             touch.contact(0).active);
  expect("touch: down position is captured for hit-testing",
         touch.contact(0).downX == 300 && touch.contact(0).downY == 400);
  expect("touch: primary view mirrors the contact",
         touch.down() && touch.pressedEdge() && touch.x() == 300 &&
             touch.y() == 400 && touch.count() == 1);

  // A dropped GT911 frame in the middle of a press must not read as a lift.
  rawNone();
  pollAfter(touch, CYPHER_KEYS_TOUCH_POLL_MS);
  expect("touch: one empty frame starts the debounce but holds the press",
         touch.down() && !touch.releasedEdge() &&
             touch.contact(0).releasePending);
  rawOne(302, 401, 0);
  pollAfter(touch, CYPHER_KEYS_TOUCH_POLL_MS);
  expect("touch: the contact reappearing cancels the pending release",
         touch.down() && !touch.releasedEdge() && touch.count() == 1 &&
             !touch.contact(0).releasePending && touch.x() == 302);

  // A real lift needs the full debounce window of empty frames.
  rawNone();
  pollAfter(touch, CYPHER_KEYS_TOUCH_POLL_MS);  // debounce starts
  expect("touch: still down one poll into the debounce window", touch.down());
  pollAfter(touch, CYPHER_KEYS_TOUCH_POLL_MS);  // 16 ms < 30 ms
  expect("touch: still down two polls into the debounce window", touch.down());
  pollAfter(touch, CYPHER_KEYS_TOUCH_POLL_MS);  // 32 ms >= 30 ms -> commit
  expect("touch: the release commits once the window elapses",
         !touch.down() && touch.releasedEdge() && touch.releaseX() == 302 &&
             touch.releaseY() == 401);
  pollAfter(touch, CYPHER_KEYS_TOUCH_POLL_MS);
  expect("touch: the release edge lasts exactly one tick",
         !touch.releasedEdge() && touch.activeCount() == 0);
}

void testTouchPollCadence() {
  KeysTouch touch;
  gHostMillis = 5000;
  rawNone();
  touch.tick();  // first poll: nothing down
  rawOne(100, 350, 1);
  pollAfter(touch, 5);
  expect("touch: a tick inside the poll window does not read the panel",
         !touch.down() && touch.count() == 0);
  pollAfter(touch, CYPHER_KEYS_TOUCH_POLL_MS);
  expect("touch: the next poll past the window picks the contact up",
         touch.down() && touch.count() == 1);
  int16_t heldX = touch.x();
  rawOne(500, 350, 1);
  pollAfter(touch, 2);
  expect("touch: position is held steady between polls",
         touch.x() == heldX && !touch.pressedEdge());
}

void testTouchTwoContacts() {
  KeysTouch touch;
  gHostMillis = 9000;
  rawOne(200, 500, 3);  // finger 1: a modifier key
  touch.tick();
  touch.contact(0).owner = 51;  // as HidDeck would bind it

  rawTwo(200, 500, 3, 700, 500, 4);  // finger 2 lands
  pollAfter(touch, CYPHER_KEYS_TOUCH_POLL_MS);
  expect("touch: a second finger opens its own slot with its own press edge",
         touch.activeCount() == 2 && touch.contact(1).pressedEdge &&
             touch.contact(1).downX == 700);
  expect("touch: the first contact keeps its binding and shows no new edge",
         touch.contact(0).owner == 51 && !touch.contact(0).pressedEdge);
  expect("touch: the primary view ignores the second finger",
         !touch.pressedEdge() && touch.x() == 200 && touch.count() == 2);

  // Finger 2 lifts; finger 1 stays down.
  rawOne(200, 500, 3);
  pollAfter(touch, CYPHER_KEYS_TOUCH_POLL_MS);
  pollAfter(touch, CYPHER_KEYS_TOUCH_POLL_MS);
  pollAfter(touch, CYPHER_KEYS_TOUCH_POLL_MS);
  expect("touch: the second finger's release lands on its own slot",
         touch.contact(1).releasedEdge && !touch.contact(1).active &&
             touch.activeCount() == 1);
  expect("touch: the primary view reports no release while finger 1 is down",
         !touch.releasedEdge() && touch.down() &&
             touch.contact(0).owner == 51);
}

void testTouchIdChurn() {
  KeysTouch touch;
  gHostMillis = 20000;
  rawOne(400, 450, 0);
  touch.tick();
  touch.contact(0).owner = 40;  // BACK, say - mid hold-repeat

  rawOne(406, 452, 9);  // same finger, brand-new track id
  pollAfter(touch, CYPHER_KEYS_TOUCH_POLL_MS);
  expect("touch: a churned track id is matched by proximity, not re-pressed",
         touch.count() == 1 && touch.activeCount() == 1 &&
             touch.contact(0).id == 9);
  expect("touch: the key binding survives an id churn",
         touch.contact(0).owner == 40 && touch.contact(0).x == 406);

  rawTwo(406, 452, 9, 900, 560, 11);  // far away -> a genuine new press
  pollAfter(touch, CYPHER_KEYS_TOUCH_POLL_MS);
  expect("touch: a distant new id is a new press, not a churn",
         touch.count() == 2 && touch.activeCount() == 2 &&
             touch.contact(1).pressedEdge);

  String diag = touch.diagnostics();
  expect("touch: diagnostics report the live contact count",
         strstr(diag.c_str(), "contacts=2") != nullptr &&
             strstr(diag.c_str(), "presses=2") != nullptr);
  printf("[host] diagnostics: %s\n", diag.c_str());
}

// ---- KeySoundPacks: WAV header parsing and clip resolution -----------------
// The parser and the resolution order are pure functions in the shipped
// translation unit, so these tests exercise the exact code the firmware runs -
// including against the real converted WAVs when they are present.

void put16(uint8_t *p, uint16_t v) {
  p[0] = (uint8_t)(v & 0xFF);
  p[1] = (uint8_t)(v >> 8);
}

void put32(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)(v & 0xFF);
  p[1] = (uint8_t)((v >> 8) & 0xFF);
  p[2] = (uint8_t)((v >> 16) & 0xFF);
  p[3] = (uint8_t)((v >> 24) & 0xFF);
}

struct WavBuild {
  uint8_t buf[256] = {};
  uint32_t len = 0;
  uint32_t dataAt = 0;
};

// Builds an ffmpeg-shaped header: RIFF/WAVE, fmt, an optional LIST chunk (which
// is exactly what convert-key-sounds.sh emits, and the reason the parser has to
// walk chunks at all), then data. listSize < 0 omits the LIST chunk.
void buildWav(WavBuild &w, uint16_t channels, uint16_t bits, uint32_t rate,
              uint32_t dataBytes, int16_t listSize, bool withData,
              const char *magic) {
  uint8_t *b = w.buf;
  uint32_t n = 0;
  memcpy(b + n, magic, 4);
  n += 4;
  put32(b + n, 0);  // RIFF size: the parser never trusts it
  n += 4;
  memcpy(b + n, "WAVE", 4);
  n += 4;
  memcpy(b + n, "fmt ", 4);
  n += 4;
  put32(b + n, 16);
  n += 4;
  put16(b + n, 1);  // PCM
  n += 2;
  put16(b + n, channels);
  n += 2;
  put32(b + n, rate);
  n += 4;
  put32(b + n, rate * channels * bits / 8);
  n += 4;
  put16(b + n, (uint16_t)(channels * bits / 8));
  n += 2;
  put16(b + n, bits);
  n += 2;
  if (listSize >= 0) {
    memcpy(b + n, "LIST", 4);
    n += 4;
    put32(b + n, (uint32_t)listSize);
    n += 4;
    memcpy(b + n, "INFO", 4);
    n += (uint32_t)listSize + (uint32_t)(listSize & 1);  // word-aligned + pad
  }
  if (withData) {
    memcpy(b + n, "data", 4);
    n += 4;
    put32(b + n, dataBytes);
    n += 4;
    w.dataAt = n;
    n += 16;  // a little payload so len covers more than the header
  } else {
    memcpy(b + n, "junk", 4);  // no data chunk at all
    n += 4;
    put32(b + n, 8);
    n += 4;
    n += 8;
  }
  w.len = n;
}

void testWavHeaderParser() {
  const uint32_t rate = CYPHER_KEYS_AUDIO_SAMPLE_RATE;
  KeySoundPacks::WavInfo info;
  String why;

  WavBuild good;
  buildWav(good, 1, 16, rate, 512, 26, true, "RIFF");
  expect("wav: 16-bit mono at the engine rate is accepted",
         KeySoundPacks::parseWavHeader(good.buf, good.len, info, why));
  expect("wav: the LIST chunk is skipped and data is located",
         info.dataOffset == good.dataAt && info.dataBytes == 512 &&
             info.rate == rate && info.channels == 1 && info.bits == 16);

  // An odd chunk size carries a pad byte; getting that wrong desynchronizes the
  // walk and the data chunk is never found.
  WavBuild odd;
  buildWav(odd, 1, 16, rate, 256, 9, true, "RIFF");
  expect("wav: an odd-sized chunk's pad byte is accounted for",
         KeySoundPacks::parseWavHeader(odd.buf, odd.len, info, why) &&
             info.dataOffset == odd.dataAt);

  WavBuild noList;
  buildWav(noList, 1, 16, rate, 64, -1, true, "RIFF");
  expect("wav: a bare fmt+data file parses too",
         KeySoundPacks::parseWavHeader(noList.buf, noList.len, info, why));

  WavBuild stereo;
  buildWav(stereo, 2, 16, rate, 512, 26, true, "RIFF");
  expect("wav: stereo is rejected (the engine mixes mono)",
         !KeySoundPacks::parseWavHeader(stereo.buf, stereo.len, info, why) &&
             strstr(why.c_str(), "mono") != nullptr);

  WavBuild eight;
  buildWav(eight, 1, 8, rate, 512, 26, true, "RIFF");
  expect("wav: 8-bit is rejected",
         !KeySoundPacks::parseWavHeader(eight.buf, eight.len, info, why) &&
             strstr(why.c_str(), "16-bit") != nullptr);

  WavBuild fast;
  buildWav(fast, 1, 16, 44100, 512, 26, true, "RIFF");
  expect("wav: a wrong sample rate is rejected, never resampled",
         !KeySoundPacks::parseWavHeader(fast.buf, fast.len, info, why) &&
             strstr(why.c_str(), "44100") != nullptr);

  WavBuild notRiff;
  buildWav(notRiff, 1, 16, rate, 512, 26, true, "RIFX");
  expect("wav: a non-RIFF file is rejected",
         !KeySoundPacks::parseWavHeader(notRiff.buf, notRiff.len, info, why));

  WavBuild noData;
  buildWav(noData, 1, 16, rate, 0, 26, false, "RIFF");
  expect("wav: a file with no data chunk is rejected",
         !KeySoundPacks::parseWavHeader(noData.buf, noData.len, info, why) &&
             strstr(why.c_str(), "data") != nullptr);

  expect("wav: a runt buffer is rejected before any chunk walk",
         !KeySoundPacks::parseWavHeader(good.buf, 20, info, why));
  expect("wav: a null buffer is rejected",
         !KeySoundPacks::parseWavHeader(nullptr, 512, info, why));
}

void testSlotForFile() {
  using namespace KeySoundPacks;
  expect("slots: press/GENERIC_R0.wav is the row-0 press clip",
         slotForFile(false, "GENERIC_R0.wav") == kPressR0);
  expect("slots: press/GENERIC_R4.wav is the row-4 press clip",
         slotForFile(false, "GENERIC_R4.wav") == kPressR4);
  expect("slots: press specials map to their own clips",
         slotForFile(false, "BACKSPACE.wav") == kPressBackspace &&
             slotForFile(false, "ENTER.wav") == kPressEnter &&
             slotForFile(false, "SPACE.wav") == kPressSpace);
  expect("slots: release specials map to the release clips",
         slotForFile(true, "GENERIC.wav") == kReleaseGeneric &&
             slotForFile(true, "BACKSPACE.wav") == kReleaseBackspace &&
             slotForFile(true, "ENTER.wav") == kReleaseEnter &&
             slotForFile(true, "SPACE.wav") == kReleaseSpace);
  // bluealps really does ship this file; it must be ignored, not mistaken for
  // the release clip.
  expect("slots: release/GENERIC_long.wav is ignored",
         slotForFile(true, "GENERIC_long.wav") == kSlotNone);
  expect("slots: a full path is reduced to its basename",
         slotForFile(false, "/cypher-keys/sounds/topre/press/GENERIC_R2.WAV") ==
             kPressR2);
  expect("slots: names are matched case-insensitively",
         slotForFile(false, "generic_r1.wav") == kPressR1 &&
             slotForFile(false, "space.WAV") == kPressSpace);
  expect("slots: GENERIC_R5 and friends are ignored",
         slotForFile(false, "GENERIC_R5.wav") == kSlotNone &&
             slotForFile(false, "GENERIC_RX.wav") == kSlotNone &&
             slotForFile(false, "GENERIC.wav") == kSlotNone);
  expect("slots: a release phase has no per-row clips",
         slotForFile(true, "GENERIC_R0.wav") == kSlotNone);
  expect("slots: dotfiles and junk are ignored",
         slotForFile(false, "._GENERIC_R0.wav") == kSlotNone &&
             slotForFile(false, ".DS_Store") == kSlotNone &&
             slotForFile(false, "notes.txt") == kSlotNone &&
             slotForFile(false, nullptr) == kSlotNone);
}

uint16_t maskOf(const uint8_t *slots, uint8_t count) {
  uint16_t mask = 0;
  for (uint8_t i = 0; i < count; ++i) mask |= (uint16_t)(1u << slots[i]);
  return mask;
}

void testClipResolutionOrder() {
  using namespace KeySoundPacks;

  // A complete pack: every class and row has its own clip.
  const uint16_t full = (uint16_t)((1u << kSlotCount) - 1u);
  expect("resolve: a full pack plays its own BACKSPACE press clip",
         resolvePressSlot(full, kClassBackspace, 2) == kPressBackspace);
  expect("resolve: a full pack plays its own ENTER / SPACE press clips",
         resolvePressSlot(full, kClassEnter, 3) == kPressEnter &&
             resolvePressSlot(full, kClassSpace, 3) == kPressSpace);
  expect("resolve: a generic key plays its own row's press clip",
         resolvePressSlot(full, kClassGeneric, 0) == kPressR0 &&
             resolvePressSlot(full, kClassGeneric, 1) == kPressR1 &&
             resolvePressSlot(full, kClassGeneric, 3) == kPressR3);
  expect("resolve: a row past the pack's rows clamps to the last one",
         resolvePressSlot(full, kClassGeneric, 9) == kPressR4);
  expect("resolve: a full pack plays its own release clips",
         resolveReleaseSlot(full, kClassBackspace) == kReleaseBackspace &&
             resolveReleaseSlot(full, kClassEnter) == kReleaseEnter &&
             resolveReleaseSlot(full, kClassSpace) == kReleaseSpace &&
             resolveReleaseSlot(full, kClassGeneric) == kReleaseGeneric);

  // kbsim's mxblue: press GENERIC_R0..R4 plus release GENERIC, nothing else.
  const uint8_t mxblueSlots[] = {kPressR0,  kPressR1, kPressR2,
                                 kPressR3,  kPressR4, kReleaseGeneric};
  const uint16_t mxblue = maskOf(mxblueSlots, 6);
  expect("resolve: mxblue has no BACKSPACE press clip -> that row's GENERIC",
         resolvePressSlot(mxblue, kClassBackspace, 2) == kPressR2);
  expect("resolve: mxblue's ENTER and SPACE fall back to their rows too",
         resolvePressSlot(mxblue, kClassEnter, 3) == kPressR3 &&
             resolvePressSlot(mxblue, kClassSpace, 3) == kPressR3);
  expect("resolve: every mxblue release falls back to release/GENERIC",
         resolveReleaseSlot(mxblue, kClassBackspace) == kReleaseGeneric &&
             resolveReleaseSlot(mxblue, kClassEnter) == kReleaseGeneric &&
             resolveReleaseSlot(mxblue, kClassSpace) == kReleaseGeneric);

  // A one-clip pack: everything collapses onto GENERIC_R0, and a release has
  // nothing at all so the synthesized clack covers it.
  const uint8_t oneSlot[] = {kPressR0};
  const uint16_t sparse = maskOf(oneSlot, 1);
  expect("resolve: a press with no row clip falls back to GENERIC_R0",
         resolvePressSlot(sparse, kClassGeneric, 3) == kPressR0 &&
             resolvePressSlot(sparse, kClassBackspace, 2) == kPressR0);
  expect("resolve: no release clip at all -> synthesized fallback",
         resolveReleaseSlot(sparse, kClassGeneric) == kSlotNone &&
             resolveReleaseSlot(sparse, kClassEnter) == kSlotNone);
  expect("resolve: an empty pack resolves to nothing in both directions",
         resolvePressSlot(0, kClassGeneric, 0) == kSlotNone &&
             resolveReleaseSlot(0, kClassGeneric) == kSlotNone);

  // A pack with only the special-key clips still uses them; the generic keys
  // have nothing and fall through to the synthesized profile.
  const uint8_t specialsOnly[] = {kPressBackspace, kReleaseBackspace};
  const uint16_t specials = maskOf(specialsOnly, 2);
  expect("resolve: a class clip is used even when no GENERIC_R* exists",
         resolvePressSlot(specials, kClassBackspace, 2) == kPressBackspace &&
             resolvePressSlot(specials, kClassGeneric, 2) == kSlotNone);
}

// The deck's own key ids -> class/row -> pack slot, end to end: this is the path
// HidDeck::serviceKeyboardTouch drives on every touch.
void testKeySoundClassAndRow() {
  using namespace KeySoundPacks;
  HidKeyboard kb;
  const int16_t qId = expectedId(kKeyQ);
  const int16_t backId = expectedId(kKeyBack);
  const int16_t retId = expectedId(kKeyRet);
  const int16_t spaceId = expectedId(kKeySpace);
  const int16_t cmdId = expectedId(kKeyCmd);

  // keySoundRow() reports the SAMPLE-PACK row, not the panel row. Packs count
  // rows from the top of a full ANSI board (R0 = function row, R1 = numbers,
  // R2 = QWERTY, R3 = ASDF, R4 = ZXCV and below), so this panel's four rows sit
  // at R2..R4 in the letters layer. Verified against kbsim's KeySimulator.js.
  expect("class: Q is generic and maps to the QWERTY sample row R2",
         kb.keySoundClass(qId) == kClassGeneric && kb.keySoundRow(qId) == 2);
  expect("class: BACK is Backspace on the ZXCV sample row R4",
         kb.keySoundClass(backId) == kClassBackspace &&
             kb.keySoundRow(backId) == 4);
  expect("class: RETURN is Enter on the bottom sample row R4",
         kb.keySoundClass(retId) == kClassEnter && kb.keySoundRow(retId) == 4);
  expect("class: SPACE is Space on the bottom sample row R4",
         kb.keySoundClass(spaceId) == kClassSpace &&
             kb.keySoundRow(spaceId) == 4);
  expect("class: a modifier key is generic",
         kb.keySoundClass(cmdId) == kClassGeneric);
  expect("class: an unbound id is generic on row 0",
         kb.keySoundClass(HidKeyboard::kNoKey) == kClassGeneric &&
             kb.keySoundRow(HidKeyboard::kNoKey) == 0);

  // Symbols layer: BACK / RETURN / SPACE keep their classes even though the row
  // is packed differently (SPACE at index 2, RETURN at index 6), and the layer's
  // own extra keys (ESC, TAB) are generic.
  Press sym = press(kb, kKey123, "123");
  (void)sym;
  expect("class: the symbols layer still classes RETURN and SPACE",
         kb.symbols() && kb.keySoundClass((int16_t)(3 * 16 + 6)) == kClassEnter &&
             kb.keySoundClass((int16_t)(3 * 16 + 2)) == kClassSpace);
  expect("class: the symbols layer's BACK is still Backspace",
         kb.keySoundClass(backId) == kClassBackspace);
  // The symbols layer puts the number row up top, so its rows shift down one
  // sample row relative to the letters layer: R1 (numbers) .. R4 (bottom).
  expect("class: the symbols number row maps to the numbers sample row R1",
         kb.keySoundRow((int16_t)0) == 1);
  expect("class: the symbols bottom row still maps to R4",
         kb.keySoundRow((int16_t)(3 * 16 + 2)) == 4);
  expect("class: ESC and TAB are generic",
         kb.keySoundClass((int16_t)(3 * 16 + 1)) == kClassGeneric &&
             kb.keySoundClass((int16_t)(3 * 16 + 3)) == kClassGeneric);
  // A stale id from the other layer (letters RETURN is index 7, which the
  // symbols row does not have) classifies as generic rather than misfiring.
  expect("class: a stale cross-layer id falls back to generic",
         kb.keySoundClass(expectedId(kKeyRet)) == kClassGeneric);

  // And the whole chain, against the two real-world pack shapes.
  HidKeyboard letters;
  const uint16_t full = (uint16_t)((1u << kSlotCount) - 1u);
  const uint8_t mxblueSlots[] = {kPressR0, kPressR1, kPressR2,
                                 kPressR3, kPressR4, kReleaseGeneric};
  const uint16_t mxblue = maskOf(mxblueSlots, 6);
  expect("chain: tapping BACK on a full pack plays press/BACKSPACE",
         resolvePressSlot(full, letters.keySoundClass(backId),
                          letters.keySoundRow(backId)) == kPressBackspace);
  expect("chain: tapping BACK on mxblue falls back to press/GENERIC_R4",
         resolvePressSlot(mxblue, letters.keySoundClass(backId),
                          letters.keySoundRow(backId)) == kPressR4);
  expect("chain: releasing SPACE on mxblue plays release/GENERIC",
         resolveReleaseSlot(mxblue, letters.keySoundClass(spaceId)) ==
             kReleaseGeneric);
  expect("chain: typing Q on mxblue plays press/GENERIC_R2",
         resolvePressSlot(mxblue, letters.keySoundClass(qId),
                          letters.keySoundRow(qId)) == kPressR2);
}

// ---- real converted WAVs (skipped when the tree is absent) ------------------

// Scans one phase folder, parsing every recognized file through the shipped
// header parser, and returns the present-mask the firmware would end up with.
uint16_t scanPackPhase(const std::string &packDir, bool releasePhase,
                       unsigned &accepted, unsigned &rejected,
                       unsigned &ignored) {
  const std::string dir = packDir + (releasePhase ? "/release" : "/press");
  DIR *handle = opendir(dir.c_str());
  if (handle == nullptr) return 0;
  uint16_t mask = 0;
  const struct dirent *entry = readdir(handle);
  while (entry != nullptr) {
    const std::string name = entry->d_name;
    if (name != "." && name != "..") {
      const uint8_t slot = KeySoundPacks::slotForFile(releasePhase, name.c_str());
      if (slot == KeySoundPacks::kSlotNone) {
        ignored++;
      } else {
        uint8_t head[512] = {};
        FILE *file = fopen((dir + "/" + name).c_str(), "rb");
        const size_t got = file ? fread(head, 1, sizeof(head), file) : 0;
        if (file != nullptr) fclose(file);
        KeySoundPacks::WavInfo info;
        String why;
        if (KeySoundPacks::parseWavHeader(head, (uint32_t)got, info, why)) {
          mask |= (uint16_t)(1u << slot);
          accepted++;
        } else {
          rejected++;
          printf("[host]   rejected %s/%s: %s\n",
                 releasePhase ? "release" : "press", name.c_str(), why.c_str());
        }
      }
    }
    entry = readdir(handle);
  }
  closedir(handle);
  return mask;
}

void testRealSoundPacks() {
  const char *root = getenv("CYPHER_KEYS_SOUND_DIR");
  if (root == nullptr || root[0] == '\0') root = CYPHER_KEYS_TEST_SOUND_DIR;
  DIR *rootDir = (root[0] != '\0') ? opendir(root) : nullptr;
  if (rootDir == nullptr) {
    printf("[host] real sound packs: SKIP (no tree at \"%s\"; set "
           "CYPHER_KEYS_SOUND_DIR)\n",
           root);
    return;
  }

  unsigned packs = 0, accepted = 0, rejected = 0, ignored = 0, unusable = 0;
  uint16_t mxblueMask = 0, alpacaMask = 0, bluealpsRelease = 0;
  bool sawMxblue = false, sawAlpaca = false, sawBluealps = false;

  const struct dirent *entry = readdir(rootDir);
  while (entry != nullptr) {
    const std::string name = entry->d_name;
    if (name != "." && name != ".." && name[0] != '.') {
      const std::string packDir = std::string(root) + "/" + name;
      const uint16_t press = scanPackPhase(packDir, false, accepted, rejected, ignored);
      const uint16_t rel = scanPackPhase(packDir, true, accepted, rejected, ignored);
      if (press != 0 || rel != 0) {
        packs++;
        const uint16_t mask = (uint16_t)(press | rel);
        if ((mask & (1u << KeySoundPacks::kPressR0)) == 0) unusable++;
        if (name == "mxblue") { mxblueMask = mask; sawMxblue = true; }
        if (name == "alpaca") { alpacaMask = mask; sawAlpaca = true; }
        if (name == "bluealps") { bluealpsRelease = rel; sawBluealps = true; }
      }
    }
    entry = readdir(rootDir);
  }
  closedir(rootDir);

  printf("[host] real packs at %s: %u packs, %u clips parsed, %u rejected, "
         "%u files ignored\n",
         root, packs, accepted, rejected, ignored);
  expect("real: at least one pack was found", packs > 0);
  expect("real: every recognized WAV parses as 16-bit mono at the engine rate",
         rejected == 0);
  expect("real: every pack has the required press/GENERIC_R0.wav",
         unusable == 0);

  if (sawMxblue) {
    using namespace KeySoundPacks;
    const uint8_t expectSlots[] = {kPressR0, kPressR1, kPressR2,
                                   kPressR3, kPressR4, kReleaseGeneric};
    expect("real: mxblue is exactly GENERIC_R0..R4 + release/GENERIC",
           mxblueMask == maskOf(expectSlots, 6));
    expect("real: mxblue's BACK press resolves to GENERIC_R2",
           resolvePressSlot(mxblueMask, kClassBackspace, 2) == kPressR2);
    expect("real: mxblue's ENTER release resolves to release/GENERIC",
           resolveReleaseSlot(mxblueMask, kClassEnter) == kReleaseGeneric);
  } else {
    printf("[host] mxblue: SKIP (not in the tree)\n");
  }
  if (sawAlpaca) {
    using namespace KeySoundPacks;
    expect("real: alpaca is a complete 12-clip pack",
           alpacaMask == (uint16_t)((1u << kSlotCount) - 1u));
    expect("real: alpaca's BACK press uses its own BACKSPACE clip",
           resolvePressSlot(alpacaMask, kClassBackspace, 2) == kPressBackspace);
  } else {
    printf("[host] alpaca: SKIP (not in the tree)\n");
  }
  if (sawBluealps) {
    using namespace KeySoundPacks;
    const uint8_t releaseSlots[] = {kReleaseGeneric, kReleaseBackspace,
                                    kReleaseEnter, kReleaseSpace};
    expect("real: bluealps' extra release/GENERIC_long.wav is ignored",
           bluealpsRelease == maskOf(releaseSlots, 4));
  } else {
    printf("[host] bluealps: SKIP (not in the tree)\n");
  }
}

}  // namespace

int main() {
  setvbuf(stdout, nullptr, _IOLBF, 0);

  printf("[host] HidKeyboard: modifier chording and one-shots\n");
  testHoldCmdTapC();
  testTapCmdThenC();
  testDoubleTapCmdClearsSticky();
  testHoldShiftTapA();
  testTapShiftThenA();
  testTwoModsHeldAtOnce();
  testStickyPlusHeldCompose();
  testSpecialKeysAndOneShots();

  printf("\n[host] HidKeyboard: hold-repeat and press ownership\n");
  testRepeat();
  testRepeatCarriesHeldButNotStickyMods();
  testPressOwnership();
  testSymbolsLayer();

  printf("\n[host] KeysTouch: poll cadence, release debounce, multi-contact\n");
  testTouchPressAndDebounce();
  testTouchPollCadence();
  testTouchTwoContacts();
  testTouchIdChurn();

  printf("\n[host] KeySoundPacks: WAV headers, filename map, clip fallbacks\n");
  testWavHeaderParser();
  testSlotForFile();
  testClipResolutionOrder();
  testKeySoundClassAndRow();
  testRealSoundPacks();

  printf("\n[host] %s (%u failures)\n", gFailures == 0 ? "ALL PASS" : "FAILURES",
         (unsigned)gFailures);
  return gFailures == 0 ? 0 : 1;
}
