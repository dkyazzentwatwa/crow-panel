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
//
// Degrades to literal/plain: setext (Title\n===) headings, a bare `###`
// with no following space, a closing-hash sequence ("# Title ###") kept
// verbatim in the heading text, hard line breaks (trailing double-space)
// collapsed into the normal single-space join, and links/images or HTML
// tags whose closing marker falls outside the bounded lookahead window
// (>512 bytes for links/images, >=64 bytes for tags) render as literal
// markup instead of being parsed.
//
// Rule is the one BlockType that always has zero runs -- renderers must
// treat it as the exception to the "every block has >=1 run" guarantee
// the other block types uphold.
std::vector<Block> parseMarkdown(const std::string &src);

}  // namespace Ink

#endif
