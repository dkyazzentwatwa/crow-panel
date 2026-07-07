#ifndef CROW_PANEL_WORLD_FEED_H
#define CROW_PANEL_WORLD_FEED_H

#include <Arduino.h>
#include <math.h>

enum AuroraVerdict { kAuroraQuiet = 0, kAuroraWatch = 1, kAuroraLikely = 2 };

struct WeatherData {
  float tempC = NAN;
  float feelsC = NAN;
  float windKt = NAN;
  float hiC = NAN;
  float loC = NAN;
  String condition = "";
};

struct QuakeData {
  float mag = NAN;
  float depthKm = NAN;
  String place = "";
  long ageMin = -1;   // -1 = unknown (clock not synced yet)
  int count24h = -1;  // -1 = unknown
};

struct AuroraData {
  float kp = NAN;
  String level = "";  // NOAA G-scale text, e.g. "G1"
  AuroraVerdict verdict = kAuroraQuiet;
  int trend = 0;  // -1 falling, 0 flat, +1 rising
};

struct AirQualityData {
  float usAqi = NAN;
  float pm25 = NAN;
  float pm10 = NAN;
  float ozone = NAN;
  float uvIndex = NAN;
  String category = "";
};

// One snapshot of the public world feeds, with per-feed validity, last-update
// millis() for staleness dimming, and the last concrete fetch error.
struct WorldFeeds {
  WeatherData weather;
  bool weatherValid = false;
  unsigned long weatherMs = 0;
  String weatherError = "";

  QuakeData quake;
  bool quakeValid = false;
  unsigned long quakeMs = 0;
  String quakeError = "";

  AuroraData aurora;
  bool auroraValid = false;
  unsigned long auroraMs = 0;
  String auroraError = "";

  AirQualityData air;
  bool airValid = false;
  unsigned long airMs = 0;
  String airError = "";
};

#endif
