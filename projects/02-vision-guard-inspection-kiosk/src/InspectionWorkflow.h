#ifndef VISION_GUARD_INSPECTION_WORKFLOW_H
#define VISION_GUARD_INSPECTION_WORKFLOW_H

#include <Arduino.h>

// Per-check outcome. PENDING only exists before a run is evaluated; a
// recorded run always resolves every item to PASS/FAIL/SKIP.
enum ItemState : uint8_t {
  ITEM_PENDING = 0,
  ITEM_PASS,
  ITEM_FAIL,
  ITEM_SKIP,
};

const char *itemStateLabel(ItemState s);   // "PASS" / "FAIL" / "SKIP" / "PEND"

// One auditable inspection run. Fixed-size, POD-copyable (no heap, no
// pointers) so the in-session history ring can shift entries with plain
// struct assignment - the same storage policy the shared EventLog uses.
struct InspectionRun {
  uint32_t id;                 // stable id, monotonic per boot (0 = invalid)
  char qr[24];
  ItemState items[7];
  uint8_t passed;
  uint8_t failed;
  uint8_t skipped;
  bool failStatus;             // true => overall FAIL (failed > 0)
  unsigned long createdMs;     // millis() at creation
  uint32_t seedAgoS;           // extra "age" for seeded demo runs (0 for live)
  char reason[48];             // checklist-derived verdict line
  char aiNote[96];             // vision-client observation
  bool valid;
};

// Owns the inspection domain: the static checklist definition, the current
// run under review, and an in-session ring of completed runs. The UI only
// READS this model; every mutation is driven from the sketch via a serial
// command or a UI event, keeping rendering and state cleanly separated.
class InspectionWorkflow {
 public:
  static const uint8_t kItemCount = 7;
  static const uint8_t kHistoryCap = 16;

  void begin();   // seeds a spread of sample runs and selects the newest

  uint8_t itemCount() const { return kItemCount; }
  const char *itemName(uint8_t i) const;
  const char *itemDetail(uint8_t i) const;

  // Create a run for `qr`, auto-evaluate every item, push it to the front of
  // history. makeCurrent selects it for the Checklist/Result screens; a
  // background scan (makeCurrent=false) only grows the audit log. The vision
  // note is attached afterwards (once the outcome is known) via setNoteForRun.
  const InspectionRun &recordScan(const String &qr, bool makeCurrent);

  // Attach an AI/vision note to a specific run (by id). No-op if it aged out.
  void setNoteForRun(uint32_t id, const String &note);

  // --- Operate on the currently selected run ---
  bool hasCurrent() const;
  const InspectionRun &current() const;
  void cycleItem(uint8_t i);            // PASS -> FAIL -> SKIP -> PASS
  void setItem(uint8_t i, ItemState s);
  void reEvaluateCurrent();             // re-roll the current run's items

  // --- History access (age index: 0 = newest) ---
  uint8_t historyCount() const { return count_; }
  const InspectionRun &runAt(uint8_t ageIdx) const;
  bool selectRun(uint8_t ageIdx);       // make history[ageIdx] the current run
  uint8_t currentAgeIndex() const;      // where current sits in history (0 if lost)

  uint32_t totalRuns() const { return runCounter_; }

 private:
  struct ItemDef { const char *name; const char *detail; };
  static const ItemDef kItems[kItemCount];

  InspectionRun runs_[kHistoryCap];     // newest at index 0
  InspectionRun empty_{};               // returned when there is no current run
  uint8_t count_ = 0;
  uint32_t currentId_ = 0;
  uint32_t lastId_ = 0;
  uint32_t runCounter_ = 0;

  int findCurrentIndex_() const;        // index of current run, or -1
  InspectionRun &insertFront_();        // shift ring down, return runs_[0]
  void evaluate_(InspectionRun &r, uint32_t seed);
  void recompute_(InspectionRun &r);
  void seedRun_(const char *qr, const char *aiNote, uint32_t agoS, const char *pattern);
};

#endif
