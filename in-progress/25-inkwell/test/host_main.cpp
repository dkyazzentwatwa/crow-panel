// Host tests for Inkwell's format core. No Arduino, no SD, no display:
// these are the EXACT translation units that ship in the firmware.
#include <cstdio>
#include <string>
#include "../src/InkDoc.h"
#include "../src/MarkdownParser.h"
#include "../src/TxtParser.h"

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
  std::printf("inkwell host tests: %d checks passed\n", checks);
  return 0;
}
