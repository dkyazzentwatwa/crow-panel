#ifndef SURVEYOPS_PROJECT_CONFIG_H
#define SURVEYOPS_PROJECT_CONFIG_H

#include <AppConfig.h>

#ifndef SURVEYOPS_GPS_SERIAL_BAUD
#define SURVEYOPS_GPS_SERIAL_BAUD 9600
#endif

#ifndef SURVEYOPS_GPS_RX_PIN
#define SURVEYOPS_GPS_RX_PIN 52
#endif

#ifndef SURVEYOPS_GPS_TX_PIN
#define SURVEYOPS_GPS_TX_PIN 51
#endif

// IO51/IO52 are routable through the P4 GPIO matrix, but are also used by
// other external-bus conventions in this repo. Verify no attached module uses
// them before wiring the GPS.
#ifndef SURVEYOPS_SDMMC_1BIT
#define SURVEYOPS_SDMMC_1BIT 1
#endif

#ifndef SURVEYOPS_WIGLE_FILE_PREFIX
#define SURVEYOPS_WIGLE_FILE_PREFIX "/wigle"
#endif

#ifndef SURVEYOPS_WIGLE_ROTATE_ROWS
#define SURVEYOPS_WIGLE_ROTATE_ROWS 200
#endif

#endif
