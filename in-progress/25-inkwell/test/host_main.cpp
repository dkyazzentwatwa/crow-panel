// Host tests for Inkwell's format core. No Arduino, no SD, no display:
// these are the EXACT translation units that ship in the firmware.
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include "../src/InkDoc.h"
#include "../src/MarkdownParser.h"
#include "../src/TxtParser.h"
#include "../src/XhtmlParser.h"
#include "../src/EpubBook.h"
#include "../src/miniz.h"

static int checks = 0;
#define CHECK(cond) do { \
  if (!(cond)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); return 1; } \
  ++checks; \
} while (0)

static int testTxtParagraphs() {
  auto blocks = Ink::parseTxt("First para line one.\nline two.\n\n\nSecond para.\n");
  CHECK(blocks.size() == 2);
  CHECK(blocks[0].type == Ink::BlockType::Body);
  CHECK(blocks[0].runs.size() == 1);
  CHECK(Ink::plainText(blocks[0]) == "First para line one. line two.");
  CHECK(blocks[1].srcOffset == 33);  // offset of 'S' in "Second"
  CHECK(Ink::plainText(blocks[1]) == "Second para.");
  return 0;
}

static int testTxtEdges() {
  CHECK(Ink::parseTxt("").empty());
  CHECK(Ink::parseTxt("\n\n\n").empty());
  auto crlf = Ink::parseTxt("a\r\nb\r\n\r\nc");
  CHECK(crlf.size() == 2);
  CHECK(Ink::plainText(crlf[0]) == "a b");
  auto tabs = Ink::parseTxt("x\ty");
  CHECK(Ink::plainText(tabs[0]) == "x y");
  // Bare (non-CRLF) carriage returns normalize to a space rather than
  // vanishing or corrupting the line split.
  auto loneCr = Ink::parseTxt("a\rb");
  CHECK(loneCr.size() == 1);
  CHECK(Ink::plainText(loneCr[0]) == "a b");
  // A whitespace-only line is blank -- it separates paragraphs just like
  // an empty line does.
  auto wsLine = Ink::parseTxt("x\n   \ny");
  CHECK(wsLine.size() == 2);
  // Runs of internal spaces/tabs collapse to a single space.
  auto multiSpace = Ink::parseTxt("a    b");
  CHECK(Ink::plainText(multiSpace[0]) == "a b");
  return 0;
}

static int testStyleFor() {
  Ink::Block h2;
  h2.type = Ink::BlockType::H2;
  Ink::Run boldRun;
  boldRun.bold = true;
  CHECK(Ink::styleFor(h2, boldRun) == Ink::kStyleH2);

  Ink::Block code;
  code.type = Ink::BlockType::Code;
  Ink::Run plainRun;
  CHECK(Ink::styleFor(code, plainRun) == Ink::kStyleMono);

  Ink::Block body;
  body.type = Ink::BlockType::Body;
  Ink::Run boldItalicRun;
  boldItalicRun.bold = true;
  boldItalicRun.italic = true;
  CHECK(Ink::styleFor(body, boldItalicRun) == Ink::kStyleBoldItalic);
  return 0;
}

// The offset contract is "offset of the paragraph's first non-blank
// character" -- a paragraph whose first line has leading spaces must not
// have srcOffset point at the leading whitespace.
static int testTxtLeadingSpaces() {
  auto indented = Ink::parseTxt("\n  indented para\n");
  CHECK(indented.size() == 1);
  CHECK(Ink::plainText(indented[0]) == "indented para");
  CHECK(indented[0].srcOffset == 3);  // offset of 'i' in "indented"
  return 0;
}

static int testMdHeadingsAndRule() {
  auto b = Ink::parseMarkdown("# Title\n\nBody text.\n\n## Sub *head*\n\n---\n");
  CHECK(b.size() == 4);
  CHECK(b[0].type == Ink::BlockType::H1 && Ink::plainText(b[0]) == "Title");
  CHECK(b[1].type == Ink::BlockType::Body);
  CHECK(b[2].type == Ink::BlockType::H2 && Ink::plainText(b[2]) == "Sub head");
  CHECK(b[3].type == Ink::BlockType::Rule);
  return 0;
}

static int testMdInlineStyles() {
  auto b = Ink::parseMarkdown("plain **bold** and *it* and `mono` mix");
  CHECK(b.size() == 1);
  const auto &runs = b[0].runs;
  CHECK(runs.size() == 7);
  CHECK(runs[1].text == "bold" && runs[1].bold && !runs[1].italic);
  CHECK(runs[3].text == "it" && runs[3].italic);
  CHECK(runs[5].text == "mono" && runs[5].mono);
  return 0;
}

static int testMdListQuoteCode() {
  auto b = Ink::parseMarkdown("- one\n- two\n  - nested\n\n> quoted\n\n```\ncode line\n```\n");
  CHECK(b.size() == 5);  // 3 list items + 1 quote + 1 fence, hand-counted
  CHECK(b[0].type == Ink::BlockType::ListItem && b[0].listDepth == 1 && !b[0].ordered);
  CHECK(b[2].listDepth == 2);
  CHECK(b[3].type == Ink::BlockType::Quote && Ink::plainText(b[3]) == "quoted");
  CHECK(b[4].type == Ink::BlockType::Code && Ink::plainText(b[4]) == "code line");
  return 0;
}

static int testMdEdges() {
  // Unterminated emphasis renders literally; links keep text; images drop.
  auto b = Ink::parseMarkdown("a **broken\n\n[text](http://x) ![img](y)\n\n1. first\n2. second");
  // 2 paragraphs + 2 ordered list items -- each list line is its own block
  // (no continuation joining), so "1. first" and "2. second" are separate
  // blocks. Hand-counted and verified against the implementation; pins
  // against any spurious extra/missing block.
  CHECK(b.size() == 4);
  CHECK(Ink::plainText(b[0]) == "a **broken");
  CHECK(Ink::plainText(b[1]) == "text");
  CHECK(b[2].ordered && b[2].type == Ink::BlockType::ListItem);
  return 0;
}

// A trailing image/link/tag can leave every run whitespace-only after the
// tail-trim in makeBlock; inlineRuns' >=1-run guarantee must still hold.
static int testMdZeroRunGuard() {
  auto img = Ink::parseMarkdown("![img](y)");
  CHECK(img.size() == 1);
  CHECK(img[0].type == Ink::BlockType::Body);
  CHECK(img[0].runs.size() == 1);
  CHECK(Ink::plainText(img[0]) == "");

  auto heading = Ink::parseMarkdown("# ");
  CHECK(heading.size() == 1);
  CHECK(heading[0].type == Ink::BlockType::H1);
  CHECK(heading[0].runs.size() == 1);
  return 0;
}

// Flanking rules: "5 * 3" is prose, not an opened italic run; snake_case
// underscores never toggle either.
static int testMdEmphasisFlanking() {
  auto math = Ink::parseMarkdown("5 * 3 and 2 * 4");
  CHECK(math.size() == 1);
  CHECK(math[0].runs.size() == 1);
  for (const auto &r : math[0].runs) CHECK(!r.italic);
  CHECK(Ink::plainText(math[0]) == "5 * 3 and 2 * 4");

  auto snake = Ink::parseMarkdown("snake_case_name here");
  CHECK(snake[0].runs.size() == 1);
  CHECK(Ink::plainText(snake[0]) == "snake_case_name here");
  return 0;
}

static int testMdTabListIndent() {
  auto b = Ink::parseMarkdown("\t- tabbed");
  CHECK(b.size() == 1);
  CHECK(b[0].type == Ink::BlockType::ListItem);
  CHECK(b[0].listDepth == 2);
  return 0;
}

// A lone (non-CRLF) '\r' normalizes to a space, exactly like TxtParser --
// it must never leak into rendered text as a raw 0x0D byte, and it must
// not accidentally trigger heading detection when it happens to precede a
// '#' mid-line (headings only match at the true start of a line).
static int testMdLoneCr() {
  auto plain = Ink::parseMarkdown("a\rb");
  CHECK(plain.size() == 1);
  CHECK(Ink::plainText(plain[0]) == "a b");

  auto notHeading = Ink::parseMarkdown("a\r# not-heading");
  CHECK(notHeading.size() == 1);
  CHECK(notHeading[0].type == Ink::BlockType::Body);
  CHECK(Ink::plainText(notHeading[0]).find('\r') == std::string::npos);
  return 0;
}

// A link's text renders inside whatever emphasis already encloses the
// link markup -- the recursed run must inherit the enclosing bold state.
static int testMdLinkInheritsEmphasis() {
  auto b = Ink::parseMarkdown("**bold [link](u) more**");
  CHECK(b.size() == 1);
  CHECK(b[0].runs.size() >= 3);
  for (const auto &r : b[0].runs) CHECK(r.bold);
  return 0;
}

// srcOffset is captured from the block's first line before any
// continuation join -- pin the exact byte arithmetic for a heading, a
// multi-line quote, and a paragraph.
static int testMdSrcOffsets() {
  // "# T\n\n> q1\n> q2\n\npara"
  //  0123 4 56789 ...
  // '#' at 0 (H1 start); '>' of "> q1" at byte 5; 'p' of "para" at byte 16.
  auto b = Ink::parseMarkdown("# T\n\n> q1\n> q2\n\npara");
  CHECK(b.size() == 3);
  CHECK(b[0].type == Ink::BlockType::H1 && b[0].srcOffset == 0);
  CHECK(b[1].type == Ink::BlockType::Quote && b[1].srcOffset == 5);
  CHECK(Ink::plainText(b[1]) == "q1 q2");
  CHECK(b[2].type == Ink::BlockType::Body && b[2].srcOffset == 16);
  return 0;
}

// Pins the first-occurrence consequence documented at the '[' link branch
// in MarkdownParser.cpp: `close` is the FIRST "](" at/after `i`, so the
// text between i and close can never itself contain "](" -- a nested '['
// inside it always falls through to a literal '[' rather than opening a
// second real link.
static int testMdNestedBracketFirstOccurrence() {
  auto b = Ink::parseMarkdown("[[a](u)](u)");
  CHECK(b.size() == 1);
  CHECK(b[0].runs.size() == 2);
  CHECK(b[0].runs[0].text == "[a");
  CHECK(b[0].runs[1].text == "](u)");
  return 0;
}

static int testXhtmlBasics() {
  auto b = Ink::parseXhtml(
      "<html><head><title>x</title><style>p{}</style></head><body>"
      "<h1>Chapter &amp; One</h1><p>Hello <em>world</em>, <strong>bold</strong>.</p>"
      "<hr/><blockquote><p>quoted</p></blockquote></body></html>");
  CHECK(b.size() == 4);
  CHECK(b[0].type == Ink::BlockType::H1 && Ink::plainText(b[0]) == "Chapter & One");
  CHECK(b[1].runs.size() >= 4 && b[1].runs[1].italic && b[1].runs[3].bold);
  CHECK(b[2].type == Ink::BlockType::Rule);
  CHECK(b[3].type == Ink::BlockType::Quote);
  return 0;
}

static int testXhtmlListsPreEntities() {
  auto b = Ink::parseXhtml(
      "<body><ul><li>one</li><li>two<ol><li>inner</li></ol></li></ul>"
      "<pre>  keep   spaces</pre>"
      "<p>a&nbsp;b &#65; &mdash; &hellip; &unknown; end</p></body>");
  // b[0]="one" (depth1), b[1]="two" (depth1), b[2]="inner" (depth2
  // ordered), b[3]=pre Code, b[4]=entity paragraph -- hand-traced.
  CHECK(b.size() == 5);
  CHECK(b[0].type == Ink::BlockType::ListItem && b[0].listDepth == 1);
  CHECK(b[2].listDepth == 2 && b[2].ordered);
  CHECK(b[3].type == Ink::BlockType::Code && Ink::plainText(b[3]) == "  keep   spaces");
  CHECK(Ink::plainText(b[4]) == "a b A -- ... &unknown; end");
  return 0;
}

static int testXhtmlDegrade() {
  // Unknown tags transparent; attribute noise ignored; unclosed input survives.
  auto b = Ink::parseXhtml("<body><p class=\"x\" id='y'><span>te</span>xt<p>next");
  CHECK(b.size() == 2);
  CHECK(Ink::plainText(b[0]) == "text");
  CHECK(Ink::plainText(b[1]) == "next");
  return 0;
}

static int testXhtmlDecodeEntitiesStandalone() {
  CHECK(Ink::decodeEntities("&lt;b&gt; &amp; &#x41;") == "<b> & A");
  return 0;
}

// <b><b></b></b>: bold must persist through the FIRST close (depth drops
// from 2 to 1, still >0) and only turn off at the second close (depth 0).
static int testXhtmlNestedBoldDepth() {
  auto b = Ink::parseXhtml("<body><p>x<b><b>y</b>m</b>z</p></body>");
  CHECK(b.size() == 1);
  CHECK(b[0].runs.size() == 3);
  CHECK(b[0].runs[0].text == "x" && !b[0].runs[0].bold);
  CHECK(b[0].runs[1].text == "ym" && b[0].runs[1].bold);
  CHECK(b[0].runs[2].text == "z" && !b[0].runs[2].bold);
  return 0;
}

static int testXhtmlScriptStyleDrop() {
  auto b = Ink::parseXhtml(
      "<body><p>a</p><script>var x = 1;</script><style>p{color:red}</style><p>b</p></body>");
  CHECK(b.size() == 2);
  CHECK(Ink::plainText(b[0]) == "a");
  CHECK(Ink::plainText(b[1]) == "b");
  return 0;
}

// A stray closing tag with the depth counter already at 0 is ignored --
// it must not affect the run's style state.
static int testXhtmlStrayClose() {
  auto b = Ink::parseXhtml("<body><p>a</em>b</p></body>");
  CHECK(b.size() == 1);
  CHECK(b[0].runs.size() == 1);
  CHECK(!b[0].runs[0].italic);
  CHECK(Ink::plainText(b[0]) == "ab");
  return 0;
}

// &#x263A; (WHITE SMILING FACE) is a 3-byte UTF-8 sequence: the run text
// grows by 3 bytes for that one code point, beyond the "a" + "b" bytes.
static int testXhtmlNumericEntityUtf8() {
  auto b = Ink::parseXhtml("<body><p>a&#x263A;b</p></body>");
  CHECK(b.size() == 1);
  CHECK(Ink::plainText(b[0]).size() == 5);  // 'a' + 3-byte smiley + 'b'
  return 0;
}

// A numeric entity above 0xFFFF is outside what InkDoc's pipeline is
// sized to render -- it degrades to a literal '?' rather than emitting a
// 4-byte UTF-8 sequence.
static int testXhtmlNumericEntityOverflow() {
  auto b = Ink::parseXhtml("<body><p>a&#x1F600;b</p></body>");
  CHECK(b.size() == 1);
  CHECK(Ink::plainText(b[0]) == "a?b");
  return 0;
}

// srcOffset is the byte offset of the opening tag in the ORIGINAL source
// string, not body-relative.
// "<body><p>a</p><h2>t</h2></body>"
//  0123456789...
// '<' of "<body>" at 0, its '>' at 5, so parsing starts at byte 6, which
// is exactly where "<p>" begins: b[0].srcOffset == 6. "<h2>" begins at
// byte 14 (6 + len("<p>a</p>") == 6 + 8): b[1].srcOffset == 14.
static int testXhtmlSrcOffsets() {
  auto b = Ink::parseXhtml("<body><p>a</p><h2>t</h2></body>");
  CHECK(b.size() == 2);
  CHECK(b[0].srcOffset == 6);
  CHECK(b[1].srcOffset == 14);
  return 0;
}

// --- Spec-review fixes (script/style raw text, bare '<', phantom blocks,
// blockquote join, comments/bogus markup, quoted attrs, &#0;) ---

// Fix 1 (SEVERE): a stray '<' inside <script>/<style> content (from JS
// "a<b" or a CSS selector) must never be tokenized as a tag -- doing so
// previously scanned straight past the real </script> closer and dropped
// everything after it. <script>/<style> content is now consumed as raw
// text via one linear scan for the literal closer.
static int testXhtmlScriptRawTextSurvivesAfter() {
  auto b = Ink::parseXhtml("<body><script>if(a<b){}</script><p>after</p></body>");
  CHECK(b.size() == 1);
  CHECK(Ink::plainText(b[0]) == "after");
  return 0;
}

static int testXhtmlStyleRawTextSurvivesAfter() {
  auto b = Ink::parseXhtml("<body><style>a<b{color:red}</style><p>after</p></body>");
  CHECK(b.size() == 1);
  CHECK(Ink::plainText(b[0]) == "after");
  return 0;
}

// A '<' embedded inside a JS string literal within <script> must not be
// mistaken for the closer either -- the case-insensitive scan for
// "</script" only matches the real closing tag.
static int testXhtmlScriptNestedMarkupInString() {
  auto b = Ink::parseXhtml(
      "<body><script>document.write(\"<p>x</p>\")</script><p>after</p></body>");
  CHECK(b.size() == 1);
  CHECK(Ink::plainText(b[0]) == "after");
  return 0;
}

// Fix 2: '<' only opens a tag when followed by a letter, '/', '!' or '?'
// (HTML5 rule) -- otherwise it's literal text.
static int testXhtmlBareLtLiteral() {
  auto b = Ink::parseXhtml("<body><p>a < b</p><p>next</p></body>");
  CHECK(b.size() == 2);
  CHECK(Ink::plainText(b[0]) == "a < b");
  CHECK(Ink::plainText(b[1]) == "next");
  return 0;
}

// '<' followed by a digit is not a tag opener either -- it degrades to a
// literal character with no inserted whitespace, since none was present
// in the source between '<' and '6'.
static int testXhtmlBareLtBeforeDigit() {
  auto b = Ink::parseXhtml("<body><p>5 <6 ok</p></body>");
  CHECK(b.size() == 1);
  CHECK(Ink::plainText(b[0]) == "5 <6 ok");
  return 0;
}

// Fix 3: a block that opened (e.g. a <div>) but was superseded by a
// nested block-opening tag before any real text arrived is a phantom --
// dropped rather than materialized as an empty block.
static int testXhtmlPhantomBlockDropped() {
  auto b = Ink::parseXhtml("<body><div class=\"chapter\"><h1>T</h1><p>x</p></div></body>");
  CHECK(b.size() == 2);
  CHECK(b[0].type == Ink::BlockType::H1 && Ink::plainText(b[0]) == "T");
  CHECK(b[1].type == Ink::BlockType::Body && Ink::plainText(b[1]) == "x");
  return 0;
}

// Pins </div> as non-flushing: the inner div's close does NOT end the
// block (still exactly 1 block, unlike </p>/</h*>/</li>), it only marks a
// joining space -- so "a" and "b" merge with a space between them rather
// than splitting into two blocks or gluing with none.
static int testXhtmlNestedDivMerges() {
  auto b = Ink::parseXhtml("<body><div><div>a</div>b</div></body>");
  CHECK(b.size() == 1);
  CHECK(Ink::plainText(b[0]) == "a b");
  return 0;
}

// Fix 4: a minified blockquote ("<p>a</p><p>b</p>" with no whitespace
// between them) still joins with a space, as if real whitespace had
// separated the two <p> elements.
static int testXhtmlMinifiedBlockquoteJoins() {
  auto b = Ink::parseXhtml("<body><blockquote><p>a</p><p>b</p></blockquote></body>");
  CHECK(b.size() == 1);
  CHECK(b[0].type == Ink::BlockType::Quote);
  CHECK(Ink::plainText(b[0]) == "a b");
  return 0;
}

// Fix 5: HTML comments are dropped via one linear scan to "-->", even
// when the comment's own text contains a bare '>'.
static int testXhtmlCommentDropped() {
  auto b = Ink::parseXhtml("<body><p>a</p><!-- x > y --><p>b</p></body>");
  CHECK(b.size() == 2);
  CHECK(Ink::plainText(b[0]) == "a");
  CHECK(Ink::plainText(b[1]) == "b");
  return 0;
}

// DOCTYPE and other bogus "<!...>" declarations are skipped to their
// next '>', not emitted as text.
static int testXhtmlDoctypeDropped() {
  auto b = Ink::parseXhtml("<!DOCTYPE html><body><p>a</p></body>");
  CHECK(b.size() == 1);
  CHECK(Ink::plainText(b[0]) == "a");
  return 0;
}

// Fix 6: a quoted attribute value containing '>' must not end the tag
// early.
static int testXhtmlQuotedAttributeWithGt() {
  auto b = Ink::parseXhtml("<body><p title=\"a>b\">x</p></body>");
  CHECK(b.size() == 1);
  CHECK(Ink::plainText(b[0]) == "x");
  return 0;
}

// Fix 7: a numeric entity decoding to code point 0 emits nothing -- no
// NUL byte leaks into the run text.
static int testXhtmlNulEntitySuppressed() {
  auto b = Ink::parseXhtml("<body><p>a&#0;b</p></body>");
  CHECK(b.size() == 1);
  CHECK(Ink::plainText(b[0]) == "ab");
  CHECK(Ink::plainText(b[0]).find('\0') == std::string::npos);
  return 0;
}

// Perf: the bounded/linear-scan discipline must keep even adversarial
// bare-'<' and repeated-<script> inputs well under a second.
static int testXhtmlPerfBareLt() {
  std::string src = "<body>";
  src.reserve(6 + 400000 + 7);
  for (int i = 0; i < 200000; ++i) src += "< ";  // 400000 bytes of "< "
  src += "</body>";
  auto t0 = std::chrono::steady_clock::now();
  auto b = Ink::parseXhtml(src);
  auto t1 = std::chrono::steady_clock::now();
  double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
  std::fprintf(stderr, "perf bare-lt (400KB): %.1f ms, blocks=%zu\n", ms, b.size());
  CHECK(ms < 1000.0);
  return 0;
}

static int testXhtmlPerfScriptRepeat() {
  std::string unit = "<script>a<b</script>";
  std::string src = "<body>";
  size_t reps = 400000 / unit.size() + 1;
  src.reserve(6 + reps * unit.size() + 7);
  for (size_t i = 0; i < reps; ++i) src += unit;
  src += "</body>";
  auto t0 = std::chrono::steady_clock::now();
  auto b = Ink::parseXhtml(src);
  auto t1 = std::chrono::steady_clock::now();
  double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
  std::fprintf(stderr, "perf script-repeat (~400KB): %.1f ms, blocks=%zu\n", ms, b.size());
  CHECK(ms < 1000.0);
  return 0;
}

// --- Round 3 spec-review fixes (closing-tag flush, entity accumulator
// width, table cell breaks, findBodyStart hardening) ---

// Fix 1: a closing block tag matters as much as its open. Text right
// after </h1> (even with no intervening whitespace) must start a fresh
// Body block, not glue onto the heading's text AND inherit its style.
static int testXhtmlClosingHeadingFlushesNotGlues() {
  auto b = Ink::parseXhtml("<body><h1>Title</h1>stray words<p>x</p></body>");
  CHECK(b.size() == 3);
  CHECK(b[0].type == Ink::BlockType::H1 && Ink::plainText(b[0]) == "Title");
  CHECK(b[1].type == Ink::BlockType::Body && Ink::plainText(b[1]) == "stray words");
  CHECK(b[2].type == Ink::BlockType::Body && Ink::plainText(b[2]) == "x");
  return 0;
}

static int testXhtmlClosingParagraphFlushesNotGlues() {
  auto b = Ink::parseXhtml("<body><p>para</p>stray<h2>H</h2></body>");
  CHECK(b.size() == 3);
  CHECK(b[0].type == Ink::BlockType::Body && Ink::plainText(b[0]) == "para");
  CHECK(b[1].type == Ink::BlockType::Body && Ink::plainText(b[1]) == "stray");
  CHECK(b[2].type == Ink::BlockType::H2 && Ink::plainText(b[2]) == "H");
  return 0;
}

// Fix 2: the numeric-entity accumulator is a fixed-width uint32_t,
// saturated well above the valid Unicode range as soon as it's exceeded
// -- a value like 2^32 can't wrap to a small, plausible code point on a
// 32-bit target while yielding '?' on a 64-bit host.
static int testXhtmlNumericEntityOverflow32BitHex() {
  auto b = Ink::parseXhtml("<body><p>a&#x100000000;b</p></body>");
  CHECK(b.size() == 1);
  CHECK(Ink::plainText(b[0]) == "a?b");
  return 0;
}

static int testXhtmlNumericEntityOverflow32BitDecimal() {
  auto b = Ink::parseXhtml("<body><p>a&#4294967296;b</p></body>");
  CHECK(b.size() == 1);
  CHECK(Ink::plainText(b[0]) == "a?b");
  return 0;
}

// Fix 3: td/th/tr/caption are not modeled as a grid -- each is just
// another joining-space tag, so a table's cells/rows flatten into one
// run-on paragraph.
static int testXhtmlTableCellsJoinWithSpace() {
  auto b = Ink::parseXhtml("<body><table><tr><td>a</td><td>b</td></tr></table></body>");
  CHECK(b.size() == 1);
  CHECK(Ink::plainText(b[0]) == "a b");
  return 0;
}

// Fix 4: findBodyStart is quote-aware (a <body ...> attribute containing
// '>' doesn't end the tag early) and skips comment spans (a "<body>"
// mentioned inside one isn't mistaken for the real tag).
static int testXhtmlBodyTagQuotedAttrWithGt() {
  auto b = Ink::parseXhtml("<body class=\"a>b\"><p>x</p>");
  CHECK(b.size() == 1);
  CHECK(Ink::plainText(b[0]) == "x");
  return 0;
}

static int testXhtmlBodyMentionInCommentIgnored() {
  auto b = Ink::parseXhtml("<!-- old <body> --><body><p>x</p>");
  CHECK(b.size() == 1);
  CHECK(Ink::plainText(b[0]) == "x");
  return 0;
}

// Fix 6 cheap-ride pinning tests.
static int testXhtmlEmptyInput() {
  auto b = Ink::parseXhtml("");
  CHECK(b.empty());
  return 0;
}

static int testXhtmlNoBodyParsesWholeDoc() {
  auto b = Ink::parseXhtml("<p>only</p>");
  CHECK(b.size() == 1);
  CHECK(Ink::plainText(b[0]) == "only");
  return 0;
}

// Four levels of <ul> nesting cap listDepth at 3, not 4.
static int testXhtmlListDepthCapsAtThree() {
  auto b = Ink::parseXhtml(
      "<body><ul><li>a<ul><li>b<ul><li>c<ul><li>d</li></ul></li></ul></li></ul></li></ul></body>");
  CHECK(b.size() == 4);
  CHECK(b[0].listDepth == 1);
  CHECK(b[1].listDepth == 2);
  CHECK(b[2].listDepth == 3);
  CHECK(b[3].listDepth == 3);  // 4th nesting level caps at 3, not 4
  CHECK(Ink::plainText(b[3]) == "d");
  return 0;
}

static int testXhtmlBrProducesSpace() {
  auto b = Ink::parseXhtml("<body><p>a<br/>b</p></body>");
  CHECK(b.size() == 1);
  CHECK(Ink::plainText(b[0]) == "a b");
  return 0;
}

// A tag with no '>' before EOF degrades to literal text, byte for byte.
static int testXhtmlUnterminatedTagAtEofIsLiteral() {
  auto b = Ink::parseXhtml("<body><p>a<b");
  CHECK(b.size() == 1);
  CHECK(Ink::plainText(b[0]) == "a<b");
  return 0;
}

// --- EpubBook fixtures + tests -------------------------------------------

// Builds a minimal but structurally honest EPUB in memory.
static std::string buildFixtureEpub(bool navToc) {
  mz_zip_archive z; memset(&z, 0, sizeof(z));
  mz_zip_writer_init_heap(&z, 0, 16 * 1024);
  auto add = [&](const char *name, const std::string &data) {
    mz_zip_writer_add_mem(&z, name, data.data(), data.size(), MZ_DEFAULT_COMPRESSION);
  };
  add("mimetype", "application/epub+zip");
  add("META-INF/container.xml",
      "<?xml version=\"1.0\"?><container><rootfiles>"
      "<rootfile full-path=\"OEBPS/content.opf\" media-type=\"application/oebps-package+xml\"/>"
      "</rootfiles></container>");
  std::string opf =
      "<?xml version=\"1.0\"?><package><metadata>"
      "<dc:title>Fixture Book</dc:title><dc:creator>Test Author</dc:creator>"
      "<meta name=\"cover\" content=\"cov\"/></metadata><manifest>"
      "<item id=\"c1\" href=\"ch1.xhtml\" media-type=\"application/xhtml+xml\"/>"
      "<item id=\"c2\" href=\"ch2.xhtml\" media-type=\"application/xhtml+xml\"/>"
      "<item id=\"cov\" href=\"cover.jpg\" media-type=\"image/jpeg\"/>";
  if (navToc)
    opf += "<item id=\"nav\" href=\"nav.xhtml\" media-type=\"application/xhtml+xml\" properties=\"nav\"/>";
  else
    opf += "<item id=\"ncx\" href=\"toc.ncx\" media-type=\"application/x-dtbncx+xml\"/>";
  opf += "</manifest><spine";
  if (!navToc) opf += " toc=\"ncx\"";
  opf += "><itemref idref=\"c1\"/><itemref idref=\"c2\"/></spine></package>";
  add("OEBPS/content.opf", opf);
  add("OEBPS/ch1.xhtml", "<html><body><h1>One</h1><p>First chapter text.</p></body></html>");
  add("OEBPS/ch2.xhtml", "<html><body><h1>Two</h1><p>Second chapter text.</p></body></html>");
  add("OEBPS/cover.jpg", std::string("\xFF\xD8\xFF\xE0 fake jpeg", 14));
  if (navToc)
    add("OEBPS/nav.xhtml",
        "<html><body><nav epub:type=\"toc\"><ol>"
        "<li><a href=\"ch1.xhtml\">Chapter One</a></li>"
        "<li><a href=\"ch2.xhtml#frag\">Chapter Two</a></li></ol></nav></body></html>");
  else
    add("OEBPS/toc.ncx",
        "<ncx><navMap><navPoint><navLabel><text>Chapter One</text></navLabel>"
        "<content src=\"ch1.xhtml\"/></navPoint><navPoint><navLabel>"
        "<text>Chapter Two</text></navLabel><content src=\"ch2.xhtml\"/>"
        "</navPoint></navMap></ncx>");
  void *buf = nullptr; size_t size = 0;
  mz_zip_writer_finalize_heap_archive(&z, &buf, &size);
  std::string out((char *)buf, size);
  mz_zip_writer_end(&z);
  mz_free(buf);
  return out;
}

static int testEpubOpen(bool navToc) {
  std::string zipData = buildFixtureEpub(navToc);
  Ink::EpubBook book;
  CHECK(book.open((const uint8_t *)zipData.data(), zipData.size()));
  CHECK(book.title() == "Fixture Book");
  CHECK(book.author() == "Test Author");
  CHECK(book.chapterCount() == 2);
  std::string x;
  CHECK(book.chapterXhtml(1, x) && x.find("Second chapter") != std::string::npos);
  CHECK(book.toc().size() == 2);
  CHECK(book.toc()[0].title == "Chapter One" && book.toc()[0].spineIndex == 0);
  CHECK(book.toc()[1].spineIndex == 1);  // href fragment stripped
  std::string cover, mediaType;
  CHECK(book.coverImage(cover, mediaType) && mediaType == "image/jpeg");
  return 0;
}

static int testEpubMalformed() {
  Ink::EpubBook b1, b2, b3;
  CHECK(!b1.open((const uint8_t *)"not a zip", 9));
  std::string zipData = buildFixtureEpub(true);
  CHECK(!b2.open((const uint8_t *)zipData.data(), zipData.size() / 2));  // truncated
  // zip with no container.xml
  mz_zip_archive z; memset(&z, 0, sizeof(z));
  mz_zip_writer_init_heap(&z, 0, 1024);
  mz_zip_writer_add_mem(&z, "hello.txt", "hi", 2, 0);
  void *buf = nullptr; size_t size = 0;
  mz_zip_writer_finalize_heap_archive(&z, &buf, &size);
  CHECK(!b3.open((const uint8_t *)buf, size));
  mz_zip_writer_end(&z); mz_free(buf);
  return 0;
}

// TOC titles run through Ink::decodeEntities -- "A &amp; B" must come back
// as "A & B", not the raw escaped text.
static int testEpubNavTocEntityDecodedTitle() {
  mz_zip_archive z; memset(&z, 0, sizeof(z));
  mz_zip_writer_init_heap(&z, 0, 16 * 1024);
  auto add = [&](const char *name, const std::string &data) {
    mz_zip_writer_add_mem(&z, name, data.data(), data.size(), MZ_DEFAULT_COMPRESSION);
  };
  add("mimetype", "application/epub+zip");
  add("META-INF/container.xml",
      "<?xml version=\"1.0\"?><container><rootfiles>"
      "<rootfile full-path=\"OEBPS/content.opf\" media-type=\"application/oebps-package+xml\"/>"
      "</rootfiles></container>");
  add("OEBPS/content.opf",
      "<?xml version=\"1.0\"?><package><metadata>"
      "<dc:title>T</dc:title></metadata><manifest>"
      "<item id=\"c1\" href=\"ch1.xhtml\" media-type=\"application/xhtml+xml\"/>"
      "<item id=\"nav\" href=\"nav.xhtml\" media-type=\"application/xhtml+xml\" properties=\"nav\"/>"
      "</manifest><spine><itemref idref=\"c1\"/></spine></package>");
  add("OEBPS/ch1.xhtml", "<html><body><p>x</p></body></html>");
  add("OEBPS/nav.xhtml",
      "<html><body><nav epub:type=\"toc\"><ol>"
      "<li><a href=\"ch1.xhtml\">A &amp; B</a></li></ol></nav></body></html>");
  void *buf = nullptr; size_t size = 0;
  mz_zip_writer_finalize_heap_archive(&z, &buf, &size);
  std::string zipData((char *)buf, size);
  mz_zip_writer_end(&z); mz_free(buf);

  Ink::EpubBook book;
  CHECK(book.open((const uint8_t *)zipData.data(), zipData.size()));
  CHECK(book.toc().size() == 1);
  CHECK(book.toc()[0].title == "A & B");
  return 0;
}

// A namespace-prefixed OPF root (<opf:package>) and dc: elements must still
// resolve title/creator/manifest/spine normally.
static int testEpubOpfNamespacePrefixes() {
  mz_zip_archive z; memset(&z, 0, sizeof(z));
  mz_zip_writer_init_heap(&z, 0, 16 * 1024);
  auto add = [&](const char *name, const std::string &data) {
    mz_zip_writer_add_mem(&z, name, data.data(), data.size(), MZ_DEFAULT_COMPRESSION);
  };
  add("mimetype", "application/epub+zip");
  add("META-INF/container.xml",
      "<?xml version=\"1.0\"?><container><rootfiles>"
      "<rootfile full-path=\"OEBPS/content.opf\" media-type=\"application/oebps-package+xml\"/>"
      "</rootfiles></container>");
  add("OEBPS/content.opf",
      "<?xml version=\"1.0\"?><opf:package xmlns:opf=\"x\"><opf:metadata>"
      "<dc:title>Prefixed Title</dc:title><dc:creator>Prefixed Author</dc:creator>"
      "</opf:metadata><opf:manifest>"
      "<item id=\"c1\" href=\"ch1.xhtml\" media-type=\"application/xhtml+xml\"/>"
      "</opf:manifest><opf:spine><itemref idref=\"c1\"/></opf:spine></opf:package>");
  add("OEBPS/ch1.xhtml", "<html><body><p>x</p></body></html>");
  void *buf = nullptr; size_t size = 0;
  mz_zip_writer_finalize_heap_archive(&z, &buf, &size);
  std::string zipData((char *)buf, size);
  mz_zip_writer_end(&z); mz_free(buf);

  Ink::EpubBook book;
  CHECK(book.open((const uint8_t *)zipData.data(), zipData.size()));
  CHECK(book.title() == "Prefixed Title");
  CHECK(book.author() == "Prefixed Author");
  CHECK(book.chapterCount() == 1);
  return 0;
}

// Cover resolved via properties="cover-image" on the manifest item, with no
// <meta name="cover"> present at all.
static int testEpubCoverViaProperties() {
  mz_zip_archive z; memset(&z, 0, sizeof(z));
  mz_zip_writer_init_heap(&z, 0, 16 * 1024);
  auto add = [&](const char *name, const std::string &data) {
    mz_zip_writer_add_mem(&z, name, data.data(), data.size(), MZ_DEFAULT_COMPRESSION);
  };
  add("mimetype", "application/epub+zip");
  add("META-INF/container.xml",
      "<?xml version=\"1.0\"?><container><rootfiles>"
      "<rootfile full-path=\"OEBPS/content.opf\" media-type=\"application/oebps-package+xml\"/>"
      "</rootfiles></container>");
  add("OEBPS/content.opf",
      "<?xml version=\"1.0\"?><package><metadata><dc:title>T</dc:title></metadata>"
      "<manifest>"
      "<item id=\"c1\" href=\"ch1.xhtml\" media-type=\"application/xhtml+xml\"/>"
      "<item id=\"cov\" href=\"cover.png\" media-type=\"image/png\" properties=\"cover-image\"/>"
      "</manifest><spine><itemref idref=\"c1\"/></spine></package>");
  add("OEBPS/ch1.xhtml", "<html><body><p>x</p></body></html>");
  add("OEBPS/cover.png", std::string("\x89PNG fake", 9));
  void *buf = nullptr; size_t size = 0;
  mz_zip_writer_finalize_heap_archive(&z, &buf, &size);
  std::string zipData((char *)buf, size);
  mz_zip_writer_end(&z); mz_free(buf);

  Ink::EpubBook book;
  CHECK(book.open((const uint8_t *)zipData.data(), zipData.size()));
  std::string cover, mediaType;
  CHECK(book.coverImage(cover, mediaType));
  CHECK(mediaType == "image/png");
  CHECK(cover.size() == 9);
  return 0;
}

// A TOC entry whose href resolves to nothing in the spine is kept (not
// dropped), with spineIndex -1.
static int testEpubTocEntryUnresolvedHrefKept() {
  mz_zip_archive z; memset(&z, 0, sizeof(z));
  mz_zip_writer_init_heap(&z, 0, 16 * 1024);
  auto add = [&](const char *name, const std::string &data) {
    mz_zip_writer_add_mem(&z, name, data.data(), data.size(), MZ_DEFAULT_COMPRESSION);
  };
  add("mimetype", "application/epub+zip");
  add("META-INF/container.xml",
      "<?xml version=\"1.0\"?><container><rootfiles>"
      "<rootfile full-path=\"OEBPS/content.opf\" media-type=\"application/oebps-package+xml\"/>"
      "</rootfiles></container>");
  add("OEBPS/content.opf",
      "<?xml version=\"1.0\"?><package><metadata><dc:title>T</dc:title></metadata>"
      "<manifest>"
      "<item id=\"c1\" href=\"ch1.xhtml\" media-type=\"application/xhtml+xml\"/>"
      "<item id=\"nav\" href=\"nav.xhtml\" media-type=\"application/xhtml+xml\" properties=\"nav\"/>"
      "</manifest><spine><itemref idref=\"c1\"/></spine></package>");
  add("OEBPS/ch1.xhtml", "<html><body><p>x</p></body></html>");
  add("OEBPS/nav.xhtml",
      "<html><body><nav epub:type=\"toc\"><ol>"
      "<li><a href=\"ch1.xhtml\">Real</a></li>"
      "<li><a href=\"nowhere.xhtml\">Ghost</a></li></ol></nav></body></html>");
  void *buf = nullptr; size_t size = 0;
  mz_zip_writer_finalize_heap_archive(&z, &buf, &size);
  std::string zipData((char *)buf, size);
  mz_zip_writer_end(&z); mz_free(buf);

  Ink::EpubBook book;
  CHECK(book.open((const uint8_t *)zipData.data(), zipData.size()));
  CHECK(book.toc().size() == 2);
  CHECK(book.toc()[0].spineIndex == 0);
  CHECK(book.toc()[1].title == "Ghost" && book.toc()[1].spineIndex == -1);
  return 0;
}

static int testEpubChapterXhtmlOutOfRange() {
  std::string zipData = buildFixtureEpub(true);
  Ink::EpubBook book;
  CHECK(book.open((const uint8_t *)zipData.data(), zipData.size()));
  std::string out;
  CHECK(!book.chapterXhtml(2, out));
  CHECK(!book.chapterXhtml((size_t)-1, out));
  return 0;
}

static int testEpubChapterSizePositive() {
  std::string zipData = buildFixtureEpub(true);
  Ink::EpubBook book;
  CHECK(book.open((const uint8_t *)zipData.data(), zipData.size()));
  CHECK(book.chapterSize(0) > 0);
  CHECK(book.chapterSize(1) > 0);
  return 0;
}

// Spec-review fix 3: an EPUB3 "refines" shape -- attributes on the
// dc:title/dc:creator opening tag -- must not blank out the title/author.
// The prior ":title>" fallback anchored on the CLOSING tag instead of the
// (attributed) opening tag and returned empty text.
static int testEpubAttributedMetadataTags() {
  mz_zip_archive z; memset(&z, 0, sizeof(z));
  mz_zip_writer_init_heap(&z, 0, 16 * 1024);
  auto add = [&](const char *name, const std::string &data) {
    mz_zip_writer_add_mem(&z, name, data.data(), data.size(), MZ_DEFAULT_COMPRESSION);
  };
  add("mimetype", "application/epub+zip");
  add("META-INF/container.xml",
      "<?xml version=\"1.0\"?><container><rootfiles>"
      "<rootfile full-path=\"OEBPS/content.opf\" media-type=\"application/oebps-package+xml\"/>"
      "</rootfiles></container>");
  add("OEBPS/content.opf",
      "<?xml version=\"1.0\"?><package><metadata>"
      "<dc:title id=\"t1\">Attributed Title</dc:title>"
      "<dc:creator opf:role=\"aut\">A. Author</dc:creator>"
      "</metadata><manifest>"
      "<item id=\"c1\" href=\"ch1.xhtml\" media-type=\"application/xhtml+xml\"/>"
      "</manifest><spine><itemref idref=\"c1\"/></spine></package>");
  add("OEBPS/ch1.xhtml", "<html><body><p>x</p></body></html>");
  void *buf = nullptr; size_t size = 0;
  mz_zip_writer_finalize_heap_archive(&z, &buf, &size);
  std::string zipData((char *)buf, size);
  mz_zip_writer_end(&z); mz_free(buf);

  Ink::EpubBook book;
  CHECK(book.open((const uint8_t *)zipData.data(), zipData.size()));
  CHECK(book.title() == "Attributed Title");
  CHECK(book.author() == "A. Author");
  return 0;
}

// Spec-review fix 4: a navPoint's nested child navPoint must not be
// silently dropped -- the NCX path must flatten nesting into document
// order, exactly like the nav-doc path already does for nested <a> markup.
static int testEpubNcxNestedNavPointFlattened() {
  mz_zip_archive z; memset(&z, 0, sizeof(z));
  mz_zip_writer_init_heap(&z, 0, 16 * 1024);
  auto add = [&](const char *name, const std::string &data) {
    mz_zip_writer_add_mem(&z, name, data.data(), data.size(), MZ_DEFAULT_COMPRESSION);
  };
  add("mimetype", "application/epub+zip");
  add("META-INF/container.xml",
      "<?xml version=\"1.0\"?><container><rootfiles>"
      "<rootfile full-path=\"OEBPS/content.opf\" media-type=\"application/oebps-package+xml\"/>"
      "</rootfiles></container>");
  add("OEBPS/content.opf",
      "<?xml version=\"1.0\"?><package><metadata><dc:title>T</dc:title></metadata>"
      "<manifest>"
      "<item id=\"c1\" href=\"ch1.xhtml\" media-type=\"application/xhtml+xml\"/>"
      "<item id=\"c2\" href=\"ch2.xhtml\" media-type=\"application/xhtml+xml\"/>"
      "<item id=\"ncx\" href=\"toc.ncx\" media-type=\"application/x-dtbncx+xml\"/>"
      "</manifest><spine toc=\"ncx\">"
      "<itemref idref=\"c1\"/><itemref idref=\"c2\"/></spine></package>");
  add("OEBPS/ch1.xhtml", "<html><body><p>x</p></body></html>");
  add("OEBPS/ch2.xhtml", "<html><body><p>y</p></body></html>");
  add("OEBPS/toc.ncx",
      "<ncx><navMap><navPoint><navLabel><text>Parent</text></navLabel>"
      "<content src=\"ch1.xhtml\"/>"
      "<navPoint><navLabel><text>Child</text></navLabel>"
      "<content src=\"ch2.xhtml\"/></navPoint>"
      "</navPoint></navMap></ncx>");
  void *buf = nullptr; size_t size = 0;
  mz_zip_writer_finalize_heap_archive(&z, &buf, &size);
  std::string zipData((char *)buf, size);
  mz_zip_writer_end(&z); mz_free(buf);

  Ink::EpubBook book;
  CHECK(book.open((const uint8_t *)zipData.data(), zipData.size()));
  CHECK(book.toc().size() == 2);
  CHECK(book.toc()[0].title == "Parent" && book.toc()[0].spineIndex == 0);
  CHECK(book.toc()[1].title == "Child" && book.toc()[1].spineIndex == 1);
  return 0;
}

// Spec-review round-2 fix 1a: a navPoint missing <content> must not reach
// into the FOLLOWING navPoint's own <content> -- it should resolve to
// spineIndex -1, and the following navPoint must still come through
// intact with its own title/content.
static int testEpubNcxNavPointMissingContent() {
  mz_zip_archive z; memset(&z, 0, sizeof(z));
  mz_zip_writer_init_heap(&z, 0, 16 * 1024);
  auto add = [&](const char *name, const std::string &data) {
    mz_zip_writer_add_mem(&z, name, data.data(), data.size(), MZ_DEFAULT_COMPRESSION);
  };
  add("mimetype", "application/epub+zip");
  add("META-INF/container.xml",
      "<?xml version=\"1.0\"?><container><rootfiles>"
      "<rootfile full-path=\"OEBPS/content.opf\" media-type=\"application/oebps-package+xml\"/>"
      "</rootfiles></container>");
  add("OEBPS/content.opf",
      "<?xml version=\"1.0\"?><package><metadata><dc:title>T</dc:title></metadata>"
      "<manifest>"
      "<item id=\"c1\" href=\"ch1.xhtml\" media-type=\"application/xhtml+xml\"/>"
      "<item id=\"c2\" href=\"ch2.xhtml\" media-type=\"application/xhtml+xml\"/>"
      "<item id=\"ncx\" href=\"toc.ncx\" media-type=\"application/x-dtbncx+xml\"/>"
      "</manifest><spine toc=\"ncx\">"
      "<itemref idref=\"c1\"/><itemref idref=\"c2\"/></spine></package>");
  add("OEBPS/ch1.xhtml", "<html><body><p>x</p></body></html>");
  add("OEBPS/ch2.xhtml", "<html><body><p>y</p></body></html>");
  add("OEBPS/toc.ncx",
      "<ncx><navMap>"
      "<navPoint><navLabel><text>NoContent</text></navLabel></navPoint>"
      "<navPoint><navLabel><text>Second</text></navLabel>"
      "<content src=\"ch2.xhtml\"/></navPoint>"
      "</navMap></ncx>");
  void *buf = nullptr; size_t size = 0;
  mz_zip_writer_finalize_heap_archive(&z, &buf, &size);
  std::string zipData((char *)buf, size);
  mz_zip_writer_end(&z); mz_free(buf);

  Ink::EpubBook book;
  CHECK(book.open((const uint8_t *)zipData.data(), zipData.size()));
  CHECK(book.toc().size() == 2);
  CHECK(book.toc()[0].title == "NoContent" && book.toc()[0].spineIndex == -1);
  CHECK(book.toc()[1].title == "Second" && book.toc()[1].spineIndex == 1);
  return 0;
}

// Spec-review round-2 fix 1b: a navPoint missing <navLabel> must yield an
// empty title (not the FOLLOWING navPoint's title), and the following
// navPoint must still come through intact.
static int testEpubNcxNavPointMissingLabel() {
  mz_zip_archive z; memset(&z, 0, sizeof(z));
  mz_zip_writer_init_heap(&z, 0, 16 * 1024);
  auto add = [&](const char *name, const std::string &data) {
    mz_zip_writer_add_mem(&z, name, data.data(), data.size(), MZ_DEFAULT_COMPRESSION);
  };
  add("mimetype", "application/epub+zip");
  add("META-INF/container.xml",
      "<?xml version=\"1.0\"?><container><rootfiles>"
      "<rootfile full-path=\"OEBPS/content.opf\" media-type=\"application/oebps-package+xml\"/>"
      "</rootfiles></container>");
  add("OEBPS/content.opf",
      "<?xml version=\"1.0\"?><package><metadata><dc:title>T</dc:title></metadata>"
      "<manifest>"
      "<item id=\"c1\" href=\"ch1.xhtml\" media-type=\"application/xhtml+xml\"/>"
      "<item id=\"c2\" href=\"ch2.xhtml\" media-type=\"application/xhtml+xml\"/>"
      "<item id=\"ncx\" href=\"toc.ncx\" media-type=\"application/x-dtbncx+xml\"/>"
      "</manifest><spine toc=\"ncx\">"
      "<itemref idref=\"c1\"/><itemref idref=\"c2\"/></spine></package>");
  add("OEBPS/ch1.xhtml", "<html><body><p>x</p></body></html>");
  add("OEBPS/ch2.xhtml", "<html><body><p>y</p></body></html>");
  add("OEBPS/toc.ncx",
      "<ncx><navMap>"
      "<navPoint><content src=\"ch1.xhtml\"/></navPoint>"
      "<navPoint><navLabel><text>Second</text></navLabel>"
      "<content src=\"ch2.xhtml\"/></navPoint>"
      "</navMap></ncx>");
  void *buf = nullptr; size_t size = 0;
  mz_zip_writer_finalize_heap_archive(&z, &buf, &size);
  std::string zipData((char *)buf, size);
  mz_zip_writer_end(&z); mz_free(buf);

  Ink::EpubBook book;
  CHECK(book.open((const uint8_t *)zipData.data(), zipData.size()));
  CHECK(book.toc().size() == 2);
  CHECK(book.toc()[0].title == "" && book.toc()[0].spineIndex == 0);
  CHECK(book.toc()[1].title == "Second" && book.toc()[1].spineIndex == 1);
  return 0;
}

// Spec-review round-2 fix 2: a spine listing the same href twice must
// resolve a TOC entry pointing at it to the FIRST occurrence, matching the
// old linear left-to-right scan's semantics (hrefToSpineIndex_ now uses
// emplace(), not operator[], to preserve that under a map).
static int testEpubDuplicateSpineHrefResolvesToFirst() {
  mz_zip_archive z; memset(&z, 0, sizeof(z));
  mz_zip_writer_init_heap(&z, 0, 16 * 1024);
  auto add = [&](const char *name, const std::string &data) {
    mz_zip_writer_add_mem(&z, name, data.data(), data.size(), MZ_DEFAULT_COMPRESSION);
  };
  add("mimetype", "application/epub+zip");
  add("META-INF/container.xml",
      "<?xml version=\"1.0\"?><container><rootfiles>"
      "<rootfile full-path=\"OEBPS/content.opf\" media-type=\"application/oebps-package+xml\"/>"
      "</rootfiles></container>");
  add("OEBPS/content.opf",
      "<?xml version=\"1.0\"?><package><metadata><dc:title>T</dc:title></metadata>"
      "<manifest>"
      "<item id=\"c1\" href=\"ch1.xhtml\" media-type=\"application/xhtml+xml\"/>"
      "<item id=\"c1dup\" href=\"ch1.xhtml\" media-type=\"application/xhtml+xml\"/>"
      "<item id=\"nav\" href=\"nav.xhtml\" media-type=\"application/xhtml+xml\" properties=\"nav\"/>"
      "</manifest><spine>"
      "<itemref idref=\"c1\"/><itemref idref=\"c1dup\"/></spine></package>");
  add("OEBPS/ch1.xhtml", "<html><body><p>x</p></body></html>");
  add("OEBPS/nav.xhtml",
      "<html><body><nav epub:type=\"toc\"><ol>"
      "<li><a href=\"ch1.xhtml\">Duplicate Target</a></li></ol></nav></body></html>");
  void *buf = nullptr; size_t size = 0;
  mz_zip_writer_finalize_heap_archive(&z, &buf, &size);
  std::string zipData((char *)buf, size);
  mz_zip_writer_end(&z); mz_free(buf);

  Ink::EpubBook book;
  CHECK(book.open((const uint8_t *)zipData.data(), zipData.size()));
  CHECK(book.chapterCount() == 2);  // both itemrefs kept in the spine
  CHECK(book.toc().size() == 1);
  CHECK(book.toc()[0].spineIndex == 0);  // first occurrence, not the second
  return 0;
}

// Builds an in-memory EPUB with `items` manifest/spine entries and returns
// how long EpubBook::open() took, in milliseconds (-1.0 if open() failed).
static double timeEpubOpenWithManifestSize(int items, size_t *chapterCountOut) {
  mz_zip_archive z; memset(&z, 0, sizeof(z));
  mz_zip_writer_init_heap(&z, 0, 512 * 1024);
  auto add = [&](const std::string &name, const std::string &data) {
    mz_zip_writer_add_mem(&z, name.c_str(), data.data(), data.size(), MZ_DEFAULT_COMPRESSION);
  };
  add("mimetype", "application/epub+zip");
  add("META-INF/container.xml",
      "<?xml version=\"1.0\"?><container><rootfiles>"
      "<rootfile full-path=\"OEBPS/content.opf\" media-type=\"application/oebps-package+xml\"/>"
      "</rootfiles></container>");

  std::string opf =
      "<?xml version=\"1.0\"?><package><metadata>"
      "<dc:title>Big Book</dc:title><dc:creator>Author</dc:creator>"
      "</metadata><manifest>";
  for (int i = 0; i < items; ++i) {
    opf += "<item id=\"c" + std::to_string(i) + "\" href=\"ch" + std::to_string(i) +
           ".xhtml\" media-type=\"application/xhtml+xml\"/>";
  }
  opf += "</manifest><spine>";
  for (int i = 0; i < items; ++i) {
    opf += "<itemref idref=\"c" + std::to_string(i) + "\"/>";
  }
  opf += "</spine></package>";
  add("OEBPS/content.opf", opf);
  for (int i = 0; i < items; ++i) {
    add("OEBPS/ch" + std::to_string(i) + ".xhtml", "<html><body><p>x</p></body></html>");
  }
  void *buf = nullptr; size_t size = 0;
  mz_zip_writer_finalize_heap_archive(&z, &buf, &size);
  std::string zipData((char *)buf, size);
  mz_zip_writer_end(&z); mz_free(buf);

  Ink::EpubBook book;
  auto t0 = std::chrono::steady_clock::now();
  bool ok = book.open((const uint8_t *)zipData.data(), zipData.size());
  auto t1 = std::chrono::steady_clock::now();
  double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
  if (chapterCountOut) *chapterCountOut = ok ? book.chapterCount() : 0;
  return ok ? ms : -1.0;
}

// Perf guard: a SCALING assertion rather than an absolute wall-clock bar
// (flaky across machines/CI, and too loose to actually catch a regression
// once it stops being egregious). Times open() at 750 and 1500 manifest
// items -- linear cost roughly doubles across that jump, quadratic cost
// roughly quadruples; +5.0ms absorbs timer noise at these tiny absolute
// values (single-digit milliseconds). This is exactly the tripwire that
// would catch a reintroduced O(n^2) in attrInSpan or spineIndexForHref.
static int testEpubPerfScalesLinearlyNotQuadratically() {
  size_t count750 = 0, count1500 = 0;
  double ms750 = timeEpubOpenWithManifestSize(750, &count750);
  double ms1500 = timeEpubOpenWithManifestSize(1500, &count1500);
  std::fprintf(stderr, "perf epub manifest scaling: 750 items=%.2f ms, 1500 items=%.2f ms\n",
               ms750, ms1500);
  CHECK(ms750 >= 0.0);
  CHECK(ms1500 >= 0.0);
  CHECK(count750 == 750);
  CHECK(count1500 == 1500);
  CHECK(ms1500 < ms750 * 4.0 + 5.0);
  return 0;
}

int main() {
  if (testTxtParagraphs()) return 1;
  if (testTxtEdges()) return 1;
  if (testTxtLeadingSpaces()) return 1;
  if (testStyleFor()) return 1;
  if (testMdHeadingsAndRule()) return 1;
  if (testMdInlineStyles()) return 1;
  if (testMdListQuoteCode()) return 1;
  if (testMdEdges()) return 1;
  if (testMdZeroRunGuard()) return 1;
  if (testMdEmphasisFlanking()) return 1;
  if (testMdTabListIndent()) return 1;
  if (testMdLoneCr()) return 1;
  if (testMdLinkInheritsEmphasis()) return 1;
  if (testMdSrcOffsets()) return 1;
  if (testMdNestedBracketFirstOccurrence()) return 1;
  if (testXhtmlBasics()) return 1;
  if (testXhtmlListsPreEntities()) return 1;
  if (testXhtmlDegrade()) return 1;
  if (testXhtmlDecodeEntitiesStandalone()) return 1;
  if (testXhtmlNestedBoldDepth()) return 1;
  if (testXhtmlScriptStyleDrop()) return 1;
  if (testXhtmlStrayClose()) return 1;
  if (testXhtmlNumericEntityUtf8()) return 1;
  if (testXhtmlNumericEntityOverflow()) return 1;
  if (testXhtmlSrcOffsets()) return 1;
  if (testXhtmlScriptRawTextSurvivesAfter()) return 1;
  if (testXhtmlStyleRawTextSurvivesAfter()) return 1;
  if (testXhtmlScriptNestedMarkupInString()) return 1;
  if (testXhtmlBareLtLiteral()) return 1;
  if (testXhtmlBareLtBeforeDigit()) return 1;
  if (testXhtmlPhantomBlockDropped()) return 1;
  if (testXhtmlNestedDivMerges()) return 1;
  if (testXhtmlMinifiedBlockquoteJoins()) return 1;
  if (testXhtmlCommentDropped()) return 1;
  if (testXhtmlDoctypeDropped()) return 1;
  if (testXhtmlQuotedAttributeWithGt()) return 1;
  if (testXhtmlNulEntitySuppressed()) return 1;
  if (testXhtmlPerfBareLt()) return 1;
  if (testXhtmlPerfScriptRepeat()) return 1;
  if (testXhtmlClosingHeadingFlushesNotGlues()) return 1;
  if (testXhtmlClosingParagraphFlushesNotGlues()) return 1;
  if (testXhtmlNumericEntityOverflow32BitHex()) return 1;
  if (testXhtmlNumericEntityOverflow32BitDecimal()) return 1;
  if (testXhtmlTableCellsJoinWithSpace()) return 1;
  if (testXhtmlBodyTagQuotedAttrWithGt()) return 1;
  if (testXhtmlBodyMentionInCommentIgnored()) return 1;
  if (testXhtmlEmptyInput()) return 1;
  if (testXhtmlNoBodyParsesWholeDoc()) return 1;
  if (testXhtmlListDepthCapsAtThree()) return 1;
  if (testXhtmlBrProducesSpace()) return 1;
  if (testXhtmlUnterminatedTagAtEofIsLiteral()) return 1;
  if (testEpubOpen(true)) return 1;
  if (testEpubOpen(false)) return 1;
  if (testEpubMalformed()) return 1;
  if (testEpubNavTocEntityDecodedTitle()) return 1;
  if (testEpubOpfNamespacePrefixes()) return 1;
  if (testEpubCoverViaProperties()) return 1;
  if (testEpubTocEntryUnresolvedHrefKept()) return 1;
  if (testEpubChapterXhtmlOutOfRange()) return 1;
  if (testEpubChapterSizePositive()) return 1;
  if (testEpubAttributedMetadataTags()) return 1;
  if (testEpubNcxNestedNavPointFlattened()) return 1;
  if (testEpubNcxNavPointMissingContent()) return 1;
  if (testEpubNcxNavPointMissingLabel()) return 1;
  if (testEpubDuplicateSpineHrefResolvesToFirst()) return 1;
  if (testEpubPerfScalesLinearlyNotQuadratically()) return 1;
  std::printf("inkwell host tests: %d checks passed\n", checks);
  return 0;
}
