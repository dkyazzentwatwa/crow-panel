#ifndef NFC_FIELD_LAB_PROJECT_CONFIG_H
#define NFC_FIELD_LAB_PROJECT_CONFIG_H

#include <AppConfig.h>

// Real reader flags default to off in AppConfig.h. Enable with EXTRA_FLAGS:
//   -DUSE_PN532_DRIVER=1
//   -DUSE_MFRC522_DRIVER=1

#ifndef NFC_LAB_PN532_IRQ
#define NFC_LAB_PN532_IRQ -1
#endif

#ifndef NFC_LAB_PN532_RESET
#define NFC_LAB_PN532_RESET -1
#endif

#ifndef NFC_LAB_MFRC522_SS
#define NFC_LAB_MFRC522_SS 10
#endif

#ifndef NFC_LAB_MFRC522_RST
#define NFC_LAB_MFRC522_RST 9
#endif

#ifndef NFC_LAB_MAX_NDEF_PREVIEW_BYTES
#define NFC_LAB_MAX_NDEF_PREVIEW_BYTES 48
#endif

#ifndef NFC_LAB_APDU_RESPONSE_BYTES
#define NFC_LAB_APDU_RESPONSE_BYTES 64
#endif

#endif
