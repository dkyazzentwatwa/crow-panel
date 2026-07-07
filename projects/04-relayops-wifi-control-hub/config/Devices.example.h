#ifndef RELAYOPS_DEVICES_H
#define RELAYOPS_DEVICES_H

// Copy this file to Devices.h (gitignored) and edit for your network, then
// build with -DUSE_WIFI=1 so the hub can actually reach the devices.
//
// Each row registers one controllable ESP32 the hub can command. The hub
// sends an HTTP GET to toggle a pin:
//   GET http://<host><path>?pin=<pin>&state=<0|1>
// A minimal node firmware just needs to serve <path> and drive the pin.
//
// Nodes can ALSO self-register at runtime by POSTing to /register on the
// hub (see docs/tutorial.md); this static list seeds devices you always
// want present, even before they check in.

#define RELAYOPS_STATIC_DEVICES                                 \
  RELAYOPS_DEVICE("shop-light",  "192.168.1.50", "/gpio", 2)    \
  RELAYOPS_DEVICE("porch-relay", "192.168.1.51", "/gpio", 4)    \
  RELAYOPS_DEVICE("fan-relay",   "192.168.1.52", "/gpio", 5)

#endif
