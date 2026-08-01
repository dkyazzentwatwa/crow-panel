#include "SocdCleaner.h"

namespace {

// Resolve one opposing pair. `a` is the negative direction (left/up),
// `b` the positive (right/down). Returns 0 none, 1 a, 2 b.
uint8_t resolveAxis(bool a, bool b, bool prevA, bool prevB, uint8_t policy,
                    uint8_t &winner) {
  // Releasing either direction clears the standing winner, so re-pressing
  // recomputes who got there first (or arrived last) instead of replaying a
  // stale answer left over from the previous time both were held.
  if (!a && !b) {
    winner = 0;
    return 0;
  }
  if (a && !b) {
    winner = 0;
    return 1;
  }
  if (!a && b) {
    winner = 0;
    return 2;
  }

  // Both held.
  switch (policy) {
    case kSocdLastInput: {
      bool aIsNew = a && !prevA;
      bool bIsNew = b && !prevB;
      if (aIsNew && !bIsNew) winner = 1;
      else if (bIsNew && !aIsNew) winner = 2;
      // Both new on the same poll, or neither new: keep the standing winner.
      return winner;
    }
    case kSocdFirstInput: {
      if (winner == 0) {
        // Whichever was already held before this poll got there first.
        if (prevA && !prevB) winner = 1;
        else if (prevB && !prevA) winner = 2;
        // Both arrived together: no first, stay neutral.
      }
      return winner;
    }
    case kSocdUpPriority:
      // Asymmetric by design: only the vertical axis is ever called with
      // this policy (socdResolve remaps the horizontal call to kSocdNeutral),
      // so reaching here always means "up vs down" and up (a) always wins.
      winner = 0;
      return 1;
    case kSocdNeutral:
    default:  // unknown policy (corrupt profile) degrades to neutral
      winner = 0;
      return 0;
  }
}

}  // namespace

uint8_t socdResolve(bool up, bool down, bool left, bool right,
                    uint8_t policy, SocdMemory &mem) {
  // Up priority is asymmetric: it means "up beats down" and says nothing
  // about left vs right, so the horizontal axis falls back to neutral.
  const uint8_t hPolicy = (policy == kSocdUpPriority) ? kSocdNeutral : policy;
  uint8_t v = resolveAxis(up, down, mem.prevUp, mem.prevDown, policy, mem.vWinner);
  uint8_t h = resolveAxis(left, right, mem.prevLeft, mem.prevRight, hPolicy, mem.hWinner);

  mem.prevUp = up;
  mem.prevDown = down;
  mem.prevLeft = left;
  mem.prevRight = right;

  // Combine into the hat enum.
  const bool u = (v == 1), d = (v == 2), l = (h == 1), r = (h == 2);
  if (u && r) return kHatUpRight;
  if (d && r) return kHatDownRight;
  if (d && l) return kHatDownLeft;
  if (u && l) return kHatUpLeft;
  if (u) return kHatUp;
  if (d) return kHatDown;
  if (l) return kHatLeft;
  if (r) return kHatRight;
  return kHatCenter;
}
