#ifndef RELAYOPS_LOCATION_H
#define RELAYOPS_LOCATION_H

// Copy this file to Location.h (gitignored) and set your location. The
// weather card and the aurora "visible?" verdict use it. Defaults are
// Eugene, OR. Only meaningful with -DUSE_WIFI=1; mock mode ignores it.

#define RELAYOPS_LAT 44.05
#define RELAYOPS_LON -123.09
#define RELAYOPS_PLACE "Eugene, OR"

// Aurora is called "likely" at/above this planetary Kp for your latitude.
// Rough guide: ~7-8 for a 45N overhead show; lower to catch it on the
// northern horizon. This is an approximation, not a guarantee.
#define RELAYOPS_KP_THRESHOLD 6

#endif
