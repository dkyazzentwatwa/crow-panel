#ifndef VISION_CAM_SECRETS_H
#define VISION_CAM_SECRETS_H

// Copy this file to CamSecrets.h (gitignored) and set your own values.
// Only meaningful when built with -DUSE_WIFI=1.
//
// This device serves a live camera feed over HTTP. Anyone who can join the
// access point can watch it, so the password is not optional - leave
// VISIONCAM_AP_PASS unset and the panel will run with the insecure
// placeholder default and say so on the STREAM screen.
//
// WPA2 requires 8-63 characters. Pick something you would not mind typing on
// a phone keyboard, because that is exactly what you will be doing.

// Leave the SSID empty to auto-derive "CypherCam-XXXX" from the panel's MAC,
// which keeps two panels in the same room from colliding.
#define VISIONCAM_AP_SSID ""
#define VISIONCAM_AP_PASS "change-this-password"

#endif
