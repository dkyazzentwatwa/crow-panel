#ifndef LITTLEHAKR_RF_LAB_PROFILE_H
#define LITTLEHAKR_RF_LAB_PROFILE_H

// Copy to LabProfile.h only after confirming the connected equipment and the
// frequencies are yours or explicitly authorized for this lab session.
#define RF_LAB_PROFILE_CONFIRMED 1
#define RF_LAB_PROFILE_NAME "OWNED-433-NRF24"

// Fixed receive-only test profile. These values cannot be changed at runtime.
#define RF_LAB_NRF_CHANNEL 76
#define RF_LAB_CC1101_FREQUENCY_HZ 433920000UL

#endif
