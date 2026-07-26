#ifndef CROW_HID_H
#define CROW_HID_H

// Umbrella for the shared CrowHid stack: a USB + BLE HID output layer extracted
// from project 21 (Cypher Keys) so project 05 (CypherDrive) and project 21 share
// one transport implementation. Include this to get the full API; the flags
// USE_USB_HID / USE_BLE_HID (AppConfig.h defaults, passed as -D for real builds)
// gate the live transports, and everything degrades to a mock logging path when
// they are off. See CrowHidBackend for the high-level API.
#include "CrowHidTypes.h"
#include "CrowHidTransport.h"
#include "CrowHidKeycodes.h"
#include "CrowUsbTransport.h"
#include "CrowBleTransport.h"
#include "CrowHidBackend.h"

#endif
