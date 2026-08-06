#ifndef INKWELL_MARKDOWN_PARSER_H
#define INKWELL_MARKDOWN_PARSER_H

#include <string>
#include <vector>

#include "InkDoc.h"

namespace Ink {

// Parses a small Markdown subset into blocks: #/##/### headings (deeper
// ####+ collapses to H3), **bold**/*italic*/_italic_/`code` inline styles,
// -/*/1. list items (2-space nesting, capped at depth 3), > blockquotes
// (consecutive quote lines join into one block), fenced ``` code blocks
// (verbatim, no whitespace collapse), and ---/***/___  thematic breaks.
// Links keep their text (styled recursively); images and inline HTML tags
// are dropped. Everything else is a Body paragraph, like TxtParser.
std::vector<Block> parseMarkdown(const std::string &src);

}  // namespace Ink

#endif
