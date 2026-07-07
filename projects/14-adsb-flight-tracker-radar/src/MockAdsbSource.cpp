#include "MockAdsbSource.h"

#include <math.h>
#include <string.h>

#include "GeoUtils.h"

namespace {
// A little variety across the fleet: type code and a plausible cruise altitude
// band so the altitude color coding (blue/green/amber/red) is visible at once.
const char *kTypes[] = {"B738", "A320", "B77W", "A319", "A21N", "E75L", "C172", "B739"};
const int32_t kAlts[] = {34000, 31500, 37000, 8200, 24000, 12500, 3100, -1};  // -1 => unknown
const float kSpeeds[] = {452, 438, 501, 291, 447, 402, 122, 470};
}  // namespace

void MockAdsbSource::begin(double homeLat, double homeLon, float rangeKm) {
  homeLat_ = homeLat;
  homeLon_ = homeLon;
  rangeKm_ = rangeKm;
  lastMs_ = millis();

  for (uint8_t i = 0; i < kCount; i++) {
    MockPlane &p = planes_[i];
    p.trackDeg = fmodf(37.0f * i + 15.0f, 360.0f);
    p.speedKt = kSpeeds[i];
    p.altFt = kAlts[i];
    p.haveAlt = (kAlts[i] >= 0);
    snprintf(p.icao, sizeof(p.icao), "%06X", (unsigned)(0xA07500u + i * 37u));
    snprintf(p.callsign, sizeof(p.callsign), "SIM%03u", (unsigned)(101 + i));
    strncpy(p.type, kTypes[i], sizeof(p.type) - 1);
    p.type[sizeof(p.type) - 1] = '\0';
    // Spread the initial positions around the ring at a fraction of range.
    double frac = 0.30 + 0.075 * i;
    if (frac > 0.95) frac = 0.95;
    GeoUtils::destPoint(homeLat_, homeLon_, rangeKm_ * frac, 43.0 * i, p.lat, p.lon);
  }
  Logger::info("mock", "synthetic ADS-B source ready (" + String(kCount) + " aircraft), no network");
}

// Re-enter from the far side (opposite the heading) so the plane flies back
// across the scope along its current track.
void MockAdsbSource::respawn_(MockPlane &p) {
  double spawnBrg = fmod((double)p.trackDeg + 180.0, 360.0);
  GeoUtils::destPoint(homeLat_, homeLon_, rangeKm_ * 0.92, spawnBrg, p.lat, p.lon);
}

bool MockAdsbSource::tick(AircraftStore &store) {
  if (!cadence_.ready()) return false;

  uint32_t now = millis();
  float dt = (now - lastMs_) / 1000.0f;
  if (dt <= 0) dt = 1.0f;
  lastMs_ = now;

  for (uint8_t i = 0; i < kCount; i++) {
    MockPlane &p = planes_[i];

    // Advance along track: knots -> km travelled this interval.
    double stepKm = (double)p.speedKt * 1.852 / 3600.0 * dt;
    GeoUtils::destPoint(p.lat, p.lon, stepKm, p.trackDeg, p.lat, p.lon);

    float dist = GeoUtils::haversineKm(homeLat_, homeLon_, p.lat, p.lon);
    if (dist > rangeKm_ * 1.05f) {
      respawn_(p);
      dist = GeoUtils::haversineKm(homeLat_, homeLon_, p.lat, p.lon);
    }

    Aircraft a;
    memset(&a, 0, sizeof(a));
    strncpy(a.icao, p.icao, sizeof(a.icao) - 1);
    strncpy(a.callsign, p.callsign, sizeof(a.callsign) - 1);
    strncpy(a.type, p.type, sizeof(a.type) - 1);
    a.category[0] = '\0';
    a.lat = p.lat;
    a.lon = p.lon;
    a.altFt = p.haveAlt ? p.altFt : 0;
    a.haveAlt = p.haveAlt;
    a.onGround = false;
    a.groundSpeedKt = p.speedKt;
    a.trackDeg = p.trackDeg;
    a.haveTrack = true;
    a.distanceKm = dist;
    a.bearingDeg = GeoUtils::bearingDeg(homeLat_, homeLon_, p.lat, p.lon);
    a.lastSeenMs = now;
    store.upsert(a);
  }

  store.commit("mock", kCount);
  return true;
}
