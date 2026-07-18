#include "DeskPrompts.h"

namespace {

const char *kCategoryNames[] = {"Morning Pages", "Observation", "Memory", "Scene", "Letter"};

const char *kBuiltInPrompts[40] = {
    "What is taking up the most room in your mind this morning?",
    "Write the first honest sentence that arrives.",
    "What would make today feel spacious?",
    "Name three things you can let be unfinished.",
    "Describe the mood of the room before anyone speaks.",
    "What are you quietly looking forward to?",
    "Write about the day without using the word busy.",
    "Begin with: Today I want to notice...",
    "Describe one ordinary object as if it belongs in a museum.",
    "Watch a shadow for a minute. What story does it suggest?",
    "Write down five sounds that are usually ignored.",
    "Describe a stranger using only gestures and clothing.",
    "What changed in this room since yesterday?",
    "Study your hands. What work do they remember?",
    "Describe the weather without naming the weather.",
    "Choose a color nearby and follow it through the room.",
    "Write about a kitchen you remember clearly.",
    "What did your childhood bedroom sound like at night?",
    "Recall a small kindness you almost forgot.",
    "Describe the oldest object you still use.",
    "Write about a road you could travel from memory.",
    "Remember a day that felt longer than twenty-four hours.",
    "What smell instantly returns you to another time?",
    "Write about a photograph that was never taken.",
    "Two people wait for a shop to open in the rain.",
    "A receipt contains a message meant for someone else.",
    "The power goes out halfway through an important conversation.",
    "Someone returns a borrowed book after twenty years.",
    "A quiet cafe has one table that is always reserved.",
    "Begin a scene with a door that should not be open.",
    "A character finds a key with no recognizable lock.",
    "Two old friends disagree about the same shared memory.",
    "Write a letter to the version of you who needs a slow day.",
    "Write to a place that taught you something.",
    "Thank an object that has served you well.",
    "Write a letter you will never send to an old friend.",
    "Tell tomorrow what you hope it carries gently.",
    "Write to your work as if it were a person.",
    "Apologize to a hobby you stopped making time for.",
    "Write a note from your future desk back to this one."};

}  // namespace

void DeskPrompts::begin() { refill(); }

void DeskPrompts::refill() {
  for (uint8_t i = 0; i < 8; ++i) order_[i] = i;
  for (int8_t i = 7; i > 0; --i) {
    uint8_t j = random(i + 1);
    uint8_t temp = order_[i];
    order_[i] = order_[j];
    order_[j] = temp;
  }
  cursor_ = 0;
}

void DeskPrompts::setCategory(DeskPromptCategory category) {
  if (category >= kDeskPromptCategoryCount) category = kDeskPromptMorning;
  category_ = category;
  refill();
}
DeskPromptCategory DeskPrompts::category() const { return category_; }
const char *DeskPrompts::categoryName() const { return kCategoryNames[category_]; }
String DeskPrompts::current() const {
  uint8_t index = order_[cursor_ < 8 ? cursor_ : 0];
  uint8_t absolute = static_cast<uint8_t>(category_) * 8 + index;
  if (replacementCount_ == 40) return replacements_[absolute];
  return String(kBuiltInPrompts[absolute]);
}
String DeskPrompts::shuffle() {
  if (++cursor_ >= 8) refill();
  return current();
}

bool DeskPrompts::loadReplacement(const String &body) {
  replacementCount_ = 0;
  int start = 0;
  while (start <= static_cast<int>(body.length()) && replacementCount_ < 40) {
    int end = body.indexOf('\n', start);
    if (end < 0) end = body.length();
    String line = body.substring(start, end);
    line.trim();
    if (line.length() && !line.startsWith("#")) replacements_[replacementCount_++] = line;
    start = end + 1;
  }
  if (replacementCount_ != 40) {
    replacementCount_ = 0;
    return false;
  }
  refill();
  return true;
}
bool DeskPrompts::usingSdPrompts() const { return replacementCount_ == 40; }
