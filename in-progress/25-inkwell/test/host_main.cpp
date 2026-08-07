// Host tests for Inkwell's format core. No Arduino, no SD, no display:
// these are the EXACT translation units that ship in the firmware.
#include <cstdio>
#include <string>
#include "../src/InkDoc.h"
#include "../src/MarkdownParser.h"
#include "../src/TxtParser.h"
#include "../src/XhtmlParser.h"

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
  std::printf("inkwell host tests: %d checks passed\n", checks);
  return 0;
}
