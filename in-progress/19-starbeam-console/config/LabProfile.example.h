#ifndef STARBEAM_CONSOLE_LAB_PROFILE_H
#define STARBEAM_CONSOLE_LAB_PROFILE_H

// Copy to LabProfile.h (gitignored) only after confirming that the connected
// radios, antennas, and target frequencies are yours or explicitly authorized
// for this lab session. This arms every RF transmit path on the panel (all
// nRF24/CC1101 jammers) and lets the console forward attack commands to the
// UART co-processor. Leave it uncopied to run receive/analysis only.

#define STARBEAM_TX_CONFIRMED 1
#define STARBEAM_PROFILE_NAME "OWNED-LAB"

#endif  // STARBEAM_CONSOLE_LAB_PROFILE_H
