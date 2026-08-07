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
#include "../src/Paginator.h"
#include "../src/InkBook.h"
#include "../src/SampleBooks.h"
#include <vector>

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

// Wraps miniz's zip-writer boilerplate (init/add/finalize/end/free) that
// used to be copy-pasted at the top of every fixture builder below (M5).
struct ZipBuilder {
  mz_zip_archive z{};
  explicit ZipBuilder(size_t initialAlloc = 16 * 1024) {
    mz_zip_writer_init_heap(&z, 0, initialAlloc);
  }
  void add(const std::string &name, const std::string &data,
           mz_uint levelAndFlags = MZ_DEFAULT_COMPRESSION) {
    mz_zip_writer_add_mem(&z, name.c_str(), data.data(), data.size(), levelAndFlags);
  }
  // Fabricates an entry whose STATED uncompressed size (fakeUncompSize) is
  // larger than the bytes actually stored (rawBytes) -- used by the
  // oversized-entry test below. rawBytes need not be valid deflate output:
  // MZ_ZIP_FLAG_COMPRESSED_DATA tells miniz to store it verbatim as the
  // entry's "already compressed" payload, trusting the given size/crc
  // metadata, and the test never asks EpubBook to actually decompress it
  // (readEntry must reject it on the stat check alone, before extracting).
  void addFakeSized(const std::string &name, const std::string &rawBytes,
                     uint64_t fakeUncompSize) {
    mz_zip_writer_add_mem_ex(&z, name.c_str(), rawBytes.data(), rawBytes.size(), nullptr, 0,
                              MZ_ZIP_FLAG_COMPRESSED_DATA, fakeUncompSize, 0);
  }
  std::string finalize() {
    void *buf = nullptr;
    size_t size = 0;
    mz_zip_writer_finalize_heap_archive(&z, &buf, &size);
    std::string out((char *)buf, size);
    mz_zip_writer_end(&z);
    mz_free(buf);
    return out;
  }
};

// Builds a minimal but structurally honest EPUB in memory.
static std::string buildFixtureEpub(bool navToc) {
  ZipBuilder zb;
  zb.add("mimetype", "application/epub+zip");
  zb.add("META-INF/container.xml",
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
  zb.add("OEBPS/content.opf", opf);
  zb.add("OEBPS/ch1.xhtml", "<html><body><h1>One</h1><p>First chapter text.</p></body></html>");
  zb.add("OEBPS/ch2.xhtml", "<html><body><h1>Two</h1><p>Second chapter text.</p></body></html>");
  zb.add("OEBPS/cover.jpg", std::string("\xFF\xD8\xFF\xE0 fake jpeg", 14));
  if (navToc)
    zb.add("OEBPS/nav.xhtml",
           "<html><body><nav epub:type=\"toc\"><ol>"
           "<li><a href=\"ch1.xhtml\">Chapter One</a></li>"
           "<li><a href=\"ch2.xhtml#frag\">Chapter Two</a></li></ol></nav></body></html>");
  else
    zb.add("OEBPS/toc.ncx",
           "<ncx><navMap><navPoint><navLabel><text>Chapter One</text></navLabel>"
           "<content src=\"ch1.xhtml\"/></navPoint><navPoint><navLabel>"
           "<text>Chapter Two</text></navLabel><content src=\"ch2.xhtml\"/>"
           "</navPoint></navMap></ncx>");
  return zb.finalize();
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
  ZipBuilder zb(1024);
  zb.add("hello.txt", "hi", 0);
  std::string buf = zb.finalize();
  CHECK(!b3.open((const uint8_t *)buf.data(), buf.size()));
  return 0;
}

// TOC titles run through Ink::decodeEntities -- "A &amp; B" must come back
// as "A & B", not the raw escaped text.
static int testEpubNavTocEntityDecodedTitle() {
  ZipBuilder zb;
  zb.add("mimetype", "application/epub+zip");
  zb.add("META-INF/container.xml",
         "<?xml version=\"1.0\"?><container><rootfiles>"
         "<rootfile full-path=\"OEBPS/content.opf\" media-type=\"application/oebps-package+xml\"/>"
         "</rootfiles></container>");
  zb.add("OEBPS/content.opf",
         "<?xml version=\"1.0\"?><package><metadata>"
         "<dc:title>T</dc:title></metadata><manifest>"
         "<item id=\"c1\" href=\"ch1.xhtml\" media-type=\"application/xhtml+xml\"/>"
         "<item id=\"nav\" href=\"nav.xhtml\" media-type=\"application/xhtml+xml\" properties=\"nav\"/>"
         "</manifest><spine><itemref idref=\"c1\"/></spine></package>");
  zb.add("OEBPS/ch1.xhtml", "<html><body><p>x</p></body></html>");
  zb.add("OEBPS/nav.xhtml",
         "<html><body><nav epub:type=\"toc\"><ol>"
         "<li><a href=\"ch1.xhtml\">A &amp; B</a></li></ol></nav></body></html>");
  std::string zipData = zb.finalize();

  Ink::EpubBook book;
  CHECK(book.open((const uint8_t *)zipData.data(), zipData.size()));
  CHECK(book.toc().size() == 1);
  CHECK(book.toc()[0].title == "A & B");
  return 0;
}

// A namespace-prefixed OPF root (<opf:package>) and dc: elements must still
// resolve title/creator/manifest/spine normally.
static int testEpubOpfNamespacePrefixes() {
  ZipBuilder zb;
  zb.add("mimetype", "application/epub+zip");
  zb.add("META-INF/container.xml",
         "<?xml version=\"1.0\"?><container><rootfiles>"
         "<rootfile full-path=\"OEBPS/content.opf\" media-type=\"application/oebps-package+xml\"/>"
         "</rootfiles></container>");
  zb.add("OEBPS/content.opf",
         "<?xml version=\"1.0\"?><opf:package xmlns:opf=\"x\"><opf:metadata>"
         "<dc:title>Prefixed Title</dc:title><dc:creator>Prefixed Author</dc:creator>"
         "</opf:metadata><opf:manifest>"
         "<item id=\"c1\" href=\"ch1.xhtml\" media-type=\"application/xhtml+xml\"/>"
         "</opf:manifest><opf:spine><itemref idref=\"c1\"/></opf:spine></opf:package>");
  zb.add("OEBPS/ch1.xhtml", "<html><body><p>x</p></body></html>");
  std::string zipData = zb.finalize();

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
  ZipBuilder zb;
  zb.add("mimetype", "application/epub+zip");
  zb.add("META-INF/container.xml",
         "<?xml version=\"1.0\"?><container><rootfiles>"
         "<rootfile full-path=\"OEBPS/content.opf\" media-type=\"application/oebps-package+xml\"/>"
         "</rootfiles></container>");
  zb.add("OEBPS/content.opf",
         "<?xml version=\"1.0\"?><package><metadata><dc:title>T</dc:title></metadata>"
         "<manifest>"
         "<item id=\"c1\" href=\"ch1.xhtml\" media-type=\"application/xhtml+xml\"/>"
         "<item id=\"cov\" href=\"cover.png\" media-type=\"image/png\" properties=\"cover-image\"/>"
         "</manifest><spine><itemref idref=\"c1\"/></spine></package>");
  zb.add("OEBPS/ch1.xhtml", "<html><body><p>x</p></body></html>");
  zb.add("OEBPS/cover.png", std::string("\x89PNG fake", 9));
  std::string zipData = zb.finalize();

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
  ZipBuilder zb;
  zb.add("mimetype", "application/epub+zip");
  zb.add("META-INF/container.xml",
         "<?xml version=\"1.0\"?><container><rootfiles>"
         "<rootfile full-path=\"OEBPS/content.opf\" media-type=\"application/oebps-package+xml\"/>"
         "</rootfiles></container>");
  zb.add("OEBPS/content.opf",
         "<?xml version=\"1.0\"?><package><metadata><dc:title>T</dc:title></metadata>"
         "<manifest>"
         "<item id=\"c1\" href=\"ch1.xhtml\" media-type=\"application/xhtml+xml\"/>"
         "<item id=\"nav\" href=\"nav.xhtml\" media-type=\"application/xhtml+xml\" properties=\"nav\"/>"
         "</manifest><spine><itemref idref=\"c1\"/></spine></package>");
  zb.add("OEBPS/ch1.xhtml", "<html><body><p>x</p></body></html>");
  zb.add("OEBPS/nav.xhtml",
         "<html><body><nav epub:type=\"toc\"><ol>"
         "<li><a href=\"ch1.xhtml\">Real</a></li>"
         "<li><a href=\"nowhere.xhtml\">Ghost</a></li></ol></nav></body></html>");
  std::string zipData = zb.finalize();

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
  ZipBuilder zb;
  zb.add("mimetype", "application/epub+zip");
  zb.add("META-INF/container.xml",
         "<?xml version=\"1.0\"?><container><rootfiles>"
         "<rootfile full-path=\"OEBPS/content.opf\" media-type=\"application/oebps-package+xml\"/>"
         "</rootfiles></container>");
  zb.add("OEBPS/content.opf",
         "<?xml version=\"1.0\"?><package><metadata>"
         "<dc:title id=\"t1\">Attributed Title</dc:title>"
         "<dc:creator opf:role=\"aut\">A. Author</dc:creator>"
         "</metadata><manifest>"
         "<item id=\"c1\" href=\"ch1.xhtml\" media-type=\"application/xhtml+xml\"/>"
         "</manifest><spine><itemref idref=\"c1\"/></spine></package>");
  zb.add("OEBPS/ch1.xhtml", "<html><body><p>x</p></body></html>");
  std::string zipData = zb.finalize();

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
  ZipBuilder zb;
  zb.add("mimetype", "application/epub+zip");
  zb.add("META-INF/container.xml",
         "<?xml version=\"1.0\"?><container><rootfiles>"
         "<rootfile full-path=\"OEBPS/content.opf\" media-type=\"application/oebps-package+xml\"/>"
         "</rootfiles></container>");
  zb.add("OEBPS/content.opf",
         "<?xml version=\"1.0\"?><package><metadata><dc:title>T</dc:title></metadata>"
         "<manifest>"
         "<item id=\"c1\" href=\"ch1.xhtml\" media-type=\"application/xhtml+xml\"/>"
         "<item id=\"c2\" href=\"ch2.xhtml\" media-type=\"application/xhtml+xml\"/>"
         "<item id=\"ncx\" href=\"toc.ncx\" media-type=\"application/x-dtbncx+xml\"/>"
         "</manifest><spine toc=\"ncx\">"
         "<itemref idref=\"c1\"/><itemref idref=\"c2\"/></spine></package>");
  zb.add("OEBPS/ch1.xhtml", "<html><body><p>x</p></body></html>");
  zb.add("OEBPS/ch2.xhtml", "<html><body><p>y</p></body></html>");
  zb.add("OEBPS/toc.ncx",
         "<ncx><navMap><navPoint><navLabel><text>Parent</text></navLabel>"
         "<content src=\"ch1.xhtml\"/>"
         "<navPoint><navLabel><text>Child</text></navLabel>"
         "<content src=\"ch2.xhtml\"/></navPoint>"
         "</navPoint></navMap></ncx>");
  std::string zipData = zb.finalize();

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
  ZipBuilder zb;
  zb.add("mimetype", "application/epub+zip");
  zb.add("META-INF/container.xml",
         "<?xml version=\"1.0\"?><container><rootfiles>"
         "<rootfile full-path=\"OEBPS/content.opf\" media-type=\"application/oebps-package+xml\"/>"
         "</rootfiles></container>");
  zb.add("OEBPS/content.opf",
         "<?xml version=\"1.0\"?><package><metadata><dc:title>T</dc:title></metadata>"
         "<manifest>"
         "<item id=\"c1\" href=\"ch1.xhtml\" media-type=\"application/xhtml+xml\"/>"
         "<item id=\"c2\" href=\"ch2.xhtml\" media-type=\"application/xhtml+xml\"/>"
         "<item id=\"ncx\" href=\"toc.ncx\" media-type=\"application/x-dtbncx+xml\"/>"
         "</manifest><spine toc=\"ncx\">"
         "<itemref idref=\"c1\"/><itemref idref=\"c2\"/></spine></package>");
  zb.add("OEBPS/ch1.xhtml", "<html><body><p>x</p></body></html>");
  zb.add("OEBPS/ch2.xhtml", "<html><body><p>y</p></body></html>");
  zb.add("OEBPS/toc.ncx",
         "<ncx><navMap>"
         "<navPoint><navLabel><text>NoContent</text></navLabel></navPoint>"
         "<navPoint><navLabel><text>Second</text></navLabel>"
         "<content src=\"ch2.xhtml\"/></navPoint>"
         "</navMap></ncx>");
  std::string zipData = zb.finalize();

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
  ZipBuilder zb;
  zb.add("mimetype", "application/epub+zip");
  zb.add("META-INF/container.xml",
         "<?xml version=\"1.0\"?><container><rootfiles>"
         "<rootfile full-path=\"OEBPS/content.opf\" media-type=\"application/oebps-package+xml\"/>"
         "</rootfiles></container>");
  zb.add("OEBPS/content.opf",
         "<?xml version=\"1.0\"?><package><metadata><dc:title>T</dc:title></metadata>"
         "<manifest>"
         "<item id=\"c1\" href=\"ch1.xhtml\" media-type=\"application/xhtml+xml\"/>"
         "<item id=\"c2\" href=\"ch2.xhtml\" media-type=\"application/xhtml+xml\"/>"
         "<item id=\"ncx\" href=\"toc.ncx\" media-type=\"application/x-dtbncx+xml\"/>"
         "</manifest><spine toc=\"ncx\">"
         "<itemref idref=\"c1\"/><itemref idref=\"c2\"/></spine></package>");
  zb.add("OEBPS/ch1.xhtml", "<html><body><p>x</p></body></html>");
  zb.add("OEBPS/ch2.xhtml", "<html><body><p>y</p></body></html>");
  zb.add("OEBPS/toc.ncx",
         "<ncx><navMap>"
         "<navPoint><content src=\"ch1.xhtml\"/></navPoint>"
         "<navPoint><navLabel><text>Second</text></navLabel>"
         "<content src=\"ch2.xhtml\"/></navPoint>"
         "</navMap></ncx>");
  std::string zipData = zb.finalize();

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
  ZipBuilder zb;
  zb.add("mimetype", "application/epub+zip");
  zb.add("META-INF/container.xml",
         "<?xml version=\"1.0\"?><container><rootfiles>"
         "<rootfile full-path=\"OEBPS/content.opf\" media-type=\"application/oebps-package+xml\"/>"
         "</rootfiles></container>");
  zb.add("OEBPS/content.opf",
         "<?xml version=\"1.0\"?><package><metadata><dc:title>T</dc:title></metadata>"
         "<manifest>"
         "<item id=\"c1\" href=\"ch1.xhtml\" media-type=\"application/xhtml+xml\"/>"
         "<item id=\"c1dup\" href=\"ch1.xhtml\" media-type=\"application/xhtml+xml\"/>"
         "<item id=\"nav\" href=\"nav.xhtml\" media-type=\"application/xhtml+xml\" properties=\"nav\"/>"
         "</manifest><spine>"
         "<itemref idref=\"c1\"/><itemref idref=\"c1dup\"/></spine></package>");
  zb.add("OEBPS/ch1.xhtml", "<html><body><p>x</p></body></html>");
  zb.add("OEBPS/nav.xhtml",
         "<html><body><nav epub:type=\"toc\"><ol>"
         "<li><a href=\"ch1.xhtml\">Duplicate Target</a></li></ol></nav></body></html>");
  std::string zipData = zb.finalize();

  Ink::EpubBook book;
  CHECK(book.open((const uint8_t *)zipData.data(), zipData.size()));
  CHECK(book.chapterCount() == 2);  // both itemrefs kept in the spine
  CHECK(book.toc().size() == 1);
  CHECK(book.toc()[0].spineIndex == 0);  // first occurrence, not the second
  return 0;
}

// I4: a raw '>' inside an EARLIER attribute's quoted value (title="a>b")
// must not truncate the perceived tag span via an unguarded find('>', ...)
// and lose the attributes that follow it -- previously this made an
// otherwise-normal item drop its href/media-type and the whole book
// resolve to 0 chapters.
static int testEpubAttributeValueWithGtDoesNotBreakTagSpan() {
  ZipBuilder zb;
  zb.add("mimetype", "application/epub+zip");
  zb.add("META-INF/container.xml",
         "<?xml version=\"1.0\"?><container><rootfiles>"
         "<rootfile full-path=\"OEBPS/content.opf\" media-type=\"application/oebps-package+xml\"/>"
         "</rootfiles></container>");
  zb.add("OEBPS/content.opf",
         "<?xml version=\"1.0\"?><package><metadata><dc:title>T</dc:title></metadata>"
         "<manifest>"
         "<item id=\"c1\" title=\"a>b\" href=\"ch1.xhtml\" "
         "media-type=\"application/xhtml+xml\"/>"
         "</manifest><spine><itemref idref=\"c1\"/></spine></package>");
  zb.add("OEBPS/ch1.xhtml", "<html><body><p>x</p></body></html>");
  std::string zipData = zb.finalize();

  Ink::EpubBook book;
  CHECK(book.open((const uint8_t *)zipData.data(), zipData.size()));
  CHECK(book.chapterCount() == 1);  // href/media-type must still resolve
  std::string x;
  CHECK(book.chapterXhtml(0, x));
  return 0;
}

// M1: a "data-href" attribute must not shadow a real "href" attribute --
// attrInSpan now requires a real attribute boundary (the preceding
// character must be whitespace) before accepting a match, so a substring
// hit inside another attribute's NAME can't be mistaken for the attribute
// itself.
static int testEpubDataHrefDoesNotShadowHref() {
  ZipBuilder zb;
  zb.add("mimetype", "application/epub+zip");
  zb.add("META-INF/container.xml",
         "<?xml version=\"1.0\"?><container><rootfiles>"
         "<rootfile full-path=\"OEBPS/content.opf\" media-type=\"application/oebps-package+xml\"/>"
         "</rootfiles></container>");
  zb.add("OEBPS/content.opf",
         "<?xml version=\"1.0\"?><package><metadata><dc:title>T</dc:title></metadata>"
         "<manifest>"
         "<item id=\"c1\" data-href=\"WRONG.xhtml\" href=\"ch1.xhtml\" "
         "media-type=\"application/xhtml+xml\"/>"
         "</manifest><spine><itemref idref=\"c1\"/></spine></package>");
  zb.add("OEBPS/ch1.xhtml", "<html><body><p>right chapter</p></body></html>");
  std::string zipData = zb.finalize();

  Ink::EpubBook book;
  CHECK(book.open((const uint8_t *)zipData.data(), zipData.size()));
  CHECK(book.chapterCount() == 1);
  std::string x;
  CHECK(book.chapterXhtml(0, x) && x.find("right chapter") != std::string::npos);
  return 0;
}

// I3: readEntry must stat an entry and reject anything past kMaxEntryBytes
// BEFORE attempting to allocate/extract it. open() itself must still
// succeed -- the oversized entry is just one chapter among several, not
// the container/OPF.
static int testEpubOversizedEntryRejectedByReadEntry() {
  ZipBuilder zb;
  zb.add("mimetype", "application/epub+zip");
  zb.add("META-INF/container.xml",
         "<?xml version=\"1.0\"?><container><rootfiles>"
         "<rootfile full-path=\"OEBPS/content.opf\" media-type=\"application/oebps-package+xml\"/>"
         "</rootfiles></container>");
  zb.add("OEBPS/content.opf",
         "<?xml version=\"1.0\"?><package><metadata><dc:title>T</dc:title></metadata>"
         "<manifest>"
         "<item id=\"c1\" href=\"ch1.xhtml\" media-type=\"application/xhtml+xml\"/>"
         "<item id=\"c2\" href=\"huge.xhtml\" media-type=\"application/xhtml+xml\"/>"
         "</manifest><spine><itemref idref=\"c1\"/><itemref idref=\"c2\"/></spine></package>");
  zb.add("OEBPS/ch1.xhtml", "<html><body><p>normal chapter</p></body></html>");
  // Claims a 20 MB uncompressed size (past the 16 MB cap) but stores only
  // a handful of bytes -- readEntry must reject it from the stat alone.
  zb.addFakeSized("OEBPS/huge.xhtml", "AAAA", 20ull * 1024 * 1024);
  std::string zipData = zb.finalize();

  Ink::EpubBook book;
  CHECK(book.open((const uint8_t *)zipData.data(), zipData.size()));
  CHECK(book.chapterCount() == 2);
  std::string x;
  CHECK(book.chapterXhtml(0, x) && x.find("normal chapter") != std::string::npos);
  CHECK(!book.chapterXhtml(1, x));  // oversized entry rejected
  // chapterSize() must agree with chapterXhtml()/readEntry(): 0, not a
  // clamped 16MB, so an unreadable oversized chapter can't eat most of a
  // book's progress bar (permille weights chapters by chapterSize()).
  CHECK(book.chapterSize(0) > 0);
  CHECK(book.chapterSize(1) == 0);
  return 0;
}

// M6: open -> close -> open reuse must fully reset state -- a second,
// different book opened on the same EpubBook instance must not see any
// leftover title/chapter/TOC data from the first.
static int testEpubOpenCloseOpenReuse() {
  ZipBuilder zbA;
  zbA.add("mimetype", "application/epub+zip");
  zbA.add("META-INF/container.xml",
          "<?xml version=\"1.0\"?><container><rootfiles>"
          "<rootfile full-path=\"OEBPS/content.opf\" media-type=\"application/oebps-package+xml\"/>"
          "</rootfiles></container>");
  zbA.add("OEBPS/content.opf",
          "<?xml version=\"1.0\"?><package><metadata><dc:title>Book A</dc:title></metadata>"
          "<manifest><item id=\"c1\" href=\"a1.xhtml\" media-type=\"application/xhtml+xml\"/>"
          "<item id=\"c2\" href=\"a2.xhtml\" media-type=\"application/xhtml+xml\"/></manifest>"
          "<spine><itemref idref=\"c1\"/><itemref idref=\"c2\"/></spine></package>");
  zbA.add("OEBPS/a1.xhtml", "<html><body><p>a1</p></body></html>");
  zbA.add("OEBPS/a2.xhtml", "<html><body><p>a2</p></body></html>");
  std::string zipA = zbA.finalize();

  ZipBuilder zbB;
  zbB.add("mimetype", "application/epub+zip");
  zbB.add("META-INF/container.xml",
          "<?xml version=\"1.0\"?><container><rootfiles>"
          "<rootfile full-path=\"OEBPS/content.opf\" media-type=\"application/oebps-package+xml\"/>"
          "</rootfiles></container>");
  zbB.add("OEBPS/content.opf",
          "<?xml version=\"1.0\"?><package><metadata><dc:title>Book B</dc:title></metadata>"
          "<manifest><item id=\"c1\" href=\"b1.xhtml\" media-type=\"application/xhtml+xml\"/></manifest>"
          "<spine><itemref idref=\"c1\"/></spine></package>");
  zbB.add("OEBPS/b1.xhtml", "<html><body><p>b1</p></body></html>");
  std::string zipB = zbB.finalize();

  Ink::EpubBook book;
  CHECK(book.open((const uint8_t *)zipA.data(), zipA.size()));
  CHECK(book.title() == "Book A");
  CHECK(book.chapterCount() == 2);
  book.close();
  CHECK(book.title().empty());
  CHECK(book.chapterCount() == 0);

  CHECK(book.open((const uint8_t *)zipB.data(), zipB.size()));
  CHECK(book.title() == "Book B");
  CHECK(book.chapterCount() == 1);
  std::string x;
  CHECK(book.chapterXhtml(0, x) && x.find("b1") != std::string::npos);
  return 0;
}

// M6: calling open() again while already open (no explicit close() first)
// must behave exactly like open->close->open -- open()'s internal close()
// at the top must fully tear down the first book before the second one is
// parsed.
static int testEpubReopenWhileOpen() {
  ZipBuilder zb1;
  zb1.add("mimetype", "application/epub+zip");
  zb1.add("META-INF/container.xml",
          "<?xml version=\"1.0\"?><container><rootfiles>"
          "<rootfile full-path=\"OEBPS/content.opf\" media-type=\"application/oebps-package+xml\"/>"
          "</rootfiles></container>");
  zb1.add("OEBPS/content.opf",
          "<?xml version=\"1.0\"?><package><metadata><dc:title>First</dc:title></metadata>"
          "<manifest><item id=\"c1\" href=\"f1.xhtml\" media-type=\"application/xhtml+xml\"/></manifest>"
          "<spine><itemref idref=\"c1\"/></spine></package>");
  zb1.add("OEBPS/f1.xhtml", "<html><body><p>first</p></body></html>");
  std::string zip1 = zb1.finalize();

  ZipBuilder zb2;
  zb2.add("mimetype", "application/epub+zip");
  zb2.add("META-INF/container.xml",
          "<?xml version=\"1.0\"?><container><rootfiles>"
          "<rootfile full-path=\"OEBPS/content.opf\" media-type=\"application/oebps-package+xml\"/>"
          "</rootfiles></container>");
  zb2.add("OEBPS/content.opf",
          "<?xml version=\"1.0\"?><package><metadata><dc:title>Second</dc:title></metadata>"
          "<manifest><item id=\"c1\" href=\"s1.xhtml\" media-type=\"application/xhtml+xml\"/>"
          "<item id=\"c2\" href=\"s2.xhtml\" media-type=\"application/xhtml+xml\"/></manifest>"
          "<spine><itemref idref=\"c1\"/><itemref idref=\"c2\"/></spine></package>");
  zb2.add("OEBPS/s1.xhtml", "<html><body><p>s1</p></body></html>");
  zb2.add("OEBPS/s2.xhtml", "<html><body><p>s2</p></body></html>");
  std::string zip2 = zb2.finalize();

  Ink::EpubBook book;
  CHECK(book.open((const uint8_t *)zip1.data(), zip1.size()));
  CHECK(book.title() == "First");
  // No explicit close() -- open() must tear down the first book itself.
  CHECK(book.open((const uint8_t *)zip2.data(), zip2.size()));
  CHECK(book.title() == "Second");
  CHECK(book.chapterCount() == 2);
  std::string x;
  CHECK(book.chapterXhtml(1, x) && x.find("s2") != std::string::npos);
  return 0;
}

// M6: container.xml pointing straight at the archive root ("content.opf",
// no directory) must leave opfDir_ empty and still resolve chapter hrefs.
static int testEpubOpfAtArchiveRoot() {
  ZipBuilder zb;
  zb.add("mimetype", "application/epub+zip");
  zb.add("META-INF/container.xml",
         "<?xml version=\"1.0\"?><container><rootfiles>"
         "<rootfile full-path=\"content.opf\" media-type=\"application/oebps-package+xml\"/>"
         "</rootfiles></container>");
  zb.add("content.opf",
         "<?xml version=\"1.0\"?><package><metadata><dc:title>Root Book</dc:title></metadata>"
         "<manifest><item id=\"c1\" href=\"ch1.xhtml\" media-type=\"application/xhtml+xml\"/></manifest>"
         "<spine><itemref idref=\"c1\"/></spine></package>");
  zb.add("ch1.xhtml", "<html><body><p>root chapter</p></body></html>");
  std::string zipData = zb.finalize();

  Ink::EpubBook book;
  CHECK(book.open((const uint8_t *)zipData.data(), zipData.size()));
  CHECK(book.title() == "Root Book");
  CHECK(book.chapterCount() == 1);
  std::string x;
  CHECK(book.chapterXhtml(0, x) && x.find("root chapter") != std::string::npos);
  return 0;
}

// M6: a spine with zero itemrefs must still open successfully, with a
// zero chapter count rather than false.
static int testEpubEmptySpine() {
  ZipBuilder zb;
  zb.add("mimetype", "application/epub+zip");
  zb.add("META-INF/container.xml",
         "<?xml version=\"1.0\"?><container><rootfiles>"
         "<rootfile full-path=\"OEBPS/content.opf\" media-type=\"application/oebps-package+xml\"/>"
         "</rootfiles></container>");
  zb.add("OEBPS/content.opf",
         "<?xml version=\"1.0\"?><package><metadata><dc:title>Empty Spine</dc:title></metadata>"
         "<manifest><item id=\"c1\" href=\"ch1.xhtml\" media-type=\"application/xhtml+xml\"/></manifest>"
         "<spine></spine></package>");
  zb.add("OEBPS/ch1.xhtml", "<html><body><p>never referenced</p></body></html>");
  std::string zipData = zb.finalize();

  Ink::EpubBook book;
  CHECK(book.open((const uint8_t *)zipData.data(), zipData.size()));
  CHECK(book.title() == "Empty Spine");
  CHECK(book.chapterCount() == 0);
  return 0;
}

// Builds an in-memory EPUB with `items` manifest/spine entries and returns
// how long EpubBook::open() took, in milliseconds (-1.0 if open() failed).
static double timeEpubOpenWithManifestSize(int items, size_t *chapterCountOut) {
  ZipBuilder zb(512 * 1024);
  zb.add("mimetype", "application/epub+zip");
  zb.add("META-INF/container.xml",
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
  zb.add("OEBPS/content.opf", opf);
  for (int i = 0; i < items; ++i) {
    zb.add("OEBPS/ch" + std::to_string(i) + ".xhtml", "<html><body><p>x</p></body></html>");
  }
  std::string zipData = zb.finalize();

  // Minimum of several timed open() calls on the SAME fixture -- a
  // scheduling hiccup (a stray context switch, a page fault) can spike
  // any one sample well above the algorithm's true cost; taking the best
  // of a few filters that out while still catching a genuine algorithmic
  // regression, which is consistently slow on every sample, not just one.
  double best = -1.0;
  size_t lastCount = 0;
  for (int trial = 0; trial < 5; ++trial) {
    Ink::EpubBook book;
    auto t0 = std::chrono::steady_clock::now();
    bool ok = book.open((const uint8_t *)zipData.data(), zipData.size());
    auto t1 = std::chrono::steady_clock::now();
    if (!ok) return -1.0;
    lastCount = book.chapterCount();
    double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    if (best < 0.0 || ms < best) best = ms;
  }
  if (chapterCountOut) *chapterCountOut = lastCount;
  return best;
}

// I1: a SCALING assertion rather than an absolute wall-clock bar (flaky
// across machines/CI). Times open() at 3000 and 6000 manifest items --
// large enough that timer noise is negligible relative to the measured
// cost, so the ratio check needs no additive fudge term. Linear cost
// should roughly double across that jump; this fires on anything worse
// than 3.2x: true ratio measures ~2.05, a reintroduced O(n^2) ~3.7-4.0, and
// the min-of-5 sampling keeps one-sided scheduler noise from pushing a
// linear run over the bar (observed flake: a lucky-fast small-N min).
static int testEpubPerfScalesLinearlyNotQuadratically() {
  size_t count3000 = 0, count6000 = 0;
  double ms3000 = timeEpubOpenWithManifestSize(3000, &count3000);
  double ms6000 = timeEpubOpenWithManifestSize(6000, &count6000);
  std::fprintf(stderr, "perf epub manifest scaling: 3000 items=%.2f ms, 6000 items=%.2f ms\n",
               ms3000, ms6000);
  CHECK(ms3000 >= 0.0);
  CHECK(ms6000 >= 0.0);
  CHECK(count3000 == 3000);
  CHECK(count6000 == 6000);
  CHECK(ms6000 < ms3000 * 3.2);
  return 0;
}

// Builds an in-memory EPUB with `n` chapters and an NCX table of contents
// whose `n` navPoints are all LABEL-LESS (a <content> but no <navLabel>).
// Returns how long EpubBook::open() took, in milliseconds (-1.0 if open()
// failed); *tocCountOut receives toc().size().
static double timeNcxOpenWithLabellessNavPoints(int n, size_t *tocCountOut) {
  ZipBuilder zb(512 * 1024);
  zb.add("mimetype", "application/epub+zip");
  zb.add("META-INF/container.xml",
         "<?xml version=\"1.0\"?><container><rootfiles>"
         "<rootfile full-path=\"OEBPS/content.opf\" media-type=\"application/oebps-package+xml\"/>"
         "</rootfiles></container>");

  std::string opf =
      "<?xml version=\"1.0\"?><package><metadata><dc:title>Big NCX</dc:title></metadata>"
      "<manifest>";
  for (int i = 0; i < n; ++i) {
    opf += "<item id=\"c" + std::to_string(i) + "\" href=\"ch" + std::to_string(i) +
           ".xhtml\" media-type=\"application/xhtml+xml\"/>";
  }
  opf += "<item id=\"ncx\" href=\"toc.ncx\" media-type=\"application/x-dtbncx+xml\"/>";
  opf += "</manifest><spine toc=\"ncx\">";
  for (int i = 0; i < n; ++i) opf += "<itemref idref=\"c" + std::to_string(i) + "\"/>";
  opf += "</spine></package>";
  zb.add("OEBPS/content.opf", opf);
  for (int i = 0; i < n; ++i) {
    zb.add("OEBPS/ch" + std::to_string(i) + ".xhtml", "<html><body><p>x</p></body></html>");
  }

  std::string ncx = "<ncx><navMap>";
  for (int i = 0; i < n; ++i) {
    ncx += "<navPoint><content src=\"ch" + std::to_string(i) + ".xhtml\"/></navPoint>";
  }
  ncx += "</navMap></ncx>";
  zb.add("OEBPS/toc.ncx", ncx);
  std::string zipData = zb.finalize();

  // Minimum of several timed open() calls on the SAME fixture -- see the
  // comment in timeEpubOpenWithManifestSize() for why.
  double best = -1.0;
  size_t lastTocCount = 0;
  for (int trial = 0; trial < 5; ++trial) {
    Ink::EpubBook book;
    auto t0 = std::chrono::steady_clock::now();
    bool ok = book.open((const uint8_t *)zipData.data(), zipData.size());
    auto t1 = std::chrono::steady_clock::now();
    if (!ok) return -1.0;
    lastTocCount = book.toc().size();
    double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    if (best < 0.0 || ms < best) best = ms;
  }
  if (tocCountOut) *tocCountOut = lastTocCount;
  return best;
}

// I2: parseNcxToc's per-navPoint <text>/<content> lookups must stay
// bounded even when EVERY navPoint is missing <navLabel> (so <text>
// doesn't exist ANYWHERE in the whole document) -- before findTagLocal
// took a `limit`, each such lookup scanned all the way to EOF looking for
// a tag that isn't there, making an all-label-less NCX O(n^2) (measured
// ~514ms at 4000 navPoints).
static int testEpubNcxPerfScalesLinearlyNotQuadratically() {
  size_t toc3000 = 0, toc6000 = 0;
  double ms3000 = timeNcxOpenWithLabellessNavPoints(3000, &toc3000);
  double ms6000 = timeNcxOpenWithLabellessNavPoints(6000, &toc6000);
  std::fprintf(stderr, "perf ncx label-less navPoints scaling: 3000=%.2f ms, 6000=%.2f ms\n",
               ms3000, ms6000);
  CHECK(ms3000 >= 0.0);
  CHECK(ms6000 >= 0.0);
  CHECK(toc3000 == 3000);
  CHECK(toc6000 == 6000);
  CHECK(ms6000 < ms3000 * 3.2);
  return 0;
}

// ---------------------------------------------------------------------
// Paginator tests
// ---------------------------------------------------------------------

struct MockMeasure : Ink::TextMeasure {
  // Fixed per-style char widths/heights: deterministic, style-sensitive.
  int16_t widths[Ink::kStyleCount]  = {10, 11, 10, 11, 12, 20, 16, 12};
  int16_t heights[Ink::kStyleCount] = {20, 20, 20, 20, 20, 40, 32, 24};
  int16_t textWidth(const std::string &s, uint8_t st) override {
    return (int16_t)(s.size() * widths[st]);
  }
  int16_t lineHeight(uint8_t st) override { return heights[st]; }
};

static int testPaginatorWrapAndFill() {
  // pageW 600, marginX 48 → content 504 px → 50 body chars/line.
  Ink::LayoutSettings s;
  MockMeasure m;
  std::vector<Ink::Block> blocks;
  Ink::Block b;
  b.srcOffset = 0;
  b.runs.push_back({std::string(120, 'a') + " " + std::string(10, 'b'), false, false, false});
  blocks.push_back(b);
  Ink::Paginator p;
  p.layout(blocks, s, m);
  CHECK(p.lines().size() == 4);            // 50+50+20 a's + word-wrapped b's
  CHECK(p.lines()[0].segs[0].text.size() == 50);
  CHECK(p.pages().size() == 1);
  // Fill many paragraphs → content height 1024-40-64=920 → 46 lines/page at 20px.
  for (int i = 0; i < 30; ++i) { b.srcOffset = 200 + i; blocks.push_back(b); }
  p.layout(blocks, s, m);
  CHECK(p.pages().size() > 1);
  CHECK(p.pages()[0].lineCount <= 46);
  return 0;
}

static int testPaginatorResume() {
  Ink::LayoutSettings s1, s2;
  s2.fontStep = 2;
  MockMeasure m1;
  MockMeasure m2; for (auto &w : m2.widths) w += 6;  // "bigger font"
  std::vector<Ink::Block> blocks;
  for (int i = 0; i < 40; ++i) {
    Ink::Block b; b.srcOffset = i * 100;
    b.runs.push_back({std::string(180, 'x'), false, false, false});
    blocks.push_back(b);
  }
  Ink::Paginator p1; p1.layout(blocks, s1, m1);
  size_t page = p1.pageCount() / 2;
  uint32_t off = p1.pageStartOffset(page);
  Ink::Paginator p2; p2.layout(blocks, s2, m2);
  size_t landed = p2.pageForOffset(off);
  CHECK(p2.pageStartOffset(landed) <= off);
  CHECK(landed + 1 >= p2.pageCount() || p2.pageStartOffset(landed + 1) > off);
  CHECK(s1.hash() != s2.hash());
  CHECK(Ink::LayoutSettings().hash() == s1.hash());
  return 0;
}

// Hand math for the tightened line count/heights (default settings,
// contentW 504, char width 10, body raw height 20, gap = 40%*20 = 8):
//   huge (80 'w's, unbreakable): hard-split 50+30 -> lines[0]=50 chars
//   (not the block's last line: height 20*115/100=23, no gap), lines[1]=30
//   chars (last line of huge's block: 23+8=31).
//   empty (one empty-text run): zero words -> one blank placeholder line,
//   lines[2] (its block's only/last line: body height 23+8=31).
//   rule: one fixed-height line, lines[3] (its block's only/last line:
//   24 (fixed, never run through lineSpacingPct) + 8 gap = 32).
static int testPaginatorHardCases() {
  Ink::LayoutSettings s; MockMeasure m; Ink::Paginator p;
  std::vector<Ink::Block> blocks;
  Ink::Block huge;  // one unbreakable 80-char word: must hard-split, not loop
  huge.runs.push_back({std::string(80, 'w'), false, false, false});
  blocks.push_back(huge);
  Ink::Block empty; empty.runs.push_back({"", false, false, false});
  blocks.push_back(empty);
  Ink::Block rule; rule.type = Ink::BlockType::Rule;
  blocks.push_back(rule);
  p.layout(blocks, s, m);
  CHECK(p.lines().size() == 4);
  CHECK(p.lines()[0].segs[0].text.size() == 50);  // hard split at line width
  CHECK(p.lines()[1].segs[0].text.size() == 30);  // hard-split remnant
  CHECK(p.lines()[1].height == 31);
  // The empty-run block still produces a real (blank) line.
  CHECK(p.lines()[2].segs.empty());
  CHECK(p.lines()[2].height == 31);
  // Rule line's contract: fixed height (pre-gap 24, not scaled by
  // lineSpacingPct), no segs, its own blockType, and firstOfBlock set
  // (the renderer's cue to actually draw the rule glyph here).
  CHECK(p.lines()[3].segs.empty());
  CHECK(p.lines()[3].blockType == Ink::BlockType::Rule);
  CHECK(p.lines()[3].firstOfBlock == true);
  CHECK(p.lines()[3].height == 24 + 8);
  CHECK(p.pageCount() == 1);
  p.layout({}, s, m);
  CHECK(p.pageCount() == 1 && p.pages()[0].lineCount == 0);  // empty chapter = 1 blank page
  return 0;
}

// Two runs of different style on one line: "Hi" (body) + "there" (bold),
// joined by a space. Hand math (default settings, contentW 504):
//   "Hi" width = 2*10 = 20 -> seg0 x=0.
//   space (bold metric) = 1*11 = 11; "there" width = 5*11 = 55.
//   seg1 x = 20+11 = 31; line width = 31+55 = 86 (well under 504, one line).
//   height: max(lineHeight(body)=20, lineHeight(bold)=20) = 20,
//   scaled *115/100 = 23; block's only/last line -> +40%*20=8 gap -> 31.
static int testPaginatorMultiStyleLine() {
  Ink::LayoutSettings s; MockMeasure m; Ink::Paginator p;
  Ink::Block b;
  b.runs.push_back({"Hi", false, false, false});
  b.runs.push_back({" there", true, false, false});
  p.layout({b}, s, m);
  CHECK(p.lines().size() == 1);
  CHECK(p.lines()[0].segs.size() == 2);
  CHECK(p.lines()[0].segs[0].text == "Hi");
  CHECK(p.lines()[0].segs[0].style == Ink::kStyleBody);
  CHECK(p.lines()[0].segs[0].x == 0);
  CHECK(p.lines()[0].segs[1].text == "there");
  CHECK(p.lines()[0].segs[1].style == Ink::kStyleBold);
  CHECK(p.lines()[0].segs[1].x == 31);
  CHECK(p.lines()[0].height == 31);
  return 0;
}

// Heading-orphan rule: an H2 line that would be the LAST line fitting on a
// page, with more lines still to come, gets pushed to the next page.
// Hand math (capacity 920, contentW 504 -> 50 body chars/line):
//   Body block: 37 words of 50 'a's (each too wide to share a line with a
//   neighbor: 500+10(space)+500=1010>504) -> 37 one-word lines. First 36
//   have no paragraph gap (not the block's last line): height 20*115/100=23
//   each -> 36*23=828. Line 37 IS the block's last line: 23+8(40%*20 gap)=31.
//   Running total after all 37 = 828+31 = 859. Remaining on page1 = 920-859=61.
//   H2 block (one line "Head"): raw height 32, scaled 32*115/100=36 (floor),
//   + its own last-line gap 8 = 44. 44 <= 61, so the heading WOULD fit
//   (861+44=903<=920) -- but the trailing block's first line (see below,
//   height 23) needs 61-44=17px and doesn't fit (23>17), so the heading
//   would be an orphan and must move to page 2.
//   Trailing block: 2 words of 50 'b'/'c' chars (same too-wide-to-share
//   trick) -> line A height 23 (not last), line B height 23+8=31 (last).
// So page1 = 37 lines (all body), page2 starts at line 37 with the
// heading and holds the remaining 3 lines (heading + 2 trailing).
static int testPaginatorHeadingOrphan() {
  Ink::LayoutSettings s; MockMeasure m; Ink::Paginator p;
  std::vector<Ink::Block> blocks;
  Ink::Block body;
  {
    std::string text;
    for (int i = 0; i < 37; ++i) { if (i) text += ' '; text += std::string(50, 'a'); }
    body.runs.push_back({text, false, false, false});
  }
  blocks.push_back(body);
  Ink::Block heading;
  heading.type = Ink::BlockType::H2;
  heading.runs.push_back({"Head", false, false, false});
  blocks.push_back(heading);
  Ink::Block trailing;
  trailing.runs.push_back({std::string(50, 'b') + " " + std::string(50, 'c'), false, false, false});
  blocks.push_back(trailing);
  p.layout(blocks, s, m);
  CHECK(p.lines().size() == 40);  // 37 + 1 + 2
  CHECK(p.pages().size() == 2);
  CHECK(p.pages()[0].lineCount == 37);
  CHECK(p.pages()[1].firstLine == 37);
  CHECK(p.pages()[1].lineCount == 3);
  CHECK(p.lines()[37].blockType == Ink::BlockType::H2);
  CHECK(p.lines()[37].firstOfBlock == true);
  return 0;
}

// Paragraph gap (40% of raw body line height, 8px here) changes how many
// lines fit per page. 35 one-word blocks -> 35 one-line blocks, and since
// each line is simultaneously its block's first AND last line, every one
// of them carries the +8 gap: height = 23+8 = 31 uniformly.
// capacity 920 / 31 = 29.67 -> 29 lines fit (29*31=899<=920, 30*31=930>920).
// Without the gap (raw 23/line) 920/23=40 exactly -- all 35 would fit on
// one page. Seeing page1 stop at 29 (and a second page appear) is the
// pin that the gap is actually being applied, not just present in theory.
static int testPaginatorParagraphGap() {
  Ink::LayoutSettings s; MockMeasure m; Ink::Paginator p;
  std::vector<Ink::Block> blocks;
  for (int i = 0; i < 35; ++i) {
    Ink::Block b;
    b.runs.push_back({"word", false, false, false});
    blocks.push_back(b);
  }
  p.layout(blocks, s, m);
  CHECK(p.lines().size() == 35);
  CHECK(p.pages().size() == 2);
  CHECK(p.pages()[0].lineCount == 29);
  return 0;
}

// ListItem/Quote indent shrinks contentW, so fewer characters fit per
// line. Base contentW is 504 (50 body chars/line, char width 10).
//   ListItem depth 1: indent 24*1=24 -> contentW 480 -> floor(480/10)=48
//     chars/line. One 100-char unbreakable word -> hard split 48+48+4.
//   ListItem depth 2: indent 24*2=48 -> contentW 456 -> floor(456/10)=45
//     chars/line. Same 100-char word -> hard split 45+45+10.
//   Quote: flat indent 24 (not depth-scaled) -> same 480/48 as depth 1.
static int testPaginatorIndents() {
  Ink::LayoutSettings s; MockMeasure m;

  Ink::Block listDepth1;
  listDepth1.type = Ink::BlockType::ListItem;
  listDepth1.listDepth = 1;
  listDepth1.runs.push_back({std::string(100, 'x'), false, false, false});
  Ink::Paginator p1;
  p1.layout({listDepth1}, s, m);
  CHECK(p1.lines().size() == 3);
  CHECK(p1.lines()[0].indentPx == 24);
  CHECK(p1.lines()[0].segs[0].text.size() == 48);
  CHECK(p1.lines()[1].segs[0].text.size() == 48);
  CHECK(p1.lines()[2].segs[0].text.size() == 4);

  Ink::Block listDepth2;
  listDepth2.type = Ink::BlockType::ListItem;
  listDepth2.listDepth = 2;
  listDepth2.runs.push_back({std::string(100, 'x'), false, false, false});
  Ink::Paginator p2;
  p2.layout({listDepth2}, s, m);
  CHECK(p2.lines().size() == 3);
  CHECK(p2.lines()[0].indentPx == 48);
  CHECK(p2.lines()[0].segs[0].text.size() == 45);
  CHECK(p2.lines()[2].segs[0].text.size() == 10);

  Ink::Block quote;
  quote.type = Ink::BlockType::Quote;
  quote.runs.push_back({std::string(100, 'y'), false, false, false});
  Ink::Paginator p3;
  p3.layout({quote}, s, m);
  CHECK(p3.lines().size() == 3);
  CHECK(p3.lines()[0].indentPx == 24);
  CHECK(p3.lines()[0].segs[0].text.size() == 48);
  return 0;
}

// lineSpacingPct 130 -> fewer lines/page than 115 for the same content.
// Reusing the 35 one-word-block shape from testPaginatorParagraphGap:
// raw body height 20, scaled 20*130/100=26, +8 gap (spacing-independent)
// = 34/line. capacity 920/34 = 27.06 -> 27 lines fit (27*34=918<=920,
// 28*34=952>920) -- fewer than the 29/page measured at 115%.
static int testPaginatorLineSpacing() {
  Ink::LayoutSettings s; s.lineSpacingPct = 130;
  MockMeasure m; Ink::Paginator p;
  std::vector<Ink::Block> blocks;
  for (int i = 0; i < 35; ++i) {
    Ink::Block b;
    b.runs.push_back({"word", false, false, false});
    blocks.push_back(b);
  }
  p.layout(blocks, s, m);
  CHECK(p.pages()[0].lineCount == 27);
  CHECK(p.pages()[0].lineCount < 29);  // strictly fewer than the 115% case
  return 0;
}

// pageForOffset clamps at both ends: an offset before the book's start
// resolves to page 0, and one past the end resolves to the last page.
// srcOffset must increase block-to-block (as every real parser does) --
// leaving it at the Block default of 0 for every block would make every
// line report the same srcOffset and defeat the clamp being tested.
static int testPaginatorOffsetBounds() {
  Ink::LayoutSettings s; MockMeasure m; Ink::Paginator p;
  std::vector<Ink::Block> blocks;
  for (int i = 0; i < 35; ++i) {
    Ink::Block b;
    b.srcOffset = i * 10;
    b.runs.push_back({"word", false, false, false});
    blocks.push_back(b);
  }
  p.layout(blocks, s, m);
  CHECK(p.pageCount() > 1);  // otherwise this test can't distinguish ends
  CHECK(p.pageForOffset(0) == 0);
  CHECK(p.pageForOffset(0xFFFFFFFFu) == p.pageCount() - 1);
  return 0;
}

// hash() must change when ANY single field changes, and must reproduce
// for identical settings (default-vs-default is covered in
// testPaginatorResume; this covers each field independently).
static int testPaginatorHashFields() {
  Ink::LayoutSettings base;
  uint32_t h0 = base.hash();
  CHECK(base.hash() == h0);  // reproducible

  { Ink::LayoutSettings v = base; v.pageW = (int16_t)(v.pageW + 1); CHECK(v.hash() != h0); }
  { Ink::LayoutSettings v = base; v.pageH = (int16_t)(v.pageH + 1); CHECK(v.hash() != h0); }
  { Ink::LayoutSettings v = base; v.marginX = (int16_t)(v.marginX + 1); CHECK(v.hash() != h0); }
  { Ink::LayoutSettings v = base; v.marginTop = (int16_t)(v.marginTop + 1); CHECK(v.hash() != h0); }
  { Ink::LayoutSettings v = base; v.marginBottom = (int16_t)(v.marginBottom + 1); CHECK(v.hash() != h0); }
  { Ink::LayoutSettings v = base; v.fontStep = (uint8_t)(v.fontStep + 1); CHECK(v.hash() != h0); }
  { Ink::LayoutSettings v = base; v.lineSpacingPct = (uint8_t)(v.lineSpacingPct + 1); CHECK(v.hash() != h0); }
  return 0;
}

// Code blocks split on '\n' only -- no width-based rewrap, even when a
// source line is far wider than contentW. Two source lines, the second
// far wider than the 504px content box (mono char width 12 -> a 60-char
// line measures 720px, well past 504) -- both still land as exactly one
// Line each, verbatim, because Code never re-wraps.
static int testPaginatorCodeBlock() {
  Ink::LayoutSettings s; MockMeasure m; Ink::Paginator p;
  Ink::Block code;
  code.type = Ink::BlockType::Code;
  std::string wide(60, 'z');
  code.runs.push_back({"short\n" + wide, false, false, false});
  p.layout({code}, s, m);
  CHECK(p.lines().size() == 2);
  CHECK(p.lines()[0].segs.size() == 1);
  CHECK(p.lines()[0].segs[0].text == "short");
  CHECK(p.lines()[0].segs[0].style == Ink::kStyleMono);
  CHECK(p.lines()[1].segs[0].text == wide);  // not split despite being too wide
  CHECK(p.lines()[1].segs[0].text.size() == 60);
  return 0;
}

// Mid-word style change with NO space between ("bo" + "ld", bold) glues
// the two segs directly: seg1.x must equal exactly seg0's width (20px),
// not 20+space, because spaceBefore is false for a glued word. Contrast
// with testPaginatorMultiStyleLine's x==31, which includes an 11px join
// space -- the only difference between the two scenarios is whether a
// space separated the words in the source.
static int testPaginatorGluedStyleChange() {
  Ink::LayoutSettings s; MockMeasure m; Ink::Paginator p;
  Ink::Block b;
  b.runs.push_back({"bo", false, false, false});
  b.runs.push_back({"ld", true, false, false});  // no space, bold
  p.layout({b}, s, m);
  CHECK(p.lines().size() == 1);
  CHECK(p.lines()[0].segs.size() == 2);
  CHECK(p.lines()[0].segs[0].text == "bo");
  CHECK(p.lines()[0].segs[0].style == Ink::kStyleBody);
  CHECK(p.lines()[0].segs[0].x == 0);
  CHECK(p.lines()[0].segs[1].text == "ld");
  CHECK(p.lines()[0].segs[1].style == Ink::kStyleBold);
  CHECK(p.lines()[0].segs[1].x == 20);  // == "bo" width (2*10), no space added
  return 0;
}

// A real word immediately after a hard-split word must start its own
// fresh line -- never share the hard-split remnant's line. 60 'a' chars
// hard-splits into 50+10 (contentW 504, char width 10); "hi" comes right
// after with a single joining space in the source.
static int testPaginatorWordAfterHardSplitStartsFreshLine() {
  Ink::LayoutSettings s; MockMeasure m; Ink::Paginator p;
  Ink::Block b;
  b.runs.push_back({std::string(60, 'a') + " hi", false, false, false});
  p.layout({b}, s, m);
  CHECK(p.lines().size() == 3);
  CHECK(p.lines()[0].segs[0].text.size() == 50);
  CHECK(p.lines()[1].segs.size() == 1);
  CHECK(p.lines()[1].segs[0].text.size() == 10);  // hard-split remnant, alone
  CHECK(p.lines()[2].segs.size() == 1);
  CHECK(p.lines()[2].segs[0].text == "hi");
  CHECK(p.lines()[2].segs[0].x == 0);  // fresh line, not appended to the remnant
  return 0;
}

// REQUIRED regression: a word long enough to overflow
// TextMeasure::textWidth's int16_t return must still be recognized as
// unbreakable-and-too-wide and hard-split -- not silently accepted onto
// one line because its measured width wrapped to something tiny.
// MockMeasure computes (int16_t)(s.size()*widths[st]); for a 6554-char
// body-style word that's 6554*10=65540, which truncates to int16_t as
// 65540 mod 65536 = 4 -- a bogus "4px" width that would otherwise pass
// the old `wordW > contentW` check with room to spare.
// Hand math once the length guard forces a hard split (contentW 504,
// char width 10 -> 50 chars/chunk): 6554 / 50 = 131 full 50-char chunks
// (6550 chars) + one 4-char remainder chunk = 132 lines total, not 1.
static int testPaginatorOverflowGuard() {
  Ink::LayoutSettings s; MockMeasure m; Ink::Paginator p;
  Ink::Block b;
  b.runs.push_back({std::string(6554, 'a'), false, false, false});
  p.layout({b}, s, m);
  CHECK(p.lines().size() == 132);
  CHECK(p.lines()[0].segs[0].text.size() == 50);
  CHECK(p.lines()[130].segs[0].text.size() == 50);  // last full chunk
  CHECK(p.lines()[131].segs[0].text.size() == 4);   // remainder
  CHECK(p.pageCount() > 1);  // proof it wasn't clipped onto one page/line
  return 0;
}

// ---------------------------------------------------------------------
// InkBook facade tests
// ---------------------------------------------------------------------

static int testInkBookTxt() {
  std::string src = "First para.\n\nSecond para.\n";
  Ink::InkBook book;
  CHECK(book.open(Ink::Format::Txt, (const uint8_t *)src.data(), src.size()));
  CHECK(book.format() == Ink::Format::Txt);
  CHECK(book.chapterCount() == 1);
  CHECK(book.title() == "");
  CHECK(book.author() == "");
  std::vector<Ink::Block> blocks;
  CHECK(book.loadChapter(0, blocks));
  CHECK(blocks.size() == 2);
  CHECK(Ink::plainText(blocks[0]) == "First para.");
  CHECK(Ink::plainText(blocks[1]) == "Second para.");
  CHECK(!book.loadChapter(1, blocks));  // TXT/MD only ever have chapter 0
  CHECK(book.permille(0, 0) == 0);
  CHECK(book.permille(0, (uint32_t)src.size()) == 1000);
  return 0;
}

// "# A\n\nbody\n\n## B\n\nbody\n\n### C" -- H3 excluded from the TOC per
// spec, so only A (H1) and B (H2) show up.
// Byte offsets, hand-counted:
//   0:'#' 1:' ' 2:'A' 3:'\n' 4:'\n' 5:'b' 6:'o' 7:'d' 8:'y' 9:'\n' 10:'\n'
//   11:'#' 12:'#' 13:' ' 14:'B' 15:'\n' 16:'\n' 17:'b' 18:'o' 19:'d' 20:'y'
//   21:'\n' 22:'\n' 23:'#' 24:'#' 25:'#' 26:' ' 27:'C'
// So "## B" (the H2's srcOffset) starts at byte 11.
static int testInkBookMarkdownToc() {
  std::string src = "# A\n\nbody\n\n## B\n\nbody\n\n### C";
  Ink::InkBook book;
  CHECK(book.open(Ink::Format::Markdown, (const uint8_t *)src.data(), src.size()));
  CHECK(book.format() == Ink::Format::Markdown);
  CHECK(book.chapterCount() == 1);
  CHECK(book.toc().size() == 2);
  CHECK(book.toc()[0].title == "A" && book.toc()[0].spineIndex == 0);
  CHECK(book.toc()[1].title == "B" && book.toc()[1].spineIndex == 0);
  CHECK(book.tocOffset(0) == 0);
  CHECK(book.tocOffset(1) == 11);  // '#' of "## B" -- hand-counted above
  return 0;
}

static int testInkBookMarkdownNoHeadings() {
  std::string src = "Just a paragraph.\n\nAnother one.\n";
  Ink::InkBook book;
  CHECK(book.open(Ink::Format::Markdown, (const uint8_t *)src.data(), src.size()));
  CHECK(book.toc().empty());
  CHECK(book.chapterCount() == 1);
  std::vector<Ink::Block> blocks;
  CHECK(book.loadChapter(0, blocks));
  CHECK(blocks.size() == 2);
  return 0;
}

static int testInkBookEpubDelegation() {
  std::string zipData = buildFixtureEpub(true);
  Ink::InkBook book;
  CHECK(book.open(Ink::Format::Epub, (const uint8_t *)zipData.data(), zipData.size()));
  CHECK(book.format() == Ink::Format::Epub);
  CHECK(book.title() == "Fixture Book");
  CHECK(book.author() == "Test Author");
  CHECK(book.chapterCount() == 2);
  CHECK(book.toc().size() == 2);
  CHECK(book.toc()[0].spineIndex == 0);
  CHECK(book.toc()[1].spineIndex == 1);
  CHECK(book.tocOffset(0) == 0);
  CHECK(book.tocOffset(1) == 0);  // EPUB TOC offsets are always 0

  std::vector<Ink::Block> blocks;
  CHECK(book.loadChapter(0, blocks));
  CHECK(blocks.size() == 2);  // "<h1>One</h1><p>First chapter text.</p>"
  CHECK(blocks[0].type == Ink::BlockType::H1 && Ink::plainText(blocks[0]) == "One");
  CHECK(blocks[1].type == Ink::BlockType::Body);
  CHECK(!book.loadChapter(5, blocks));
  return 0;
}

// A failed EPUB open must leave the InkBook in a clean, reusable state --
// opening a TXT on the same instance right after must succeed normally.
static int testInkBookEpubOpenFailureThenTxtSucceeds() {
  Ink::InkBook book;
  CHECK(!book.open(Ink::Format::Epub, (const uint8_t *)"not a zip", 9));
  CHECK(book.format() == Ink::Format::Txt);   // reset, not left mid-EPUB
  CHECK(book.chapterCount() == 1);
  CHECK(book.title() == "");

  std::string src = "hello world\n";
  CHECK(book.open(Ink::Format::Txt, (const uint8_t *)src.data(), src.size()));
  CHECK(book.format() == Ink::Format::Txt);
  CHECK(book.chapterCount() == 1);
  std::vector<Ink::Block> blocks;
  CHECK(book.loadChapter(0, blocks));
  CHECK(blocks.size() == 1);
  return 0;
}

// permille is byte-weighted over EpubBook::chapterSize()'s UNCOMPRESSED
// entry sizes (AMENDED 2026-08-06, fix(25): permille uses uncompressed
// sizes -- see EpubBook::chapterSize()'s comment for why compressed sizes
// under-report intra-chapter progress). Sizes are read from the same API
// InkBook itself used at open() rather than hardcoded, so this doesn't
// break if the fixture's markup ever changes length.
//
// This block PINS THE FORMULA (exact division, chapter-boundary equality,
// uint64_t range) against a fixture small enough to verify entirely by
// hand: ch1.xhtml is 64 bytes, ch2.xhtml is 65 bytes (counted with
// len()), and both are re-derived below from the live API so the numbers
// can't drift silently. total=129, permille(0,0)=0, permille(0,64)==
// permille(1,0)=floor(64000/129)=496, permille(0,32)=floor(32000/129)=248.
//
// It is deliberately NOT the compressed-vs-uncompressed-size regression
// guard, and re-review confirmed it can't be: deflate can't meaningfully
// shrink 64 bytes of unique markup (small inputs often expand slightly
// under deflate instead), and s0/s1 here are read from
// raw.chapterSize() -- the very function under test -- so a reverted
// (compressed-size) build would simply recompute the same "expected"
// numbers from its own (buggy) units and this block would still pass.
// testInkBookPermilleEpubDiscriminatesCompression below is the actual
// regression guard: a real compressible chapter, offsets measured in the
// decompressed domain via chapterXhtml() (independent of whatever
// chapterSize() currently returns), verified to fail against a
// deliberately-reverted m_comp_size build.
static int testInkBookPermilleEpub() {
  std::string zipData = buildFixtureEpub(true);
  Ink::InkBook book;
  CHECK(book.open(Ink::Format::Epub, (const uint8_t *)zipData.data(), zipData.size()));

  Ink::EpubBook raw;
  CHECK(raw.open((const uint8_t *)zipData.data(), zipData.size()));
  uint32_t s0 = raw.chapterSize(0);
  uint32_t s1 = raw.chapterSize(1);
  CHECK(s0 > 0 && s1 > 0);
  uint64_t total = (uint64_t)s0 + (uint64_t)s1;
  uint16_t expectedChapter1Start = (uint16_t)(((uint64_t)s0 * 1000ull) / total);
  uint16_t expectedMid = (uint16_t)(((uint64_t)(s0 / 2) * 1000ull) / total);

  CHECK(book.permille(0, 0) == 0);
  CHECK(book.permille(1, 0) == expectedChapter1Start);
  CHECK(book.permille(0, s0) == expectedChapter1Start);  // end of ch0 == start of ch1
  CHECK(book.permille(1, 0xFFFFFFFFu) == 1000);  // huge offset clamps to end

  // Formula sanity at a mid-chapter offset -- exact-division arithmetic
  // only, not a discrimination test (see the file comment above).
  uint16_t atStart = book.permille(0, 0);
  uint16_t atMid = book.permille(0, s0 / 2);
  uint16_t atEnd = book.permille(0, s0);
  CHECK(atMid == expectedMid);
  CHECK(atStart < atMid && atMid < atEnd);
  return 0;
}

// Same fixture SHAPE as buildFixtureEpub(true) (mimetype/container/OPF
// wiring, 2-chapter spine), but chapter 0 is ~60 repetitions of one
// prose paragraph instead of one hand-written sentence -- large and
// redundant enough for deflate to hit a REAL compression ratio. Tiny,
// unique markup (like buildFixtureEpub's 64-byte chapters) often expands
// slightly under deflate and can't discriminate a compressed-size bug
// from a correct uncompressed-size implementation at all; this fixture
// exists specifically so the two implementations disagree.
static std::string buildFixtureEpubCompressibleChapter() {
  ZipBuilder zb;
  zb.add("mimetype", "application/epub+zip");
  zb.add("META-INF/container.xml",
         "<?xml version=\"1.0\"?><container><rootfiles>"
         "<rootfile full-path=\"OEBPS/content.opf\" media-type=\"application/oebps-package+xml\"/>"
         "</rootfiles></container>");
  zb.add("OEBPS/content.opf",
         "<?xml version=\"1.0\"?><package><metadata><dc:title>T</dc:title></metadata>"
         "<manifest>"
         "<item id=\"c1\" href=\"ch1.xhtml\" media-type=\"application/xhtml+xml\"/>"
         "<item id=\"c2\" href=\"ch2.xhtml\" media-type=\"application/xhtml+xml\"/>"
         "</manifest><spine><itemref idref=\"c1\"/><itemref idref=\"c2\"/></spine></package>");
  const std::string paragraph =
      "<p>The quick brown fox jumps over the lazy dog while the old clock "
      "in the hallway keeps its slow and steady time, page after page, "
      "chapter after chapter, exactly as repetitive prose compresses.</p>";
  std::string bigChapter = "<html><body>";
  for (int i = 0; i < 60; ++i) bigChapter += paragraph;
  bigChapter += "</body></html>";
  zb.add("OEBPS/ch1.xhtml", bigChapter);
  zb.add("OEBPS/ch2.xhtml", "<html><body><p>Second chapter, unrelated text.</p></body></html>");
  return zb.finalize();
}

// THE discriminating regression test (re-review requested this after
// confirming testInkBookPermilleEpub above passes unchanged against a
// reverted m_comp_size build). Two things make this one actually catch
// the bug where the one above couldn't:
//   1. buildFixtureEpubCompressibleChapter()'s chapter 0 compresses at a
//      real ratio, so compressed and uncompressed sizes meaningfully
//      diverge (unlike the 64-byte hand-written fixture above).
//   2. The mid/end offsets come from raw.chapterXhtml()'s DECOMPRESSED
//      length -- the domain Block::srcOffset and the paginator actually
//      work in -- not from chapterSize() itself, so a buggy
//      compressed-size build can't launder its own wrong unit into the
//      test's expectations the way testInkBookPermilleEpub's s0/s1 do.
// Manually verified against a temporarily-reverted chapterSize()
// (returning stat.m_comp_size): atMid saturated to the same value as
// atEnd instead of landing strictly between atStart and atEnd -- exactly
// the failure mode this asserts against.
static int testInkBookPermilleEpubDiscriminatesCompression() {
  std::string zipData = buildFixtureEpubCompressibleChapter();
  Ink::InkBook book;
  CHECK(book.open(Ink::Format::Epub, (const uint8_t *)zipData.data(), zipData.size()));

  Ink::EpubBook raw;
  CHECK(raw.open((const uint8_t *)zipData.data(), zipData.size()));
  std::string x;
  CHECK(raw.chapterXhtml(0, x));  // decompressed text -- the unit Block::srcOffset lives in

  uint16_t atStart = book.permille(0, 0);
  uint16_t atMid = book.permille(0, (uint32_t)(x.size() / 2));
  uint16_t atEnd = book.permille(0, (uint32_t)x.size());
  CHECK(atStart < atMid && atMid < atEnd);
  return 0;
}

// open() on a live InkBook without an explicit close() first must behave
// exactly like close()+open(): the previous format's state (title,
// chapter count) is fully replaced, not merged or leaked.
static int testInkBookReopenTxtThenEpub() {
  Ink::InkBook book;
  std::string src = "hello world\n";
  CHECK(book.open(Ink::Format::Txt, (const uint8_t *)src.data(), src.size()));
  CHECK(book.title() == "");
  CHECK(book.chapterCount() == 1);

  std::string zipData = buildFixtureEpub(true);
  CHECK(book.open(Ink::Format::Epub, (const uint8_t *)zipData.data(), zipData.size()));
  CHECK(book.format() == Ink::Format::Epub);
  CHECK(book.title() == "Fixture Book");
  CHECK(book.chapterCount() == 2);
  return 0;
}

// Full pipe: EPUB fixture -> InkBook::loadChapter -> Paginator with the
// existing host MockMeasure. Confirms the facade's output actually feeds
// the real paginator, not just that it returns plausible-looking blocks.
static int testInkBookIntegrationPaginate() {
  std::string zipData = buildFixtureEpub(true);
  Ink::InkBook book;
  CHECK(book.open(Ink::Format::Epub, (const uint8_t *)zipData.data(), zipData.size()));
  std::vector<Ink::Block> blocks;
  CHECK(book.loadChapter(0, blocks));
  CHECK(!blocks.empty());

  Ink::LayoutSettings s;
  MockMeasure m;
  Ink::Paginator p;
  p.layout(blocks, s, m);
  CHECK(p.pageCount() >= 1);
  CHECK(!p.lines().empty());
  CHECK(!p.lines()[0].segs.empty());
  CHECK(p.lines()[0].segs[0].text == "One");
  return 0;
}

// tocTarget: EPUB path. Reuses the unresolved-href shape from
// testEpubTocEntryUnresolvedHrefKept (a nav TOC with one entry pointing
// at a real spine chapter and one pointing nowhere) so the resolved and
// unresolved cases sit side by side in one TOC.
static int testInkBookTocTargetEpub() {
  ZipBuilder zb;
  zb.add("mimetype", "application/epub+zip");
  zb.add("META-INF/container.xml",
         "<?xml version=\"1.0\"?><container><rootfiles>"
         "<rootfile full-path=\"OEBPS/content.opf\" media-type=\"application/oebps-package+xml\"/>"
         "</rootfiles></container>");
  zb.add("OEBPS/content.opf",
         "<?xml version=\"1.0\"?><package><metadata><dc:title>T</dc:title></metadata>"
         "<manifest>"
         "<item id=\"c1\" href=\"ch1.xhtml\" media-type=\"application/xhtml+xml\"/>"
         "<item id=\"nav\" href=\"nav.xhtml\" media-type=\"application/xhtml+xml\" properties=\"nav\"/>"
         "</manifest><spine><itemref idref=\"c1\"/></spine></package>");
  zb.add("OEBPS/ch1.xhtml", "<html><body><p>x</p></body></html>");
  zb.add("OEBPS/nav.xhtml",
         "<html><body><nav epub:type=\"toc\"><ol>"
         "<li><a href=\"ch1.xhtml\">Real</a></li>"
         "<li><a href=\"nowhere.xhtml\">Ghost</a></li></ol></nav></body></html>");
  std::string zipData = zb.finalize();

  Ink::InkBook book;
  CHECK(book.open(Ink::Format::Epub, (const uint8_t *)zipData.data(), zipData.size()));
  CHECK(book.toc().size() == 2);

  size_t chapter = 999;  // sentinel: must be overwritten only on success
  CHECK(book.tocTarget(0, chapter));
  CHECK(chapter == 0);
  CHECK(!book.tocTarget(1, chapter));   // "nowhere.xhtml" never resolved
  CHECK(!book.tocTarget(2, chapter));   // out of range
  return 0;
}

// tocTarget: MD path. Every MD TOC entry targets the single chapter 0.
static int testInkBookTocTargetMarkdown() {
  std::string src = "# A\n\nbody\n\n## B\n\nbody";
  Ink::InkBook book;
  CHECK(book.open(Ink::Format::Markdown, (const uint8_t *)src.data(), src.size()));
  CHECK(book.toc().size() == 2);

  size_t chapter = 999;
  CHECK(book.tocTarget(0, chapter) && chapter == 0);
  chapter = 999;
  CHECK(book.tocTarget(1, chapter) && chapter == 0);
  CHECK(!book.tocTarget(2, chapter));  // out of range
  return 0;
}

// TXT never has TOC entries at all, so every index is out of range.
static int testInkBookTocTargetTxt() {
  std::string src = "hello\n";
  Ink::InkBook book;
  CHECK(book.open(Ink::Format::Txt, (const uint8_t *)src.data(), src.size()));
  CHECK(book.toc().empty());
  size_t chapter = 999;
  CHECK(!book.tocTarget(0, chapter));
  return 0;
}

// open(Txt, nullptr, 0) is the ONLY thing exercising InkBook's
// bufferToString() nullptr guard: constructing std::string(nullptr, 0)
// straight from a raw pointer is technically UB even at zero length, so
// this must go through the empty-string branch instead, and behave like
// any other empty TXT book (open succeeds, one chapter, zero blocks).
static int testInkBookNullEmptyOpenTxt() {
  Ink::InkBook book;
  CHECK(book.open(Ink::Format::Txt, nullptr, 0));
  CHECK(book.chapterCount() == 1);
  std::vector<Ink::Block> blocks;
  CHECK(book.loadChapter(0, blocks));
  CHECK(blocks.empty());
  CHECK(book.permille(0, 0) == 0);  // total 0 -> 0, not a divide-by-zero
  return 0;
}

// Same nullptr/zero-size guard on the MD path, which additionally runs
// the TOC pre-scan over the (empty) buffer at open() time.
static int testInkBookNullEmptyOpenMarkdown() {
  Ink::InkBook book;
  CHECK(book.open(Ink::Format::Markdown, nullptr, 0));
  CHECK(book.chapterCount() == 1);
  CHECK(book.toc().empty());
  std::vector<Ink::Block> blocks;
  CHECK(book.loadChapter(0, blocks));
  CHECK(blocks.empty());
  return 0;
}

// Direct close() (whether or not open() was ever called first) leaves a
// fully-reset, reusable InkBook: default TXT format, one reported
// chapter, empty toc/title/author, and a permille() that returns 0
// rather than reading stale chapterSizes_/totalSize_.
static int testInkBookCloseResetsState() {
  Ink::InkBook book;
  std::string zipData = buildFixtureEpub(true);
  CHECK(book.open(Ink::Format::Epub, (const uint8_t *)zipData.data(), zipData.size()));
  CHECK(book.chapterCount() == 2);
  CHECK(!book.title().empty());

  book.close();
  CHECK(book.format() == Ink::Format::Txt);
  CHECK(book.chapterCount() == 1);
  CHECK(book.toc().empty());
  CHECK(book.title().empty());
  CHECK(book.author().empty());
  CHECK(book.permille(0, 0) == 0);
  return 0;
}

// coverImage(): false (untouched out params) for TXT/MD, delegates to
// EpubBook for EPUB. Reuses the existing fixture, which already carries a
// cover.jpg + <meta name="cover"> (see testEpubOpen).
static int testInkBookCoverImage() {
  Ink::InkBook txtBook;
  std::string src = "hello\n";
  CHECK(txtBook.open(Ink::Format::Txt, (const uint8_t *)src.data(), src.size()));
  std::string out = "untouched", mediaType = "untouched";
  CHECK(!txtBook.coverImage(out, mediaType));
  CHECK(out == "untouched" && mediaType == "untouched");

  std::string zipData = buildFixtureEpub(true);
  Ink::InkBook book;
  CHECK(book.open(Ink::Format::Epub, (const uint8_t *)zipData.data(), zipData.size()));
  std::string cover, mediaType2;
  CHECK(book.coverImage(cover, mediaType2));
  CHECK(mediaType2 == "image/jpeg");
  CHECK(!cover.empty());
  return 0;
}

// --- SampleBooks.h integration (Task 8) ----------------------------------
// SampleBooks.h is Arduino-free by design, so it -- unlike LibraryStore.h/
// .cpp, which are device-only -- belongs in the host suite: these two
// tests are the guard against the embedded samples silently rotting (a
// future edit to a parser or to the sample text itself changing what a
// device boot would actually render, with nothing catching it here).

// Opens the real embedded EPUB sample through InkBook (not a fixture) and
// paginates chapter 0 with the shared MockMeasure, pinning the exact
// values a scratch run verified: title/chapterCount from the book's own
// metadata, and chapter 0's first rendered line's first (and, since the
// whole heading fits one line and headings collapse to a single style,
// only) seg -- the literal heading text with no wrapping.
static int testSampleEpubIntegration() {
  const std::string &zip = Ink::sampleEpub();
  CHECK(!zip.empty());  // catches a SampleBooks.h build regression outright

  Ink::InkBook book;
  CHECK(book.open(Ink::Format::Epub, (const uint8_t *)zip.data(), zip.size()));
  CHECK(book.title() == "The Inkwell Sampler");
  CHECK(book.author() == "Project 25");
  CHECK(book.chapterCount() == 3);

  std::vector<Ink::Block> blocks;
  CHECK(book.loadChapter(0, blocks));
  CHECK(!blocks.empty());

  Ink::LayoutSettings s;
  MockMeasure m;
  Ink::Paginator p;
  p.layout(blocks, s, m);
  CHECK(!p.lines().empty());
  CHECK(!p.lines()[0].segs.empty());
  CHECK(p.lines()[0].segs[0].text == "The Blank Page");
  return 0;
}

// Census check on the Markdown sample: every block type the task spec asked
// it to exercise (h1/h2/h3, bold/italic/mono, a nested + an ordered list, a
// blockquote, a fenced code block, an hr, a link, a dropped image) must
// still be present with the exact counts a scratch run verified. This
// can't tell a reviewer WHAT changed if it fails, but it makes an edit
// that silently drops a feature (e.g. someone "cleaning up" the ordered
// list) fail loudly instead of only showing up as a missing demo on
// device.
static int testSampleMarkdownCensus() {
  const std::string &md = Ink::sampleMarkdown();
  CHECK(!md.empty());

  auto blocks = Ink::parseMarkdown(md);
  int h1 = 0, h2 = 0, h3 = 0, quote = 0, code = 0, rule = 0, listItem = 0, ordered = 0;
  for (const auto &b : blocks) {
    switch (b.type) {
      case Ink::BlockType::H1: ++h1; break;
      case Ink::BlockType::H2: ++h2; break;
      case Ink::BlockType::H3: ++h3; break;
      case Ink::BlockType::Quote: ++quote; break;
      case Ink::BlockType::Code: ++code; break;
      case Ink::BlockType::Rule: ++rule; break;
      case Ink::BlockType::ListItem:
        ++listItem;
        if (b.ordered) ++ordered;
        break;
      default: break;
    }
  }
  CHECK(h1 == 1);
  CHECK(h2 == 3);
  CHECK(h3 == 1);
  CHECK(quote == 1);
  CHECK(code == 1);
  CHECK(rule == 1);
  CHECK(listItem == 8);
  CHECK(ordered == 3);
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
  if (testEpubAttributeValueWithGtDoesNotBreakTagSpan()) return 1;
  if (testEpubDataHrefDoesNotShadowHref()) return 1;
  if (testEpubOversizedEntryRejectedByReadEntry()) return 1;
  if (testEpubOpenCloseOpenReuse()) return 1;
  if (testEpubReopenWhileOpen()) return 1;
  if (testEpubOpfAtArchiveRoot()) return 1;
  if (testEpubEmptySpine()) return 1;
  if (testEpubPerfScalesLinearlyNotQuadratically()) return 1;
  if (testEpubNcxPerfScalesLinearlyNotQuadratically()) return 1;
  if (testPaginatorWrapAndFill()) return 1;
  if (testPaginatorResume()) return 1;
  if (testPaginatorHardCases()) return 1;
  if (testPaginatorMultiStyleLine()) return 1;
  if (testPaginatorHeadingOrphan()) return 1;
  if (testPaginatorParagraphGap()) return 1;
  if (testPaginatorIndents()) return 1;
  if (testPaginatorLineSpacing()) return 1;
  if (testPaginatorOffsetBounds()) return 1;
  if (testPaginatorHashFields()) return 1;
  if (testPaginatorCodeBlock()) return 1;
  if (testPaginatorGluedStyleChange()) return 1;
  if (testPaginatorWordAfterHardSplitStartsFreshLine()) return 1;
  if (testPaginatorOverflowGuard()) return 1;
  if (testInkBookTxt()) return 1;
  if (testInkBookMarkdownToc()) return 1;
  if (testInkBookMarkdownNoHeadings()) return 1;
  if (testInkBookEpubDelegation()) return 1;
  if (testInkBookEpubOpenFailureThenTxtSucceeds()) return 1;
  if (testInkBookPermilleEpub()) return 1;
  if (testInkBookPermilleEpubDiscriminatesCompression()) return 1;
  if (testInkBookReopenTxtThenEpub()) return 1;
  if (testInkBookIntegrationPaginate()) return 1;
  if (testInkBookTocTargetEpub()) return 1;
  if (testInkBookTocTargetMarkdown()) return 1;
  if (testInkBookTocTargetTxt()) return 1;
  if (testInkBookNullEmptyOpenTxt()) return 1;
  if (testInkBookNullEmptyOpenMarkdown()) return 1;
  if (testInkBookCloseResetsState()) return 1;
  if (testInkBookCoverImage()) return 1;
  if (testSampleEpubIntegration()) return 1;
  if (testSampleMarkdownCensus()) return 1;
  std::printf("inkwell host tests: %d checks passed\n", checks);
  return 0;
}
