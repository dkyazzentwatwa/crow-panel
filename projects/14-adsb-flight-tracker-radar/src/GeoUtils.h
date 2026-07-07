#ifndef ADSB_GEO_UTILS_H
#define ADSB_GEO_UTILS_H

#include <Arduino.h>
#include <math.h>

// Great-circle helpers on the WGS84 mean radius. Header-only inline: tiny, hot,
// and used by both the live client and the mock source.
namespace GeoUtils {

static const double kEarthKm = 6371.0088;

inline double toRad(double deg) { return deg * M_PI / 180.0; }
inline double toDeg(double rad) { return rad * 180.0 / M_PI; }

// Great-circle distance between two lat/lon points, in km.
inline float haversineKm(double lat1, double lon1, double lat2, double lon2) {
  double dLat = toRad(lat2 - lat1);
  double dLon = toRad(lon2 - lon1);
  double a = sin(dLat / 2) * sin(dLat / 2) +
             cos(toRad(lat1)) * cos(toRad(lat2)) * sin(dLon / 2) * sin(dLon / 2);
  return (float)(2.0 * kEarthKm * atan2(sqrt(a), sqrt(1.0 - a)));
}

// Initial bearing from point 1 to point 2, degrees clockwise from true north.
inline float bearingDeg(double lat1, double lon1, double lat2, double lon2) {
  double y = sin(toRad(lon2 - lon1)) * cos(toRad(lat2));
  double x = cos(toRad(lat1)) * sin(toRad(lat2)) -
             sin(toRad(lat1)) * cos(toRad(lat2)) * cos(toRad(lon2 - lon1));
  double b = toDeg(atan2(y, x));
  if (b < 0) b += 360.0;
  return (float)b;
}

// Point reached by travelling distKm from (lat,lon) along bearing brgDeg.
// Used by the mock source to advance and respawn aircraft.
inline void destPoint(double lat, double lon, double distKm, double brgDeg,
                      double &outLat, double &outLon) {
  double br = toRad(brgDeg);
  double dr = distKm / kEarthKm;
  double la1 = toRad(lat);
  double lo1 = toRad(lon);
  double la2 = asin(sin(la1) * cos(dr) + cos(la1) * sin(dr) * cos(br));
  double lo2 = lo1 + atan2(sin(br) * sin(dr) * cos(la1),
                           cos(dr) - sin(la1) * sin(la2));
  outLat = toDeg(la2);
  outLon = toDeg(lo2);
}

}  // namespace GeoUtils

#endif
