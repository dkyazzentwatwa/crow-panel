#ifndef INKWELL_SAMPLE_BOOKS_H
#define INKWELL_SAMPLE_BOOKS_H

// Arduino-free by design: both the device .ino (via LibraryStore) and the
// host test suite must be able to include this header without pulling in
// Arduino types. Everything here returns std::string (or, for the EPUB, a
// raw zip-container byte string) -- LibraryStore.{h,cpp} is the one place
// that bridges these into Arduino String-typed BookEntry rows.
//
// All three "books" are built once and cached in a static local, so
// repeated calls (e.g. re-opening the same sample after `close`) are free.

#include <cstdint>
#include <string>

#include "miniz.h"

namespace Ink {

// ---------------------------------------------------------------------
// Sample 1 (TXT): a short-story fragment, several paragraphs, ~40 lines
// of source text -- enough to page across several screens at the default
// layout settings.
// ---------------------------------------------------------------------
inline const std::string &sampleTxt() {
  static const std::string kText = R"TXT(The lamp had burned since dusk, and Mara did not trust it to last until
the storm passed. Salt spray needled the window glass, and somewhere out
past the reef a ship's bell rang twice and went silent.

She kept the ledger the way her father had taught her: one line for the
hour, one line for the wind, and a third, if there was room, for
whatever she actually felt. Most nights the third line stayed empty.
Tonight she filled it before she filled the other two.

"Light holding," she wrote. "Wind backing northwest. Something wrong
with the second glass." The second glass was the lens on the seaward
side, the one that had thrown a thin, wavering beam for a week now
instead of the clean white column it used to spend.

Her uncle had kept this light for thirty-one years without missing a
single dusk-to-dawn watch, and he had never once mentioned that the
work was mostly listening. Listening to the motor. Listening to the
glass. Listening, above all, to the particular silence that meant a
ship out there had found the beam and turned safely away from the rock.

Mara had inherited the listening along with the keys. She had not
expected to inherit the loneliness as well, though she supposed it had
always been bundled in with the rest, quietly, the way salt gets into
everything on this coast whether you invite it or not.

At half past eleven the wavering beam steadied on its own, and she
did not know whether to credit the storm, the tide, or her own
stubborn tinkering three hours before. She wrote it down anyway:
"Second glass true again. Cause unknown." Her father would have
wanted the honesty more than the explanation.

By midnight the wind had backed fully into the north and the rain had
turned to something almost gentle, a fine mist that blurred the beam
into a soft halo visible for miles. She thought of all the ships that
would never know her name, would never know there had been a woman
awake in a tower arguing with a lens at eleven o'clock on a Tuesday.

That was, she decided, the whole of the job: to be argued with by
machinery in the dark so that strangers could sleep through a passage
they would never remember making. It was not a job that asked to be
thanked. It only asked to be done, and done again tomorrow.

She closed the ledger a few minutes before one, banked the coffee, and
climbed the last flight to check the lamp with her own eyes one final
time before she allowed herself to rest. The beam swept out across
the black water, steady now, patient, indifferent to the storm and to
her both, doing the one thing it had always done: turning, and
turning, and turning toward whatever was still out there in the dark.
)TXT";
  return kText;
}

// ---------------------------------------------------------------------
// Sample 2 (Markdown): exercises every block the MarkdownParser subset
// supports -- h1/h2/h3, bold/italic/mono, a nested + an ordered list, a
// blockquote, a fenced code block, a thematic break, a link, and an
// image (which the parser drops). This doubles as the on-device format
// demo: if a rendering regression lands, this is the page that shows it.
// ---------------------------------------------------------------------
inline const std::string &sampleMarkdown() {
  static const std::string kText = R"MD(# Inkwell Format Demo

This page exercises every block the reader's Markdown subset supports, so
it doubles as an on-device rendering check. It has **bold text**,
*italic text*, and `inline mono`.

## Lists

- Top-level item one
- Top-level item two
  - Nested item under two
    - Doubly-nested item under that
- Top-level item three

1. First ordered step
2. Second ordered step
3. Third ordered step

## Quoting and Code

> A blockquote can run several lines and still join into one paragraph
> of quoted text, the way this one does.

```
def hello():
    print("hello, inkwell")
```

---

## Links and Images

See the [project repository](https://example.com/crow-panel) for source.
This image never appears on the page -- images are parsed and dropped:

![a diagram that will not render](https://example.com/diagram.png)

### Small Print

That covers every supported block type in one file: headings at three
levels, inline emphasis, a nested list, an ordered list, a blockquote, a
fenced code block, a thematic break, a link, and a dropped image.
)MD";
  return kText;
}

// ---------------------------------------------------------------------
// Sample 3 (EPUB): a small 3-chapter book built in RAM once, on first
// call, via miniz's zip writer -- the same shape a real downloaded EPUB
// would have (container.xml -> content.opf -> spine + a nav-doc TOC),
// just small enough to hand-verify. Reuses the same manifest/spine/nav
// structure as the host tests' buildFixtureEpub(), with real (if brief)
// prose instead of one-line placeholders.
// ---------------------------------------------------------------------
namespace detail {

// Every miniz writer call is return-checked; on any failure this returns a
// static empty string rather than risking std::string(nullptr, 0) UB from
// an unfilled (buf=nullptr, size=0) finalize result -- the same hazard
// InkBook.cpp's bufferToString() guards against for a null data pointer.
// LibraryStore::begin() additionally skips registering a zero-byte sample
// (see its own comment), so an empty result here degrades to "one fewer
// library entry" rather than a bad BookEntry.
inline std::string buildSampleEpubZip() {
  mz_zip_archive zip{};
  if (!mz_zip_writer_init_heap(&zip, 0, 32 * 1024)) return std::string();

  bool ok = true;
  auto add = [&](const char *name, const std::string &data,
                 int levelAndFlags = MZ_DEFAULT_COMPRESSION) {
    if (!ok) return;
    if (!mz_zip_writer_add_mem(&zip, name, data.data(), data.size(), (mz_uint)levelAndFlags)) {
      ok = false;
    }
  };

  // Per the OCF spec, the "mimetype" entry must be stored (uncompressed),
  // not deflated -- it's the one byte-exact signature a real EPUB reader
  // is allowed to sniff before parsing any XML. Every other entry keeps
  // MZ_DEFAULT_COMPRESSION.
  add("mimetype", "application/epub+zip", MZ_NO_COMPRESSION);
  add("META-INF/container.xml",
      "<?xml version=\"1.0\"?><container><rootfiles>"
      "<rootfile full-path=\"OEBPS/content.opf\" "
      "media-type=\"application/oebps-package+xml\"/>"
      "</rootfiles></container>");

  add("OEBPS/content.opf",
      "<?xml version=\"1.0\"?><package><metadata>"
      "<dc:title>The Inkwell Sampler</dc:title>"
      "<dc:creator>Project 25</dc:creator>"
      "</metadata><manifest>"
      "<item id=\"c1\" href=\"ch1.xhtml\" media-type=\"application/xhtml+xml\"/>"
      "<item id=\"c2\" href=\"ch2.xhtml\" media-type=\"application/xhtml+xml\"/>"
      "<item id=\"c3\" href=\"ch3.xhtml\" media-type=\"application/xhtml+xml\"/>"
      "<item id=\"nav\" href=\"nav.xhtml\" media-type=\"application/xhtml+xml\" "
      "properties=\"nav\"/>"
      "</manifest><spine>"
      "<itemref idref=\"c1\"/><itemref idref=\"c2\"/><itemref idref=\"c3\"/>"
      "</spine></package>");

  add("OEBPS/ch1.xhtml",
      "<html><body>"
      "<h1>The Blank Page</h1>"
      "<p>Every sampler begins the same way: a cursor, or a nib, or a "
      "stylus, poised over a surface that has not yet decided to hold "
      "anything. The page does not care what fills it. That indifference "
      "is either the most frightening thing about writing or the most "
      "freeing, depending on the day.</p>"
      "<p>Ink is patient in a way typing never quite manages to be. Once "
      "it is down, it is down, and the writer either keeps going or "
      "starts a fresh sheet -- there is no quiet backspace, no blinking "
      "cursor pretending nothing happened. Maybe that is why the old "
      "habit of a <em>commonplace book</em> persisted for so long: a "
      "place to put a thought before it evaporated, imperfect but "
      "permanent.</p>"
      "</body></html>");

  add("OEBPS/ch2.xhtml",
      "<html><body>"
      "<h1>The Marginalia</h1>"
      "<p>Long after an author sets a book down, its readers keep "
      "writing in it -- not on new pages, but in the borrowed white space "
      "around the old ones. A pencil tick beside a stanza. An underlined "
      "sentence with three exclamation points. A recipe, once, scrawled "
      "sideways across a shipping almanac because it was the nearest "
      "paper to hand at the time.</p>"
      "<p>A secondhand book is never really one text; it is a "
      "conversation between whoever wrote it and everyone who argued "
      "with it afterward in the margins, in another decade, in another "
      "hand. The printed words hold still. Everything around them keeps "
      "moving.</p>"
      "</body></html>");

  add("OEBPS/ch3.xhtml",
      "<html><body>"
      "<h1>The Long Shelf</h1>"
      "<p>A library does not really store books so much as it stores "
      "time -- shelves of decades a reader can walk into sideways, "
      "picking up a stranger's afternoon exactly where they left it. "
      "Nothing else works quite that way: not recordings, not "
      "photographs, nothing that asks so little of the machine reading "
      "it back.</p>"
      "<p>That is the quiet promise behind every format this reader "
      "understands, plain text, Markdown, or EPUB alike: that a run of "
      "bytes, kept honestly, can still mean something to someone who "
      "was not born when it was written. The sampler ends here, but the "
      "shelf, of course, does not.</p>"
      "</body></html>");

  add("OEBPS/nav.xhtml",
      "<html><body><nav epub:type=\"toc\"><ol>"
      "<li><a href=\"ch1.xhtml\">The Blank Page</a></li>"
      "<li><a href=\"ch2.xhtml\">The Marginalia</a></li>"
      "<li><a href=\"ch3.xhtml\">The Long Shelf</a></li>"
      "</ol></nav></body></html>");

  if (!ok) {
    mz_zip_writer_end(&zip);
    return std::string();
  }

  void *buf = nullptr;
  size_t size = 0;
  if (!mz_zip_writer_finalize_heap_archive(&zip, &buf, &size)) {
    mz_zip_writer_end(&zip);
    return std::string();
  }
  std::string out(static_cast<const char *>(buf), size);
  mz_zip_writer_end(&zip);
  mz_free(buf);
  return out;
}

}  // namespace detail

inline const std::string &sampleEpub() {
  static const std::string kZip = detail::buildSampleEpubZip();
  return kZip;
}

}  // namespace Ink

#endif
