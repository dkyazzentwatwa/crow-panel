#ifndef INKWELL_TXT_PARSER_H
#define INKWELL_TXT_PARSER_H

#include <string>
#include <vector>

#include "InkDoc.h"

namespace Ink {

// Parses plain-text chapter source into paragraph blocks. Blank lines (empty
// after trimming) separate paragraphs; a paragraph's lines are joined with a
// single space, and internal runs of spaces/tabs collapse to one space.
std::vector<Block> parseTxt(const std::string &src);

}  // namespace Ink

#endif
