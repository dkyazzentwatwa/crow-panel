#ifndef INKWELL_XHTML_PARSER_H
#define INKWELL_XHTML_PARSER_H

#include <cstddef>
#include <string>
#include <vector>

#include "InkDoc.h"

namespace Ink {

// Parses one EPUB chapter's XHTML into blocks. Parsing starts at <body>
// (case-insensitive) if present, otherwise the whole input is parsed.
//
// Tag subset: h1->H1, h2->H2, h3..h6->H3, p/div -> paragraph breaks (Body),
// blockquote -> Quote (the whole subtree, including any nested <p>, joins
// into ONE Quote block -- a minified "<p>a</p><p>b</p>" inside it still
// joins with a space, as if real whitespace had separated them), pre ->
// Code (whitespace preserved verbatim, one block per <pre>), li ->
// ListItem (depth = ul/ol nesting depth capped at 3, ordered from the
// nearest list ancestor), hr -> Rule (zero runs, like MarkdownParser's
// thematic break), br -> a collapsible space, em/i -> italic, strong/b ->
// bold, code/tt -> mono (inline styles are nesting-depth counters --
// <b><b></b></b> stays bold until the second close, and a stray closing
// tag at depth 0 is ignored). <head> subtrees are dropped entirely.
// <script>/<style> are raw-text elements: everything up to their literal
// (case-insensitive) closing tag is opaque content, never re-tokenized as
// markup, so a stray '<' inside JS ("if(a<b)") or CSS can't be mistaken
// for a tag. HTML comments (<!-- ... -->) and bogus/declaration markup
// (<!DOCTYPE ...>, <?...?>) are scanned past and dropped, not emitted as
// text. Every other tag is transparent: its own markup is skipped but any
// text inside still flows through. Attributes are ignored wholesale.
//
// Tolerant of malformed input: missing close tags (the next same-scope
// block tag, or EOF, flushes whatever's pending), self-closing tags,
// attribute noise (single or double quotes, and a quoted value containing
// '>' doesn't end the tag early), a bare '<' not followed by a letter,
// '/', '!' or '?' (HTML5 rule -- it's literal text, e.g. "a < b" or a
// digit as in "5 <6"), and a missing '>' before EOF (the rest of the
// input is treated as plain text).
//
// Known degenerate inputs, documented but not fixed: a <pre> nested
// inside a <blockquote> loses its verbatim whitespace (the whole subtree
// is one merged, whitespace-collapsed Quote block, and <pre> handling is
// only recognized at quote-depth 0); conversely a literal (unescaped)
// "<p>" appearing inside real <pre> raw text would incorrectly split the
// Code block, since block-tag recognition still runs on <pre> content --
// valid XHTML never needs this because such characters are escaped.
//
// Whitespace outside <pre> collapses like HTML: runs of space/tab/
// newline/CR become a single space, and each block's text is trimmed of
// leading/trailing whitespace. Every EMITTED block has >=1 run with
// non-empty text, except Rule, which always has zero runs (the
// renderer's documented exception) -- a block that never accumulates any
// real text (e.g. a <div> immediately superseded by a nested
// block-opening tag before any text arrived) is dropped rather than
// materialized as an empty placeholder.
std::vector<Block> parseXhtml(const std::string &src);

// Decodes HTML/XML entities in a plain string (e.g. an EPUB TOC title, for
// EpubBook to reuse). Shares the entity table with parseXhtml: &amp; &lt;
// &gt; &quot; &apos; -> the literal char; &nbsp; -> space; &mdash; ->
// "--"; &ndash; -> "-"; &hellip; -> "..."; &#NNN; / &#xHH; -> UTF-8
// (code point 0, e.g. &#0;, emits nothing rather than a NUL byte; code
// points above 0xFFFF emit '?'). Unknown named entities are emitted
// literally, unchanged (e.g. "&unknown;" stays as-is).
std::string decodeEntities(const std::string &s);

// Decodes a single entity starting at s[i] (which must be '&'). Returns
// the number of source characters consumed (including the '&' and the
// trailing ';'), or 0 if s[i..] is not a recognized entity -- the caller
// then emits '&' literally and advances one character itself. The name
// scan is bounded so an input with no ';' anywhere ahead can't turn this
// into an unbounded find().
size_t decodeEntity(const std::string &s, size_t i, std::string &out);

}  // namespace Ink

#endif
