#ifndef ADSB_RADAR_LOCATION_H
#define ADSB_RADAR_LOCATION_H

// Copy this file to Location.h (gitignored) and set your radar's home center.
// These override the committed JFK placeholder in ProjectConfig.h, so your
// real location never gets committed. The radar plots every aircraft relative
// to this point, and the API is queried for planes within ADSB_RANGE_NM of it.

#define ADSB_HOME_LAT 40.6413   // your latitude  (decimal degrees, +N)
#define ADSB_HOME_LON -73.7781  // your longitude (decimal degrees, +E)
#define ADSB_RANGE_NM 54        // query radius in nautical miles (<=250)

// Optional: POSIX TZ for the on-screen clock (see ProjectConfig.h for default).
// #define ADSB_TZ "CET-1CEST,M3.5.0,M10.5.0/3"

#endif
