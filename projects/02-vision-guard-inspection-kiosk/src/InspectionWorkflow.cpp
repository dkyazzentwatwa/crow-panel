#include "InspectionWorkflow.h"
#include <CrowPanelShared.h>

const InspectionWorkflow::ItemDef InspectionWorkflow::kItems[kItemCount] = {
  {"Label alignment",  "Print centered within 2 mm"},
  {"Barcode legible",  "Decodes on the first pass"},
  {"Seal integrity",   "No tears, lifts, or gaps"},
  {"Torque spec",      "Fasteners at 4.5 Nm +/- 0.3"},
  {"Surface finish",   "No scratches, burrs, or dents"},
  {"Component count",  "All 12 parts populated"},
  {"Serial match",     "Serial matches the work order"},
};

const char *itemStateLabel(ItemState s) {
  switch (s) {
    case ITEM_PASS: return "PASS";
    case ITEM_FAIL: return "FAIL";
    case ITEM_SKIP: return "SKIP";
    default:        return "PEND";
  }
}

namespace {
// FNV-1a over a C string with a caller-supplied salt: cheap, deterministic,
// and good enough to spread believable per-item outcomes from a QR code.
uint32_t hashStr(const char *s, uint32_t seed) {
  uint32_t h = 2166136261u ^ seed;
  while (*s) {
    h ^= (uint8_t)*s++;
    h *= 16777619u;
  }
  return h;
}

ItemState nextState(ItemState s) {
  switch (s) {
    case ITEM_PASS: return ITEM_FAIL;
    case ITEM_FAIL: return ITEM_SKIP;
    case ITEM_SKIP: return ITEM_PASS;
    default:        return ITEM_PASS;
  }
}

ItemState stateFromChar(char c) {
  switch (c) {
    case 'F': case 'f': return ITEM_FAIL;
    case 'S': case 's': return ITEM_SKIP;
    default:            return ITEM_PASS;
  }
}
}  // namespace

void InspectionWorkflow::begin() {
  // Seed the audit log so every screen shows believable data with no scan yet.
  // Oldest first, so the last insert lands at history index 0 (newest).
  seedRun_("INSPECT-1001", "Vision: unit label and workspace within tolerance.", 640, "PPPPPPP");
  seedRun_("INSPECT-1002", "Vision: seal edge appears slightly lifted.",          505, "PPFPPPP");
  seedRun_("INSPECT-1003", "Vision: print crisp, barcode high-contrast.",         372, "PPPPPPP");
  seedRun_("INSPECT-1004", "Vision: two zones flagged for manual recheck.",       268, "PFPPFPP");
  seedRun_("INSPECT-1005", "Vision: acceptable; one check deferred to bench.",     141, "PPPPPSP");
  seedRun_("INSPECT-1006", "Vision: torque reading trends below target.",           47, "PPPFPPP");
  if (count_ > 0) {
    currentId_ = runs_[0].id;
  }
  Logger::info("inspection", "checklist ready; seeded " + String(count_) + " audit runs");
}

const char *InspectionWorkflow::itemName(uint8_t i) const {
  return i < kItemCount ? kItems[i].name : "";
}

const char *InspectionWorkflow::itemDetail(uint8_t i) const {
  return i < kItemCount ? kItems[i].detail : "";
}

InspectionRun &InspectionWorkflow::insertFront_() {
  uint8_t n = count_;
  if (n == kHistoryCap) {
    n = kHistoryCap - 1;  // drop the oldest entry (index kHistoryCap - 1)
  }
  for (uint8_t i = n; i > 0; i--) {
    runs_[i] = runs_[i - 1];
  }
  count_ = n + 1;
  return runs_[0];
}

const InspectionRun &InspectionWorkflow::recordScan(const String &qr, bool makeCurrent) {
  runCounter_++;
  InspectionRun &r = insertFront_();
  r.id = ++lastId_;
  snprintf(r.qr, sizeof(r.qr), "%s", qr.c_str());
  r.aiNote[0] = 0;
  r.createdMs = millis();
  r.seedAgoS = 0;
  r.valid = true;
  evaluate_(r, hashStr(r.qr, runCounter_ * 2654435761u));
  if (makeCurrent || currentId_ == 0) {
    currentId_ = r.id;
  }
  return r;
}

void InspectionWorkflow::setNoteForRun(uint32_t id, const String &note) {
  for (uint8_t i = 0; i < count_; i++) {
    if (runs_[i].id == id) {
      snprintf(runs_[i].aiNote, sizeof(runs_[i].aiNote), "%s", note.c_str());
      return;
    }
  }
}

void InspectionWorkflow::seedRun_(const char *qr, const char *aiNote, uint32_t agoS,
                                  const char *pattern) {
  runCounter_++;
  InspectionRun &r = insertFront_();
  r.id = ++lastId_;
  snprintf(r.qr, sizeof(r.qr), "%s", qr);
  snprintf(r.aiNote, sizeof(r.aiNote), "%s", aiNote);
  r.createdMs = millis();
  r.seedAgoS = agoS;
  r.valid = true;
  for (uint8_t i = 0; i < kItemCount; i++) {
    r.items[i] = pattern[i] ? stateFromChar(pattern[i]) : ITEM_PASS;
  }
  recompute_(r);
}

void InspectionWorkflow::evaluate_(InspectionRun &r, uint32_t seed) {
  for (uint8_t i = 0; i < kItemCount; i++) {
    uint32_t h = hashStr(r.qr, seed + (uint32_t)i * 40503u);
    uint8_t m = h % 100;
    r.items[i] = (m < 74) ? ITEM_PASS : (m < 90 ? ITEM_FAIL : ITEM_SKIP);
  }
  recompute_(r);
}

void InspectionWorkflow::recompute_(InspectionRun &r) {
  r.passed = r.failed = r.skipped = 0;
  int firstFail = -1;
  for (uint8_t i = 0; i < kItemCount; i++) {
    switch (r.items[i]) {
      case ITEM_PASS: r.passed++; break;
      case ITEM_FAIL: r.failed++; if (firstFail < 0) firstFail = i; break;
      case ITEM_SKIP: r.skipped++; break;
      default: break;
    }
  }
  r.failStatus = r.failed > 0;
  if (r.failed > 0) {
    snprintf(r.reason, sizeof(r.reason), "Defect: %s", kItems[firstFail].name);
  } else if (r.skipped > 0) {
    snprintf(r.reason, sizeof(r.reason), "%u passed, %u deferred", r.passed, r.skipped);
  } else {
    snprintf(r.reason, sizeof(r.reason), "All %u checks passed", (unsigned)kItemCount);
  }
}

int InspectionWorkflow::findCurrentIndex_() const {
  if (currentId_ == 0) return -1;
  for (uint8_t i = 0; i < count_; i++) {
    if (runs_[i].id == currentId_) return i;
  }
  return -1;
}

bool InspectionWorkflow::hasCurrent() const {
  return findCurrentIndex_() >= 0;
}

const InspectionRun &InspectionWorkflow::current() const {
  int idx = findCurrentIndex_();
  if (idx >= 0) return runs_[idx];
  if (count_ > 0) return runs_[0];
  return empty_;
}

void InspectionWorkflow::cycleItem(uint8_t i) {
  int idx = findCurrentIndex_();
  if (idx < 0 || i >= kItemCount) return;
  runs_[idx].items[i] = nextState(runs_[idx].items[i]);
  recompute_(runs_[idx]);
}

void InspectionWorkflow::setItem(uint8_t i, ItemState s) {
  int idx = findCurrentIndex_();
  if (idx < 0 || i >= kItemCount) return;
  runs_[idx].items[i] = s;
  recompute_(runs_[idx]);
}

void InspectionWorkflow::reEvaluateCurrent() {
  int idx = findCurrentIndex_();
  if (idx < 0) return;
  runCounter_++;
  evaluate_(runs_[idx], hashStr(runs_[idx].qr, runCounter_ * 2246822519u));
}

const InspectionRun &InspectionWorkflow::runAt(uint8_t ageIdx) const {
  if (ageIdx < count_) return runs_[ageIdx];
  return empty_;
}

bool InspectionWorkflow::selectRun(uint8_t ageIdx) {
  if (ageIdx >= count_) return false;
  currentId_ = runs_[ageIdx].id;
  return true;
}

uint8_t InspectionWorkflow::currentAgeIndex() const {
  int idx = findCurrentIndex_();
  return idx >= 0 ? (uint8_t)idx : 0;
}
