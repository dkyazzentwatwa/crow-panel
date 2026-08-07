#include "EpubBook.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <map>

#include "XhtmlParser.h"  // Ink::decodeEntities, reused for TOC titles/metadata.
#include "miniz.h"

namespace Ink {

namespace {

bool isWsChar(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

std::string trim(const std::string &s) {
  size_t b = 0, e = s.size();
  while (b < e && isWsChar(s[b])) ++b;
  while (e > b && isWsChar(s[e - 1])) --e;
  return s.substr(b, e - b);
}

// Collapses runs of whitespace to a single space and trims the ends --
// applied to TOC titles pulled out of nav/ncx documents, the same
// normalization XhtmlParser applies to block text.
std::string collapseWs(const std::string &s) {
  std::string out;
  bool pendingSpace = false;
  for (char c : s) {
    if (isWsChar(c)) {
      if (!out.empty()) pendingSpace = true;
      continue;
    }
    if (pendingSpace) {
      out += ' ';
      pendingSpace = false;
    }
    out += c;
  }
  return out;
}

// Bound for a namespace-prefix lookahead ("<opf:spine" style) -- generous
// for any real tag/prefix name, small enough to keep the scan bounded
// rather than an unbounded find() the way XhtmlParser bounds its own
// tag-close lookahead.
constexpr size_t kNsPrefixWindow = 32;

// Bounded search for a single character within [from, limit) -- never
// scans past `limit` even when `ch` doesn't occur before the end of the
// whole string, which is what keeps callers below (the namespace-prefix
// colon probe, and attrInSpan's quote-close search) from degrading into a
// document-wide scan on malformed or attribute-less input.
size_t findCharBounded(const std::string &s, char ch, size_t from, size_t limit) {
  size_t end = std::min(s.size(), limit);
  for (size_t j = from; j < end; ++j) {
    if (s[j] == ch) return j;
  }
  return std::string::npos;
}

// Locates the next opening tag whose local name is `localName`, at or
// after `from`. Tolerates an XML namespace prefix on the tag itself (e.g.
// "<opf:spine" as well as plain "<spine") -- some EPUB generators prefix
// the OPF package/metadata/manifest/spine wrapper elements. A single
// forward pass over '<' occurrences (each visited once across the whole
// document, even when this is called repeatedly with an advancing `from`
// to walk a list of sibling tags), so aggregate cost stays O(n) rather
// than O(n^2). Returns the position of the tag's '<', or npos.
size_t findTagLocal(const std::string &xml, const std::string &localName, size_t from) {
  size_t n = xml.size();
  size_t pos = from;
  while (pos < n) {
    size_t lt = xml.find('<', pos);
    if (lt == std::string::npos) return std::string::npos;
    size_t p = lt + 1;
    if (p < n && xml[p] == '/') {  // closing tag: never a match for an open-tag search
      pos = p;
      continue;
    }
    size_t nameStart = p;
    size_t colon = findCharBounded(xml, ':', p, p + kNsPrefixWindow);
    if (colon != std::string::npos) {
      bool prefixLooksValid = true;
      for (size_t k = p; k < colon; ++k) {
        if (!std::isalnum((unsigned char)xml[k])) {
          prefixLooksValid = false;
          break;
        }
      }
      if (prefixLooksValid && colon > p) nameStart = colon + 1;
    }
    if (xml.compare(nameStart, localName.size(), localName) == 0) {
      size_t after = nameStart + localName.size();
      if (after >= n || isWsChar(xml[after]) || xml[after] == '>' || xml[after] == '/') {
        return lt;
      }
    }
    pos = lt + 1;
  }
  return std::string::npos;
}

// Extracts attrName="value" (single or double quotes tolerated) from
// within [tagStart, tagEnd) -- a manual, TRULY bounded scan. This does NOT
// delegate to std::string::find(attrName, searchFrom): that searches all
// the way to the end of the *entire document* before ever checking
// whether the match landed past tagEnd, so a missing attribute (e.g.
// `properties` on an ordinary manifest item -- true for nearly every
// item) would cost a full-document scan per call, turning an N-item
// manifest into O(n^2). Instead this walks only the [tagStart, tagEnd)
// span itself char-by-char, and the closing-quote search is bounded the
// same way via findCharBounded -- so cost is proportional to one tag's
// length, never the document's, even when the attribute is absent.
std::string attrInSpan(const std::string &s, size_t tagStart, size_t tagEnd,
                        const std::string &attrName) {
  size_t alen = attrName.size();
  if (alen == 0 || tagEnd <= tagStart || alen > tagEnd - tagStart) return "";
  for (size_t i = tagStart; i + alen <= tagEnd; ++i) {
    if (s.compare(i, alen, attrName) != 0) continue;
    size_t eq = i + alen;
    while (eq < tagEnd && isWsChar(s[eq])) ++eq;
    if (eq >= tagEnd || s[eq] != '=') continue;
    size_t q = eq + 1;
    while (q < tagEnd && isWsChar(s[q])) ++q;
    if (q >= tagEnd || (s[q] != '"' && s[q] != '\'')) continue;
    char quote = s[q];
    size_t vend = findCharBounded(s, quote, q + 1, tagEnd);
    if (vend == std::string::npos) continue;
    return s.substr(q + 1, vend - (q + 1));
  }
  return "";
}

// Finds `tagNeedle` (e.g. "<rootfile") at or after `from`, then extracts
// `attrName="value"` from within that same tag (up to its closing '>').
// Returns "" if the tag or the attribute isn't found. A match is only
// accepted at a real tag-name boundary (next char is whitespace, '>' or
// '/') so a needle like "<rootfile" can't be fooled by the wrapper
// "<rootfiles>" it sits inside of -- "<rootfile" is a literal prefix of
// "<rootfiles", and a plain substring search would stop at the wrapper's
// own (attribute-less) '>' before ever reaching the real child tag.
std::string attrValue(const std::string &xml, const std::string &tagNeedle,
                       const std::string &attrName, size_t from = 0) {
  size_t searchFrom = from;
  for (;;) {
    size_t tagPos = xml.find(tagNeedle, searchFrom);
    if (tagPos == std::string::npos) return "";
    size_t after = tagPos + tagNeedle.size();
    if (after < xml.size() && !isWsChar(xml[after]) && xml[after] != '>' && xml[after] != '/') {
      searchFrom = tagPos + 1;
      continue;
    }
    size_t tagEnd = xml.find('>', tagPos);
    if (tagEnd == std::string::npos) tagEnd = xml.size();
    return attrInSpan(xml, tagPos, tagEnd, attrName);
  }
}

// Extracts the text content of a `<dc:LOCALNAME ...>...</dc:LOCALNAME>`
// element (dc:title / dc:creator). Reuses findTagLocal() -- already
// tolerant of a namespace prefix ("dc:title", "opf:title", or unprefixed
// "title") AND already boundary-aware -- to locate the OPENING tag
// specifically, whether or not it carries attributes (e.g. EPUB3 refines:
// `<dc:title id="t1">`, `<dc:creator opf:role="aut">`). Content is taken
// from just after that tag's own '>' to the next '<' -- dc:title/creator
// content never contains nested markup in practice, so the closing tag
// (whatever its exact prefix) is always that next '<'.
//
// A naive "find the literal string ':LOCALNAME>'" fallback (the prior
// approach) breaks the instant the open tag has attributes: for
// `<dc:title id="t1">Attributed Title</dc:title>`, ":title>" does NOT
// occur in the open tag (attributes intervene before '>') but DOES occur
// in the CLOSE tag "</dc:title>" -- so that fallback would anchor on the
// closing tag and return empty/wrong content. findTagLocal avoids this
// because it explicitly rejects closing tags (a '/' right after '<') and
// requires a real tag-boundary character after the name, not a literal
// '>'.
std::string extractSimpleText(const std::string &xml, const std::string &localName) {
  size_t tagPos = findTagLocal(xml, localName, 0);
  if (tagPos == std::string::npos) return "";
  size_t tagEnd = xml.find('>', tagPos);
  if (tagEnd == std::string::npos) return "";
  size_t contentStart = tagEnd + 1;
  size_t end = xml.find('<', contentStart);
  if (end == std::string::npos) end = xml.size();
  return trim(decodeEntities(xml.substr(contentStart, end - contentStart)));
}

// Joins an OPF-relative href onto the OPF's own directory, stripping a
// redundant leading "./" first.
std::string resolveHref(const std::string &dir, const std::string &href) {
  std::string h = href;
  while (h.size() >= 2 && h[0] == '.' && h[1] == '/') h = h.substr(2);
  return dir + h;
}

struct ManifestItem {
  std::string href, mediaType, properties;
};

}  // namespace

bool EpubBook::open(const uint8_t *data, size_t size) {
  static_assert(sizeof(mz_zip_archive) <= sizeof(zip_),
                "mz_zip_archive no longer fits EpubBook::zip_ -- grow the buffer in "
                "EpubBook.h to at least sizeof(mz_zip_archive) reported by this error");
  close();
  auto *zip = reinterpret_cast<mz_zip_archive *>(zip_);
  std::memset(zip_, 0, sizeof(zip_));
  if (!mz_zip_reader_init_mem(zip, data, size, 0)) return false;
  zipOpen_ = true;

  std::string container;
  if (!readEntry("META-INF/container.xml", container)) {
    close();
    return false;
  }
  std::string fullPath = attrValue(container, "<rootfile", "full-path");
  if (fullPath.empty()) {
    close();
    return false;
  }

  std::string opf;
  if (!readEntry(fullPath, opf)) {
    close();
    return false;
  }

  size_t slash = fullPath.find_last_of('/');
  opfDir_ = (slash == std::string::npos) ? "" : fullPath.substr(0, slash + 1);

  parseOpf(opf, opfDir_);
  return true;
}

void EpubBook::close() {
  if (zipOpen_) {
    auto *zip = reinterpret_cast<mz_zip_archive *>(zip_);
    mz_zip_reader_end(zip);
    zipOpen_ = false;
  }
  opfDir_.clear();
  title_.clear();
  author_.clear();
  spineHrefs_.clear();
  hrefToSpineIndex_.clear();
  coverHref_.clear();
  coverMedia_.clear();
  toc_.clear();
}

bool EpubBook::readEntry(const std::string &name, std::string &out) {
  if (!zipOpen_) return false;
  auto *zip = reinterpret_cast<mz_zip_archive *>(zip_);
  int idx = mz_zip_reader_locate_file(zip, name.c_str(), nullptr, 0);
  if (idx < 0) return false;
  size_t extractedSize = 0;
  void *buf = mz_zip_reader_extract_to_heap(zip, (mz_uint)idx, &extractedSize, 0);
  if (!buf) return false;
  out.assign((const char *)buf, extractedSize);
  mz_free(buf);
  return true;
}

uint32_t EpubBook::chapterSize(size_t i) const {
  if (!zipOpen_ || i >= spineHrefs_.size()) return 0;
  auto *zip = reinterpret_cast<mz_zip_archive *>(const_cast<unsigned char *>(zip_));
  int idx = mz_zip_reader_locate_file(zip, spineHrefs_[i].c_str(), nullptr, 0);
  if (idx < 0) return 0;
  mz_zip_archive_file_stat stat;
  if (!mz_zip_reader_file_stat(zip, (mz_uint)idx, &stat)) return 0;
  return (uint32_t)stat.m_comp_size;
}

bool EpubBook::chapterXhtml(size_t i, std::string &out) {
  if (i >= spineHrefs_.size()) return false;
  return readEntry(spineHrefs_[i], out);
}

bool EpubBook::coverImage(std::string &out, std::string &mediaType) {
  if (coverHref_.empty()) return false;
  if (!readEntry(coverHref_, out)) return false;
  mediaType = coverMedia_;
  return true;
}

int EpubBook::spineIndexForHref(const std::string &href) const {
  size_t hash = href.find('#');
  std::string h = (hash == std::string::npos) ? href : href.substr(0, hash);
  while (h.size() >= 2 && h[0] == '.' && h[1] == '/') h = h.substr(2);
  std::string resolved = opfDir_ + h;
  // O(log spine size) map lookup rather than a linear scan of the spine
  // -- called once per TOC entry, so a linear scan here would make a
  // large book's TOC parse O(toc x spine) instead of O(toc log spine).
  auto it = hrefToSpineIndex_.find(resolved);
  return (it != hrefToSpineIndex_.end()) ? it->second : -1;
}

void EpubBook::parseOpf(const std::string &opf, const std::string &opfDir) {
  title_ = extractSimpleText(opf, "title");
  author_ = extractSimpleText(opf, "creator");

  std::map<std::string, ManifestItem> manifest;
  std::string navId, coverPropId;

  size_t pos = 0;
  for (;;) {
    size_t tagPos = findTagLocal(opf, "item", pos);
    if (tagPos == std::string::npos) break;
    size_t tagEnd = opf.find('>', tagPos);
    if (tagEnd == std::string::npos) break;
    std::string id = attrInSpan(opf, tagPos, tagEnd, "id");
    if (!id.empty()) {
      ManifestItem mi;
      mi.href = resolveHref(opfDir, attrInSpan(opf, tagPos, tagEnd, "href"));
      mi.mediaType = attrInSpan(opf, tagPos, tagEnd, "media-type");
      mi.properties = attrInSpan(opf, tagPos, tagEnd, "properties");
      if (mi.properties.find("nav") != std::string::npos) navId = id;
      if (mi.properties.find("cover-image") != std::string::npos) coverPropId = id;
      manifest[id] = std::move(mi);
    }
    pos = tagEnd + 1;
  }

  pos = 0;
  for (;;) {
    size_t tagPos = findTagLocal(opf, "itemref", pos);
    if (tagPos == std::string::npos) break;
    size_t tagEnd = opf.find('>', tagPos);
    if (tagEnd == std::string::npos) break;
    std::string idref = attrInSpan(opf, tagPos, tagEnd, "idref");
    auto it = manifest.find(idref);
    if (it != manifest.end() && (it->second.mediaType == "application/xhtml+xml" ||
                                  it->second.mediaType == "text/html")) {
      spineHrefs_.push_back(it->second.href);
      // emplace(), not operator[]: a duplicate href in the spine (however
      // unusual) must resolve to the FIRST occurrence, matching the old
      // linear left-to-right scan's semantics -- operator[] would let a
      // later duplicate silently overwrite the earlier index.
      hrefToSpineIndex_.emplace(it->second.href, (int)spineHrefs_.size() - 1);
    }
    pos = tagEnd + 1;
  }

  // Cover: an item flagged properties="cover-image" wins; otherwise fall
  // back to the older <meta name="cover" content="ID"/> convention.
  if (!coverPropId.empty() && manifest.count(coverPropId)) {
    coverHref_ = manifest[coverPropId].href;
    coverMedia_ = manifest[coverPropId].mediaType;
  } else {
    std::string coverMetaId;
    pos = 0;
    for (;;) {
      size_t tagPos = findTagLocal(opf, "meta", pos);
      if (tagPos == std::string::npos) break;
      size_t tagEnd = opf.find('>', tagPos);
      if (tagEnd == std::string::npos) break;
      if (attrInSpan(opf, tagPos, tagEnd, "name") == "cover") {
        coverMetaId = attrInSpan(opf, tagPos, tagEnd, "content");
        break;
      }
      pos = tagEnd + 1;
    }
    if (!coverMetaId.empty() && manifest.count(coverMetaId)) {
      coverHref_ = manifest[coverMetaId].href;
      coverMedia_ = manifest[coverMetaId].mediaType;
    }
  }

  // TOC: an item flagged properties="nav" (EPUB3) wins; otherwise fall
  // back to the spine's toc="ID" pointer to an NCX document (EPUB2).
  if (!navId.empty() && manifest.count(navId)) {
    std::string navXhtml;
    if (readEntry(manifest[navId].href, navXhtml)) parseNavToc(navXhtml);
  } else {
    size_t spinePos = findTagLocal(opf, "spine", 0);
    if (spinePos != std::string::npos) {
      size_t spineTagEnd = opf.find('>', spinePos);
      if (spineTagEnd != std::string::npos) {
        std::string tocId = attrInSpan(opf, spinePos, spineTagEnd, "toc");
        if (!tocId.empty() && manifest.count(tocId)) {
          std::string ncx;
          if (readEntry(manifest[tocId].href, ncx)) parseNcxToc(ncx);
        }
      }
    }
  }
}

void EpubBook::parseNavToc(const std::string &navXhtml) {
  toc_.clear();
  size_t navPos = findTagLocal(navXhtml, "nav", 0);
  if (navPos == std::string::npos) return;
  size_t navOpenEnd = navXhtml.find('>', navPos);
  if (navOpenEnd == std::string::npos) return;
  size_t navCloseStart = navXhtml.find("</nav", navOpenEnd);
  size_t navEnd = (navCloseStart == std::string::npos) ? navXhtml.size() : navCloseStart;

  size_t pos = navOpenEnd + 1;
  while (pos < navEnd) {
    size_t aPos = findTagLocal(navXhtml, "a", pos);
    if (aPos == std::string::npos || aPos >= navEnd) break;
    size_t aTagEnd = navXhtml.find('>', aPos);
    if (aTagEnd == std::string::npos || aTagEnd >= navEnd) break;
    std::string href = attrInSpan(navXhtml, aPos, aTagEnd, "href");
    size_t closeA = navXhtml.find("</a", aTagEnd);
    size_t titleEnd = (closeA == std::string::npos || closeA > navEnd) ? navEnd : closeA;

    TocEntry entry;
    if (aTagEnd + 1 <= titleEnd)
      entry.title = collapseWs(decodeEntities(navXhtml.substr(aTagEnd + 1, titleEnd - (aTagEnd + 1))));
    entry.spineIndex = spineIndexForHref(href);
    toc_.push_back(std::move(entry));

    pos = (closeA == std::string::npos) ? navEnd : closeA + 1;
  }
}

void EpubBook::parseNcxToc(const std::string &ncx) {
  toc_.clear();
  size_t pos = 0;
  for (;;) {
    size_t npPos = findTagLocal(ncx, "navPoint", pos);
    if (npPos == std::string::npos) break;
    size_t npOpenEnd = ncx.find('>', npPos);
    if (npOpenEnd == std::string::npos) break;

    // Bound this navPoint's own <navLabel><text> and <content> search to
    // before the NEXT <navPoint opening tag (whether that next one is a
    // nested child or a sibling) -- not past </navPoint>, which for a
    // nested navPoint is the CHILD's own close. This bound is what makes
    // nesting flatten into document order (a nested child begins right
    // after the parent's own content, so it's naturally picked up on the
    // next loop iteration) while ALSO correctly leaving title/src empty,
    // rather than reaching into the NEXT navPoint's own text/content,
    // when this one is missing a <navLabel> or <content> (malformed or
    // simply degenerate input).
    size_t nextNp = findTagLocal(ncx, "navPoint", npOpenEnd);
    size_t npBound = (nextNp == std::string::npos) ? ncx.size() : nextNp;

    std::string title;
    size_t textPos = findTagLocal(ncx, "text", npOpenEnd);
    if (textPos != std::string::npos && textPos < npBound) {
      size_t textOpenEnd = ncx.find('>', textPos);
      if (textOpenEnd != std::string::npos && textOpenEnd < npBound) {
        size_t textCloseStart = ncx.find("</text", textOpenEnd);
        size_t textEnd = (textCloseStart == std::string::npos || textCloseStart > npBound)
                              ? npBound
                              : textCloseStart;
        if (textOpenEnd + 1 <= textEnd)
          title = collapseWs(decodeEntities(ncx.substr(textOpenEnd + 1, textEnd - (textOpenEnd + 1))));
      }
    }

    std::string src;
    size_t afterContentTag = npOpenEnd;  // resume point if <content> is missing/out of bound
    size_t contentPos = findTagLocal(ncx, "content", npOpenEnd);
    if (contentPos != std::string::npos && contentPos < npBound) {
      size_t contentTagEnd = ncx.find('>', contentPos);
      if (contentTagEnd != std::string::npos && contentTagEnd < npBound) {
        src = attrInSpan(ncx, contentPos, contentTagEnd, "src");
        afterContentTag = contentTagEnd;
      }
    }

    TocEntry entry;
    entry.title = title;
    entry.spineIndex = spineIndexForHref(src);
    toc_.push_back(std::move(entry));

    // Resume just past THIS navPoint's own <content .../> -- NOT past its
    // closing </navPoint>. A nested child <navPoint>, if any, begins
    // right after the parent's own content, so the next findTagLocal()
    // call picks it up as its own entry on the next iteration instead of
    // it being silently swallowed inside the parent's span. This
    // flattens nested navPoints into document order, the same way
    // parseNavToc() flattens nested <a> structure in a nav document.
    pos = afterContentTag + 1;
  }
}

}  // namespace Ink
