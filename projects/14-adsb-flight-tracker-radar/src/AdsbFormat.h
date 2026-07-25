#ifndef ADSB_FORMAT_H
#define ADSB_FORMAT_H

#include <Arduino.h>
#include <stdio.h>
#include <string.h>
#include "AdsbTypes.h"

// Presentation-only string formatting for aircraft fields. Deliberately free of
// any display dependency so it compiles in headless builds too, and so the
// scope (which draws the detail card) and the dashboard (which draws the list
// rows) render the same value the same way.
//
// NOTE: the vendored FreeSans fonts only cover ASCII 0x20..0x7E - there is no
// degree sign, bullet or en-dash glyph. Everything here stays ASCII; bearings
// and tracks are drawn as an arrow primitive plus bare digits instead.

// Altitude with thousands separators, or the reason there isn't one.
// "34,000 ft" / "950 ft" / "ON GROUND" / "ALT UNKNOWN"
inline void fmtAlt(char *out, size_t n, const Aircraft &a) {
  if (a.onGround) {
    snprintf(out, n, "ON GROUND");
    return;
  }
  if (!a.haveAlt) {
    snprintf(out, n, "ALT UNKNOWN");
    return;
  }
  long ft = (long)a.altFt;
  bool neg = ft < 0;
  if (neg) ft = -ft;
  if (ft < 1000) {
    snprintf(out, n, "%s%ld ft", neg ? "-" : "", ft);
  } else {
    snprintf(out, n, "%s%ld,%03ld ft", neg ? "-" : "", ft / 1000, ft % 1000);
  }
}

// ADS-B emitter category code -> a word a human can read. Returns "" when the
// code is absent or unmapped, so callers can fall back to the type code.
// Categories per DO-260B / the ADS-B "category" field the feeds expose.
inline const char *categoryLabel(const char *cat) {
  if (cat == nullptr || cat[0] == '\0') return "";
  if (cat[0] == 'A') {
    switch (cat[1]) {
      case '1': return "LIGHT";
      case '2': return "SMALL";
      case '3': return "LARGE";
      case '4': return "HI-VTX";
      case '5': return "HEAVY";
      case '6': return "HI-PERF";
      case '7': return "ROTOR";
      default: return "";
    }
  }
  if (cat[0] == 'B') {
    switch (cat[1]) {
      case '1': return "GLIDER";
      case '2': return "BALLOON";
      case '3': return "PARACHUTE";
      case '4': return "ULTRALIGHT";
      case '6': return "UAV";
      case '7': return "SPACE";
      default: return "";
    }
  }
  if (cat[0] == 'C') return "SURFACE";
  return "";
}

// "B738 HEAVY" / "B738" / "HEAVY" / "" - the type + category chip.
inline void fmtTypeCat(char *out, size_t n, const Aircraft &a) {
  const char *c = categoryLabel(a.category);
  if (a.type[0] && c[0]) {
    snprintf(out, n, "%s %s", a.type, c);
  } else if (a.type[0]) {
    snprintf(out, n, "%s", a.type);
  } else {
    snprintf(out, n, "%s", c);
  }
}

// Bearings and tracks are always three digits, no degree glyph available.
inline int bearing360(float deg) {
  int d = (int)(deg + 0.5f) % 360;
  if (d < 0) d += 360;
  return d;
}

#endif
