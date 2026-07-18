#ifndef CYPHER_DESK_PANEL_PROMPTS_H
#define CYPHER_DESK_PANEL_PROMPTS_H

#include <Arduino.h>

enum DeskPromptCategory {
  kDeskPromptMorning,
  kDeskPromptObservation,
  kDeskPromptMemory,
  kDeskPromptScene,
  kDeskPromptLetter,
  kDeskPromptCategoryCount
};

class DeskPrompts {
 public:
  void begin();
  void setCategory(DeskPromptCategory category);
  DeskPromptCategory category() const;
  const char *categoryName() const;
  String current() const;
  String shuffle();
  bool loadReplacement(const String &body);
  bool usingSdPrompts() const;

 private:
  DeskPromptCategory category_ = kDeskPromptMorning;
  uint8_t order_[8] = {};
  uint8_t cursor_ = 8;
  String replacements_[40];
  uint8_t replacementCount_ = 0;
  void refill();
};

#endif
